#include "app/NetController.h"

#include "core/Adif.h"
#include "core/ContestSerialProtocol.h"
#include "core/ContestNetReconcile.h"
#include "core/FileIdentity.h"
#include "core/LogDatabase.h"

#include <QHostInfo>
#include <QPointer>
#include <QSettings>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace decolog::app {

using namespace decolog::core;

namespace {
constexpr int kPeerTimeoutSecs = 30;
constexpr int kKeepMessages = 300;
constexpr int kResendBatch = 20;
}

NetController::NetController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    m_durablePool.setMaxThreadCount(1);
    m_qsoComparisonPool.setMaxThreadCount(1);
    m_qsoComparisonTimer.setInterval(750);
    connect(&m_qsoComparisonTimer,&QTimer::timeout,this,[this] {
        if (!m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path()!=m_qsoComparisonPath ||
            m_ctx.db->identity()!=m_qsoComparisonIdentity || m_ctx.db->openGeneration()!=m_qsoComparisonOpenGeneration ||
            m_ctx.db->openFileIdentity()!=m_qsoComparisonFileIdentity || !m_ctx.db->fileIdentityMatchesOpen() ||
            m_ctx.db->changeStamp()!=m_qsoComparisonStamp ||
            fileidentity::read(m_qsoComparisonRemotePath)!=m_qsoComparisonRemoteFileIdentity ||
            !m_qsoComparisonRemoteWatcher || !m_qsoComparisonRemoteWatcher->fileIdentityMatchesOpen() ||
            m_qsoComparisonRemoteWatcher->dataVersion()!=m_qsoComparisonRemoteVersion) {
            m_qsoComparisonTimer.stop(); m_qsoComparisonRemoteWatcher.reset(); m_qsoComparisonSummary.insert(QStringLiteral("stale"),true);
            ++m_qsoReviewGeneration; m_qsoReviewBusy=false; m_qsoReview.clear(); emit qsoReviewChanged(); emit qsoComparisonChanged();
        }
    });
    m_durableRetry.setInterval(2000);
    connect(&m_durableRetry, &QTimer::timeout, this, [this] {
        if (!m_net.running() || !m_ctx.db || m_ctx.db->openGeneration()!=m_durableRetryDbGeneration ||
            m_ctx.db->identity()!=m_durableRetryIdentity || m_ctx.db->openFileIdentity()!=m_durableRetryFileIdentity ||
            !m_ctx.db->fileIdentityMatchesOpen()) {
            m_durableRetry.stop();
            m_durableStatus=tr("Retry stopped: the network or log changed. Reconfigure and retry.");
            emit durableSerialStatusChanged();
            return;
        }
        if (!m_durableBusy) requestDurableSerialImpl(m_durableRetryMinimum,m_durableRetryRequestId);
    });
    QSettings s;
    m_enabled = s.value(QStringLiteral("net/enabled"), false).toBool();
    m_group = s.value(QStringLiteral("net/group"), QStringLiteral("DECODXLOG")).toString();
    m_name = s.value(QStringLiteral("net/station"), QHostInfo::localHostName()).toString();
    m_port = s.value(QStringLiteral("net/port"), 12060).toInt();
    m_id = s.value(QStringLiteral("net/id")).toString();
    if (m_id.isEmpty()) {
        m_id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        s.setValue(QStringLiteral("net/id"), m_id);
    }
    connect(&m_net, &ContestNet::received, this,
            [this](const QString& from, const QString& type, const QJsonObject& body, const QHostAddress& sender) {
                if (m_peers.contains(from))
                    m_peers[from].address = sender.toString();
                onReceived(from, type, body);
            });
    m_hello.setInterval(5000);
    connect(&m_hello, &QTimer::timeout, this, &NetController::hello);
    m_resendTimer.setInterval(60);
    connect(&m_resendTimer, &QTimer::timeout, this, [this] {
        for (int i = 0; i < kResendBatch && !m_resend.isEmpty(); ++i) {
            const QJsonObject qso = qsoMessage(m_resend.takeFirst());
            if (!qso.isEmpty())
                send(QStringLiteral("qso"), qso);
        }
        if (m_resend.isEmpty())
            m_resendTimer.stop();
    });
    if (m_enabled)
        QTimer::singleShot(0, this, &NetController::restart);
    m_status = tr("off");
}

NetController::~NetController()
{
    if (m_qsoComparisonCancel) m_qsoComparisonCancel->store(true);
    m_qsoComparisonPool.clear(); m_qsoComparisonPool.waitForDone();
    m_durablePool.clear();
    m_durablePool.waitForDone();
}

bool NetController::startQsoComparison(const QUrl& remoteFile)
{
    if (m_qsoComparisonBusy || !remoteFile.isLocalFile() || remoteFile.toLocalFile().isEmpty() || !m_ctx.db ||
        !m_ctx.db->isOpen() || m_ctx.db->openFileIdentity().isEmpty() || !m_ctx.db->fileIdentityMatchesOpen()) return false;
    ++m_qsoReviewGeneration; m_qsoReviewBusy=false; m_qsoReview.clear(); emit qsoReviewChanged();
    const QString localPath=m_ctx.db->path(), remotePath=remoteFile.toLocalFile(), identity=m_ctx.db->identity(), stamp=m_ctx.db->changeStamp();
    const QByteArray localFileIdentity=m_ctx.db->openFileIdentity(), remoteFileIdentity=fileidentity::read(remotePath);
    if (identity.isEmpty() || stamp.isEmpty() || remoteFileIdentity.isEmpty()) return false;
    auto watcher=std::make_unique<LogDatabase>();
    if (!watcher->openReadOnly(remotePath) || watcher->openFileIdentity()!=remoteFileIdentity || !watcher->fileIdentityMatchesOpen()) return false;
    const qint64 remoteVersion=watcher->dataVersion();
    if (remoteVersion<0) return false;
    const quint64 dbGeneration=m_ctx.db->openGeneration(), generation=++m_qsoComparisonGeneration;
    auto cancelled=std::make_shared<std::atomic_bool>(false); m_qsoComparisonCancel=cancelled;
    m_qsoComparisonTimer.stop(); m_qsoComparisonRemoteWatcher=std::move(watcher); m_qsoComparisonRemoteVersion=remoteVersion;
    m_qsoComparisonBusy=true; m_qsoComparisonSummary.clear(); m_qsoComparisonRows.clear(); emit qsoComparisonChanged();
    QPointer<NetController> self(this);
    m_qsoComparisonPool.start([self,cancelled,localPath,remotePath,identity,stamp,localFileIdentity,remoteFileIdentity,dbGeneration,generation] {
        ContestNetReconcile comparison; QVariantMap counts{{QStringLiteral("matching"),0},{QStringLiteral("localOnly"),0},{QStringLiteral("remoteOnly"),0},
            {QStringLiteral("revisionDifference"),0},{QStringLiteral("contentConflict"),0},{QStringLiteral("deletionConflict"),0}};
        QVariantList rows; QString error; qint64 localWatermark=0,remoteWatermark=0; QString remoteIdentity;
        if (!comparison.begin(localPath,remotePath)) error=comparison.error();
        else if (comparison.localIdentity()!=identity || comparison.localFileIdentity()!=localFileIdentity || comparison.remoteFileIdentity()!=remoteFileIdentity)
            error=QStringLiteral("A log file changed before comparison started");
        else {
            localWatermark=comparison.localWatermark(); remoteWatermark=comparison.remoteWatermark(); remoteIdentity=comparison.remoteIdentity();
            bool more=true;
            while (more && !cancelled->load()) {
                const auto page=comparison.next(128);
                if (!page.ok) { error=page.error; break; }
                for (const auto& difference:page.rows) {
                    QString kind;
                    switch (difference.kind) {
                    case ContestNetDifference::Kind::MatchingFingerprint: kind=QStringLiteral("matching"); break;
                    case ContestNetDifference::Kind::LocalOnly: kind=QStringLiteral("localOnly"); break;
                    case ContestNetDifference::Kind::RemoteOnly: kind=QStringLiteral("remoteOnly"); break;
                    case ContestNetDifference::Kind::RevisionDifference: kind=QStringLiteral("revisionDifference"); break;
                    case ContestNetDifference::Kind::ContentConflict: kind=QStringLiteral("contentConflict"); break;
                    case ContestNetDifference::Kind::DeletionConflict: kind=QStringLiteral("deletionConflict"); break;
                    }
                    counts[kind]=counts.value(kind).toLongLong()+1;
                    if (kind!=QLatin1String("matching") && rows.size()<200) {
                        QVariantList fields;
                        for (const auto& field:difference.fields)
                            fields.append(QVariantMap{{QStringLiteral("name"),field.name},{QStringLiteral("local"),field.localValue},{QStringLiteral("remote"),field.remoteValue},
                                {QStringLiteral("localPresent"),field.localPresent},{QStringLiteral("remotePresent"),field.remotePresent},{QStringLiteral("nameTruncated"),field.nameTruncated},
                                {QStringLiteral("localTruncated"),field.localTruncated},{QStringLiteral("remoteTruncated"),field.remoteTruncated}});
                        rows.append(QVariantMap{{QStringLiteral("uuid"),difference.uuid},{QStringLiteral("kind"),kind},
                            {QStringLiteral("localCall"),difference.local.call},{QStringLiteral("localBand"),difference.local.band},
                            {QStringLiteral("localMode"),difference.local.mode},{QStringLiteral("localDate"),difference.local.date},
                            {QStringLiteral("localRevision"),difference.local.present?QVariant(difference.local.revision):QVariant()},
                            {QStringLiteral("localDeleted"),difference.local.deleted}, {QStringLiteral("localHash"),QString::fromLatin1(difference.local.contentHash.toHex())},
                            {QStringLiteral("remoteCall"),difference.remote.call},{QStringLiteral("remoteBand"),difference.remote.band},
                            {QStringLiteral("remoteMode"),difference.remote.mode},{QStringLiteral("remoteDate"),difference.remote.date},
                            {QStringLiteral("remoteRevision"),difference.remote.present?QVariant(difference.remote.revision):QVariant()},
                            {QStringLiteral("remoteDeleted"),difference.remote.deleted}, {QStringLiteral("remoteHash"),QString::fromLatin1(difference.remote.contentHash.toHex())},
                            {QStringLiteral("fields"),fields},{QStringLiteral("fieldsOmitted"),difference.fieldsOmitted}});
                    }
                }
                more=page.hasMore;
            }
        }
        if (cancelled->load()) error=QStringLiteral("Comparison cancelled");
        if (!self) return;
        QMetaObject::invokeMethod(self,[self,cancelled,localPath,remotePath,identity,stamp,localFileIdentity,remoteFileIdentity,dbGeneration,generation,counts,rows,error,localWatermark,remoteWatermark,remoteIdentity] {
            if (!self || self->m_qsoComparisonGeneration!=generation) return;
            self->m_qsoComparisonBusy=false; self->m_qsoComparisonCancel.reset();
            if (cancelled->load() || !error.isEmpty()) {
                self->m_qsoComparisonRemoteWatcher.reset();
                self->m_qsoComparisonRows.clear(); self->m_qsoComparisonSummary={{QStringLiteral("error"),cancelled->load()?self->tr("Comparison cancelled"):error}};
            } else if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=localPath ||
                self->m_ctx.db->identity()!=identity || self->m_ctx.db->openGeneration()!=dbGeneration ||
                self->m_ctx.db->openFileIdentity()!=localFileIdentity || !self->m_ctx.db->fileIdentityMatchesOpen() ||
                fileidentity::read(remotePath)!=remoteFileIdentity) {
                self->m_qsoComparisonRemoteWatcher.reset();
                self->m_qsoComparisonRows.clear(); self->m_qsoComparisonSummary={{QStringLiteral("error"),self->tr("A log changed; compare again.")}};
            } else {
                self->m_qsoComparisonPath=localPath; self->m_qsoComparisonIdentity=identity; self->m_qsoComparisonStamp=stamp;
                self->m_qsoComparisonOpenGeneration=dbGeneration; self->m_qsoComparisonFileIdentity=localFileIdentity;
                self->m_qsoComparisonRemotePath=remotePath; self->m_qsoComparisonRemoteFileIdentity=remoteFileIdentity;
                self->m_qsoComparisonRows=rows; self->m_qsoComparisonSummary=counts;
                const qint64 differences=counts.value(QStringLiteral("localOnly")).toLongLong()+counts.value(QStringLiteral("remoteOnly")).toLongLong()+
                    counts.value(QStringLiteral("revisionDifference")).toLongLong()+counts.value(QStringLiteral("contentConflict")).toLongLong()+counts.value(QStringLiteral("deletionConflict")).toLongLong();
                self->m_qsoComparisonSummary.insert(QStringLiteral("differences"),differences);
                self->m_qsoComparisonSummary.insert(QStringLiteral("truncated"),differences>rows.size());
                self->m_qsoComparisonSummary.insert(QStringLiteral("localWatermark"),localWatermark); self->m_qsoComparisonSummary.insert(QStringLiteral("remoteWatermark"),remoteWatermark);
                self->m_qsoComparisonSummary.insert(QStringLiteral("remoteIdentity"),remoteIdentity); self->m_qsoComparisonSummary.insert(QStringLiteral("remotePath"),remotePath);
                const bool stale=self->m_ctx.db->changeStamp()!=stamp || !self->m_qsoComparisonRemoteWatcher ||
                    !self->m_qsoComparisonRemoteWatcher->fileIdentityMatchesOpen() ||
                    self->m_qsoComparisonRemoteWatcher->dataVersion()!=self->m_qsoComparisonRemoteVersion;
                self->m_qsoComparisonSummary.insert(QStringLiteral("stale"),stale);
                if (!stale) self->m_qsoComparisonTimer.start(); else self->m_qsoComparisonRemoteWatcher.reset();
            }
            emit self->qsoComparisonChanged();
        },Qt::QueuedConnection);
    });
    return true;
}

void NetController::cancelQsoComparison()
{
    ++m_qsoReviewGeneration; m_qsoReviewBusy=false; m_qsoReview.clear(); emit qsoReviewChanged();
    if (m_qsoComparisonCancel) m_qsoComparisonCancel->store(true);
    if (!m_qsoComparisonBusy) {
        m_qsoComparisonTimer.stop(); m_qsoComparisonRemoteWatcher.reset(); m_qsoComparisonSummary.clear(); m_qsoComparisonRows.clear();
        emit qsoComparisonChanged();
    }
}

bool NetController::qsoComparisonCurrent() const
{
    return !m_qsoComparisonBusy && m_ctx.db && m_ctx.db->isOpen() && !m_qsoComparisonSummary.isEmpty()
        && !m_qsoComparisonSummary.value(QStringLiteral("stale")).toBool() && m_ctx.db->path()==m_qsoComparisonPath
        && m_ctx.db->identity()==m_qsoComparisonIdentity && m_ctx.db->openGeneration()==m_qsoComparisonOpenGeneration
        && m_ctx.db->openFileIdentity()==m_qsoComparisonFileIdentity && m_ctx.db->fileIdentityMatchesOpen()
        && m_ctx.db->changeStamp()==m_qsoComparisonStamp && fileidentity::read(m_qsoComparisonRemotePath)==m_qsoComparisonRemoteFileIdentity
        && m_qsoComparisonRemoteWatcher && m_qsoComparisonRemoteWatcher->fileIdentityMatchesOpen()
        && m_qsoComparisonRemoteWatcher->dataVersion()==m_qsoComparisonRemoteVersion;
}
bool NetController::reviewQsoComparison(const QString& uuid)
{
    if (m_qsoReviewBusy || uuid.isEmpty() || !qsoComparisonCurrent()) return false;
    QVariantMap selected;
    for (const auto& value:m_qsoComparisonRows) if (value.toMap().value(QStringLiteral("uuid")).toString()==uuid) { selected=value.toMap(); break; }
    if (selected.isEmpty()) return false;
    const QString local=m_qsoComparisonPath, remote=m_qsoComparisonRemotePath, localId=m_qsoComparisonIdentity;
    const QString remoteId=m_qsoComparisonSummary.value(QStringLiteral("remoteIdentity")).toString();
    const QByteArray localFile=m_qsoComparisonFileIdentity, remoteFile=m_qsoComparisonRemoteFileIdentity;
    const quint64 generation=++m_qsoReviewGeneration; m_qsoReviewBusy=true; m_qsoReview.clear(); emit qsoReviewChanged();
    QPointer<NetController> self(this);
    m_qsoComparisonPool.start([self,local,remote,localId,remoteId,localFile,remoteFile,selected,uuid,generation] {
        QVariantMap output; QString error; ContestNetInventory a,b;
        if (!a.begin(local) || !b.begin(remote)) error=QStringLiteral("Cannot open a log snapshot");
        else if (a.logIdentity()!=localId || b.logIdentity()!=remoteId || a.fileIdentity()!=localFile || b.fileIdentity()!=remoteFile) error=QStringLiteral("A log file changed");
        else {
            const auto left=a.findUuid(uuid,4*1024*1024), right=b.findUuid(uuid,4*1024*1024);
            if (!left.ok || !right.ok) error=!left.ok?left.error:right.error;
            else {
                auto matches=[&](const ContestNetInventoryPage& page,const char* hashKey,const char* revKey,const char* deletedKey) {
                    const auto expected=selected.value(QLatin1String(hashKey)).toString();
                    if (expected.isEmpty()) return page.rows.isEmpty();
                    if (page.rows.size()!=1) return false;
                    const auto& row=page.rows.first();
                    return QString::fromLatin1(row.contentHash.toHex())==expected && row.revision==selected.value(QLatin1String(revKey)).toInt()
                        && row.deleted==selected.value(QLatin1String(deletedKey)).toBool();
                };
                if (!matches(left,"localHash","localRevision","localDeleted") || !matches(right,"remoteHash","remoteRevision","remoteDeleted")) error=QStringLiteral("A QSO changed; compare again");
                else {
                    QMap<QString,QString> localFields,remoteFields;
                    auto fill=[](const ContestNetInventoryPage& page,QMap<QString,QString>& fields) {
                        if (page.rows.isEmpty()) return;
                        const auto document=adif::parse(page.rows.first().adifPayload);
                        if (document.records.size()==1) for (const auto& f:document.records.first().fields()) fields.insert(f.name,f.value);
                    };
                    fill(left,localFields); fill(right,remoteFields); QSet<QString> names(localFields.keyBegin(),localFields.keyEnd());
                    for (auto it=remoteFields.cbegin();it!=remoteFields.cend();++it) names.insert(it.key());
                    QStringList ordered=names.values(); ordered.sort(Qt::CaseSensitive); QVariantList fields;
                    for (const auto& name:ordered) fields.append(QVariantMap{{QStringLiteral("name"),name},{QStringLiteral("local"),localFields.value(name)},
                        {QStringLiteral("remote"),remoteFields.value(name)},{QStringLiteral("localPresent"),localFields.contains(name)},{QStringLiteral("remotePresent"),remoteFields.contains(name)}});
                    output={{QStringLiteral("ok"),true},{QStringLiteral("uuid"),uuid},{QStringLiteral("kind"),selected.value(QStringLiteral("kind"))},
                            {QStringLiteral("localDeleted"),selected.value(QStringLiteral("localDeleted"))},{QStringLiteral("remoteDeleted"),selected.value(QStringLiteral("remoteDeleted"))},
                            {QStringLiteral("fields"),fields}};
                    if (!a.fileIdentityCurrent() || !b.fileIdentityCurrent()) { output.clear(); error=QStringLiteral("A log file changed"); }
                }
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self,[self,generation,output,error] {
            if (!self || self->m_qsoReviewGeneration!=generation) return;
            self->m_qsoReviewBusy=false;
            if (!self->qsoComparisonCurrent()) self->m_qsoReview={{QStringLiteral("error"),self->tr("A log changed; compare again.")}};
            else if (!error.isEmpty()) self->m_qsoReview={{QStringLiteral("error"),error}};
            else self->m_qsoReview=output;
            emit self->qsoReviewChanged();
        },Qt::QueuedConnection);
    });
    return true;
}

QString NetController::configureDurableSerialAuthority(const QString& scope, const QString& authority)
{
    const QString s = scope.trimmed();
    const QString a = authority.trimmed();
    if (s.isEmpty() != a.isEmpty() || s.size() > 256 || a.size() > 256)
        return tr("Enter both a contest scope and an authority ID, or leave both empty to disable.");
    ++m_durableGeneration;
    m_durableRetry.stop();
    m_durableRetryRequestId.clear();
    m_durablePool.clear();
    m_durableBusy = false;
    m_durableScope = s;
    m_durableAuthority = a;
    m_durableStatus = s.isEmpty() ? tr("Experimental serials disabled.") : tr("Configured; no request checked yet.");
    emit durableSerialStatusChanged();
    if (m_net.running() && !s.isEmpty()) QTimer::singleShot(0,this,&NetController::resumeDurableSerialRequest);
    return {};
}

QString NetController::requestDurableSerial(int minimum)
{
    return requestDurableSerialImpl(minimum,{});
}

QString NetController::requestDurableSerialImpl(int minimum, const QString& expectedRequestId)
{
    if (minimum < 1) return tr("Enter a positive minimum serial.");
    if (!m_net.running() || m_durableScope.isEmpty() || m_durableAuthority.isEmpty())
        return tr("Enable the network and configure the contest scope and authority first.");
    if (m_durableBusy) return tr("Serial operation in progress; retry shortly.");
    if (!m_ctx.db || !m_ctx.db->isOpen() || !m_ctx.db->fileIdentityMatchesOpen())
        return tr("Open the original file-backed log before requesting a serial.");
    const QString path=m_ctx.db->path(), identity=m_ctx.db->identity();
    const QByteArray fileIdentity=m_ctx.db->openFileIdentity();
    if (fileIdentity.isEmpty()) return tr("A file-backed log is required.");
    const quint64 dbGeneration=m_ctx.db->openGeneration(), configGeneration=m_durableGeneration;
    const QString scope=m_durableScope, authority=m_durableAuthority, requester=m_id;
    const QString requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_durableBusy=true;
    QPointer<NetController> self(this);
    m_durablePool.start([self,path,identity,fileIdentity,dbGeneration,configGeneration,scope,authority,requester,requestId,minimum,expectedRequestId] {
        ContestSerialRequest request;
        LogDatabase db;
        if (db.openExistingReadWrite(path) && db.openFileIdentity()==fileIdentity && db.fileIdentityMatchesOpen() && db.identity()==identity) {
            request=db.activeContestSerialRequest(scope,requester);
            if (!request.ok && request.error==QLatin1String("No active serial request") && expectedRequestId.isEmpty())
                request=db.beginContestSerialRequest(scope,requester,requestId,authority,minimum);
            else if (!request.ok && request.error==QLatin1String("No active serial request") && !expectedRequestId.isEmpty())
                request.error=QStringLiteral("The pending request is no longer active");
            else if (request.ok && !expectedRequestId.isEmpty() && request.requestId!=expectedRequestId) {
                request.ok=false; request.error=QStringLiteral("A different serial request is active");
            }
            else if (request.ok && (request.authority!=authority || request.minimum!=minimum)) {
                request.ok=false; request.error=QStringLiteral("An active request has different authority or minimum");
            }
            if (request.ok && request.state==QLatin1String("pending") && authority==requester) {
                const auto reservation=db.reserveContestSerial(scope,requester,request.requestId,request.minimum);
                if (!reservation.ok) { request.ok=false; request.error=reservation.error; }
                else request=db.acceptContestSerialGrant(scope,requester,request.requestId,authority,reservation.serial);
            }
        } else request.error=QStringLiteral("Cannot open the original log");
        if (!self) return;
        QMetaObject::invokeMethod(self,[self,request,identity,fileIdentity,dbGeneration,configGeneration,expectedRequestId] {
            if (!self || self->m_durableGeneration!=configGeneration) return;
            self->m_durableBusy=false;
            if (!self->m_ctx.db || self->m_ctx.db->openGeneration()!=dbGeneration || self->m_ctx.db->identity()!=identity ||
                self->m_ctx.db->openFileIdentity()!=fileIdentity || !self->m_ctx.db->fileIdentityMatchesOpen()) return;
            if (!request.ok) {
                if (!expectedRequestId.isEmpty() && (request.error==QLatin1String("The pending request is no longer active") || request.error==QLatin1String("A different serial request is active")))
                    self->m_durableRetry.stop();
                self->m_durableStatus=tr("Request failed: %1").arg(request.error);
            }
            else if (request.state==QLatin1String("granted")) {
                if (self->m_durableRetryRequestId==request.requestId) self->m_durableRetry.stop();
                const auto active=self->m_ctx.db->activeContestSerialRequest(request.scope,request.requester);
                if (!active.ok || active.state!=QLatin1String("granted") || active.requestId!=request.requestId ||
                    active.authority!=request.authority || active.serial!=request.serial)
                    self->m_durableStatus=tr("The serial grant changed; review the current request.");
                else self->m_durableStatus=tr("Experimental grant: %1; select it explicitly for a manual QSO.").arg(request.serial);
            }
            else if (self->m_net.running()) {
                const QJsonObject body=contestserial::requestFrame(request);
                if (!body.isEmpty()) {
                    self->send(QStringLiteral("serial_request_v3"),body);
                    self->m_durableStatus=tr("Pending; no serial granted. Retrying while the authority is unavailable.");
                    self->m_durableRetryRequestId=request.requestId;
                    self->m_durableRetryMinimum=request.minimum;
                    self->m_durableRetryIdentity=identity;
                    self->m_durableRetryFileIdentity=fileIdentity;
                    self->m_durableRetryDbGeneration=dbGeneration;
                    self->m_durableRetry.start();
                    emit self->durableSerialRequestSent(body);
                }
            }
            emit self->durableSerialStatusChanged();
        },Qt::QueuedConnection);
    });
    return {};
}

void NetController::resumeDurableSerialRequest()
{
    if (m_durableBusy || !m_net.running() || m_durableScope.isEmpty() || m_durableAuthority.isEmpty() ||
        !m_ctx.db || !m_ctx.db->isOpen() ||
        m_ctx.db->openFileIdentity().isEmpty() || !m_ctx.db->fileIdentityMatchesOpen()) return;
    const QString path=m_ctx.db->path(), identity=m_ctx.db->identity();
    const QByteArray fileIdentity=m_ctx.db->openFileIdentity();
    const quint64 dbGeneration=m_ctx.db->openGeneration(), configGeneration=m_durableGeneration;
    const QString scope=m_durableScope, authority=m_durableAuthority, requester=m_id;
    m_durableBusy=true;
    QPointer<NetController> self(this);
    m_durablePool.start([self,path,identity,fileIdentity,dbGeneration,configGeneration,scope,authority,requester] {
        ContestSerialRequest request; LogDatabase db;
        if (db.openExistingReadWrite(path) && db.openFileIdentity()==fileIdentity && db.fileIdentityMatchesOpen() && db.identity()==identity)
            request=db.activeContestSerialRequest(scope,requester);
        else request.error=QStringLiteral("Cannot open the original log");
        if (!self) return;
        QMetaObject::invokeMethod(self,[self,request,identity,fileIdentity,dbGeneration,configGeneration,authority] {
            if (!self || self->m_durableGeneration!=configGeneration) return;
            self->m_durableBusy=false;
            if (!self->m_net.running() || !self->m_ctx.db || self->m_ctx.db->openGeneration()!=dbGeneration ||
                self->m_ctx.db->identity()!=identity || self->m_ctx.db->openFileIdentity()!=fileIdentity ||
                !self->m_ctx.db->fileIdentityMatchesOpen()) return;
            if (!request.ok) {
                self->m_durableStatus=request.error==QLatin1String("No active serial request") ? tr("No saved serial request for this scope.") : tr("Cannot recover serial request: %1").arg(request.error);
            } else if (request.authority!=authority) {
                self->m_durableStatus=tr("The saved serial request names a different authority; review the configuration.");
            } else if (request.state==QLatin1String("pending")) {
                self->requestDurableSerialImpl(request.minimum,request.requestId); return;
            } else {
                const auto active=self->m_ctx.db->activeContestSerialRequest(request.scope,request.requester);
                if (!active.ok || active.state!=QLatin1String("granted") || active.requestId!=request.requestId ||
                    active.authority!=request.authority || active.serial!=request.serial)
                    self->m_durableStatus=tr("The serial grant changed; review the current request.");
                else self->m_durableStatus=tr("Experimental grant: %1; select it explicitly for a manual QSO.").arg(request.serial);
            }
            emit self->durableSerialStatusChanged();
        },Qt::QueuedConnection);
    });
}

QVariantMap NetController::durableSerialGrantForQso() const
{
    if (m_durableScope.isEmpty() || m_durableAuthority.isEmpty() || !m_ctx.db ||
        !m_ctx.db->isOpen() || m_ctx.db->openFileIdentity().isEmpty() || !m_ctx.db->fileIdentityMatchesOpen()) return {};
    const auto request=m_ctx.db->activeContestSerialRequest(m_durableScope,m_id);
    if (!request.ok || request.state!=QLatin1String("granted") || request.authority!=m_durableAuthority || request.serial<1) return {};
    return {{QStringLiteral("scope"),request.scope},{QStringLiteral("requester"),request.requester},
            {QStringLiteral("requestId"),request.requestId},{QStringLiteral("authority"),request.authority},
            {QStringLiteral("serial"),request.serial}};
}

void NetController::durableSerialQsoSaved()
{
    m_durableRetry.stop();
    m_durableStatus=tr("Experimental grant used; request the next serial.");
    emit durableSerialStatusChanged();
}

void NetController::serveDurableSerialRequest(const QString& from, const QJsonObject& body)
{
    if (m_durableBusy || m_durableScope.isEmpty() || m_durableAuthority != m_id ||
        !m_ctx.db || !m_ctx.db->isOpen() || !m_ctx.db->fileIdentityMatchesOpen())
        return;
    const QString path = m_ctx.db->path();
    const QString identity = m_ctx.db->identity();
    const QByteArray fileIdentity = m_ctx.db->openFileIdentity();
    const quint64 dbGeneration = m_ctx.db->openGeneration();
    const quint64 configGeneration = m_durableGeneration;
    const QString scope = m_durableScope;
    const QString authority = m_id;
    m_durableBusy = true;
    QPointer<NetController> self(this);
    m_durablePool.start([self, path, identity, fileIdentity, dbGeneration, configGeneration, scope, authority, from, body] {
        contestserial::Reply reply;
        LogDatabase db;
        if (db.openExistingReadWrite(path) && db.openFileIdentity() == fileIdentity &&
            db.fileIdentityMatchesOpen() && db.identity() == identity)
            reply = contestserial::handleRequest(db, scope, authority, from, body);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, reply, identity, fileIdentity, dbGeneration, configGeneration] {
            if (!self || self->m_durableGeneration != configGeneration) return;
            self->m_durableBusy = false;
            if (!reply.ok || !self->m_net.running() || !self->m_ctx.db ||
                self->m_ctx.db->openGeneration() != dbGeneration || self->m_ctx.db->identity() != identity ||
                self->m_ctx.db->openFileIdentity() != fileIdentity || !self->m_ctx.db->fileIdentityMatchesOpen())
                return;
            self->send(QStringLiteral("serial_grant_v3"), reply.body);
            emit self->durableSerialGrantReady(reply.body);
        }, Qt::QueuedConnection);
    });
}

void NetController::acceptDurableSerialGrant(const QString& from, const QJsonObject& body)
{
    if (m_durableBusy || m_durableScope.isEmpty() || m_durableAuthority.isEmpty() ||
        from != m_durableAuthority || !m_ctx.db || !m_ctx.db->isOpen() || !m_ctx.db->fileIdentityMatchesOpen())
        return;
    const QString path = m_ctx.db->path();
    const QString identity = m_ctx.db->identity();
    const QByteArray fileIdentity = m_ctx.db->openFileIdentity();
    const quint64 dbGeneration = m_ctx.db->openGeneration();
    const quint64 configGeneration = m_durableGeneration;
    const QString scope = m_durableScope, authority = m_durableAuthority, requester = m_id;
    m_durableBusy = true;
    QPointer<NetController> self(this);
    m_durablePool.start([self, path, identity, fileIdentity, dbGeneration, configGeneration, scope, authority, requester, from, body] {
        ContestSerialRequest grant;
        LogDatabase db;
        if (db.openExistingReadWrite(path) && db.openFileIdentity() == fileIdentity &&
            db.fileIdentityMatchesOpen() && db.identity() == identity)
            grant = contestserial::acceptGrant(db, scope, requester, authority, from, body);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, grant, body, identity, fileIdentity, dbGeneration, configGeneration] {
            if (!self || self->m_durableGeneration != configGeneration) return;
            self->m_durableBusy = false;
            if (!grant.ok || !self->m_ctx.db || self->m_ctx.db->openGeneration() != dbGeneration ||
                self->m_ctx.db->identity() != identity || self->m_ctx.db->openFileIdentity() != fileIdentity ||
                !self->m_ctx.db->fileIdentityMatchesOpen()) return;
            const auto active=self->m_ctx.db->activeContestSerialRequest(grant.scope,grant.requester);
            if (grant.state!=QLatin1String("granted") || !active.ok || active.state!=QLatin1String("granted") ||
                active.requestId!=grant.requestId || active.authority!=grant.authority || active.serial!=grant.serial) return;
            if (self->m_durableRetryRequestId==grant.requestId) self->m_durableRetry.stop();
            self->m_durableStatus = tr("Experimental grant: %1; select it explicitly for a manual QSO.").arg(grant.serial);
            emit self->durableSerialStatusChanged();
            emit self->durableSerialGrantAccepted(body);
        }, Qt::QueuedConnection);
    });
}

void NetController::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    QSettings().setValue(QStringLiteral("net/enabled"), on);
    restart();
    emit settingsChanged();
}

void NetController::setGroup(const QString& group)
{
    const QString g = group.trimmed().toUpper();
    if (g.isEmpty() || g == m_group)
        return;
    m_group = g;
    QSettings().setValue(QStringLiteral("net/group"), g);
    if (m_enabled)
        restart();
    emit settingsChanged();
}

void NetController::setStationName(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n == m_name)
        return;
    m_name = n;
    QSettings().setValue(QStringLiteral("net/station"), n);
    if (m_net.running()) {
        m_status = tr("on the network %1 (UDP %2) as %3").arg(m_group).arg(m_port).arg(m_name);
        hello();
        emit stateChanged();
    }
    emit settingsChanged();
}

void NetController::setPort(int port)
{
    if (port <= 1024 || port > 65535 || port == m_port)
        return;
    m_port = port;
    QSettings().setValue(QStringLiteral("net/port"), port);
    if (m_enabled)
        restart();
    emit settingsChanged();
}

void NetController::restart()
{
    ++m_durableGeneration;
    m_durableRetry.stop();
    m_durableRetryRequestId.clear();
    m_durablePool.clear();
    m_durableBusy = false;
    m_net.stop();
    m_hello.stop();
    m_peers.clear();
    if (!m_enabled) {
        m_status = tr("off");
    } else if (m_net.start(static_cast<quint16>(m_port), m_group, m_id)) {
        m_status = tr("on the network %1 (UDP %2) as %3").arg(m_group).arg(m_port).arg(m_name);
        m_hello.start();
        hello();
        if (!m_durableScope.isEmpty()) QTimer::singleShot(0,this,&NetController::resumeDurableSerialRequest);
        // Chi arriva a gara iniziata si fa mandare quello che manca.
        QTimer::singleShot(1500, this, &NetController::requestSync);
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("NET"), m_status, QStringLiteral("info"));
    } else {
        m_status = tr("cannot open UDP %1: %2").arg(m_port).arg(m_net.lastError());
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("NET"), m_status, QStringLiteral("warning"));
    }
    emit stateChanged();
    emit peersChanged();
}

void NetController::send(const QString& type, QJsonObject body)
{
    if (!m_net.running())
        return;
    body.insert(QStringLiteral("seq"), ++m_seq);
    body.insert(QStringLiteral("session"), m_sessionId);
    body.insert(QStringLiteral("protocol"), 2);
    body.insert(QStringLiteral("name"), m_name);
    m_net.send(type, body);
    ++m_sent;
    emit stateChanged();
}

void NetController::hello()
{
    QVariantMap here = m_ctx.here ? m_ctx.here() : QVariantMap();
    int qsos = 0;
    if (m_ctx.db && m_ctx.sessionStart) {
        const QDateTime start = m_ctx.sessionStart();
        if (start.isValid()) {
            QSqlQuery q(m_ctx.db->connection());
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0 AND qso_datetime_on >= ?"));
            q.addBindValue(start.toString(Qt::ISODate));
            if (q.exec() && q.next())
                qsos = q.value(0).toInt();
        }
    }
    send(QStringLiteral("hello"), QJsonObject{{QStringLiteral("op"), here.value(QStringLiteral("op")).toString()},
                                              {QStringLiteral("band"), here.value(QStringLiteral("band")).toString()},
                                              {QStringLiteral("mode"), here.value(QStringLiteral("mode")).toString()},
                                              {QStringLiteral("khz"), here.value(QStringLiteral("freqKhz")).toDouble()},
                                              {QStringLiteral("qsos"), qsos}});
    // Chi non si fa sentire da mezzo minuto non c'e' piu'.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool gone = false;
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        if (it->seen.secsTo(now) > kPeerTimeoutSecs) {
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("NET"), tr("%1 left the network").arg(it->name), QStringLiteral("warning"));
            it = m_peers.erase(it);
            gone = true;
        } else {
            ++it;
        }
    }
    if (gone)
        emit peersChanged();
}

QVariantList NetController::peers() const
{
    QList<Peer> list = m_peers.values();
    std::sort(list.begin(), list.end(), [](const Peer& a, const Peer& b) { return a.name < b.name; });
    QVariantList out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const Peer& p : list)
        out << QVariantMap{{QStringLiteral("id"), p.id},       {QStringLiteral("name"), p.name},
                           {QStringLiteral("op"), p.op},       {QStringLiteral("band"), p.band},
                           {QStringLiteral("mode"), p.mode},   {QStringLiteral("khz"), p.khz},
                           {QStringLiteral("qsos"), p.qsos},   {QStringLiteral("address"), p.address},
                           {QStringLiteral("age"), int(p.seen.secsTo(now))}};
    return out;
}

QJsonObject NetController::qsoMessage(qint64 id) const
{
    if (!m_ctx.db)
        return {};
    const auto record = m_ctx.db->record(id);
    if (!record)
        return {};
    return QJsonObject{{QStringLiteral("adif"), adif::writeRecord(*record)}};
}

QString NetController::queueCall(const QString& input)
{
    const QString call = input.trimmed().toUpper();
    static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9/?]{1,32}$"));
    if (!pattern.match(call).hasMatch())
        return tr("Enter up to 32 letters, digits, / or ? without spaces.");
    if (m_queuedCalls.contains(call))
        return tr("This callsign is already queued.");
    if (m_queuedCalls.size() >= 100)
        return tr("The callsign queue is full (100 entries).");
    m_queuedCalls.append(call);
    emit queuedCallsChanged();
    return {};
}

bool NetController::moveQueuedCall(int index, int delta)
{
    const int target = index + delta;
    if (index < 0 || index >= m_queuedCalls.size() || (delta != -1 && delta != 1) ||
        target < 0 || target >= m_queuedCalls.size()) return false;
    m_queuedCalls.swapItemsAt(index, target);
    emit queuedCallsChanged();
    return true;
}

bool NetController::removeQueuedCall(int index)
{
    if (index < 0 || index >= m_queuedCalls.size()) return false;
    m_queuedCalls.removeAt(index);
    emit queuedCallsChanged();
    return true;
}

QString NetController::takeQueuedCall()
{
    if (m_queuedCalls.isEmpty()) return {};
    const QString call = m_queuedCalls.takeFirst();
    emit queuedCallsChanged();
    return call;
}

void NetController::qsoLogged(qint64 id)
{
    if (m_ctx.db) {
        if (const auto r = m_ctx.db->record(id))
            noteSerial(r->value(QStringLiteral("STX")));
    }
    if (!m_net.running())
        return;
    const QJsonObject qso = qsoMessage(id);
    if (!qso.isEmpty())
        send(QStringLiteral("qso"), qso);
}

void NetController::noteSerial(const QString& stx)
{
    bool ok = false;
    const int n = stx.trimmed().toInt(&ok);
    if (ok)
        m_serials.note(n);
}

bool NetController::isSerialServer() const
{
    // Il piu' piccolo fra quelli sentiti nell'ultimo minuto.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList present;
    for (const Peer& p : m_peers) {
        if (p.seen.secsTo(now) < 60)
            present << p.id;
    }
    return core::SharedSerials::isServer(m_id, present);
}

void NetController::requestSerial()
{
    if (!m_serialSharing || !m_net.running() || isSerialServer())
        return;
    send(QStringLiteral("serialreq"), QJsonObject{});
}

void NetController::setSerialSharing(bool on)
{
    if (on == m_serialSharing)
        return;
    m_serialSharing = on;
    if (on)
        requestSerial();
}

int NetController::takeSerial(int localNext)
{
    const bool online = m_serialSharing && m_net.running();
    const bool server = online && isSerialServer();
    const int n = m_serials.take(localNext, server, online);
    // Il prossimo si chiede adesso, per averlo pronto al QSO dopo.
    if (online && !server)
        requestSerial();
    return n;
}

void NetController::sendGab(const QString& text, const QString& to)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || !m_net.running())
        return;
    send(QStringLiteral("gab"), QJsonObject{{QStringLiteral("text"), t}, {QStringLiteral("to"), to}});
    addMessage(m_name, t, true, to);
}

void NetController::sendSpot(const QString& call, double khz, const QString& comment)
{
    const QString c = call.trimmed().toUpper();
    if (c.isEmpty() || khz <= 0 || !m_net.running())
        return;
    send(QStringLiteral("spot"), QJsonObject{{QStringLiteral("call"), c}, {QStringLiteral("khz"), khz},
                                             {QStringLiteral("comment"), comment.trimmed()}});
    addMessage(m_name, tr("spot %1 %2 %3").arg(c).arg(khz, 0, 'f', 1).arg(comment.trimmed()), true);
}

void NetController::requestSync()
{
    if (!m_net.running() || !m_ctx.sessionStart)
        return;
    const QDateTime start = m_ctx.sessionStart();
    if (!start.isValid())
        return;
    send(QStringLiteral("syncreq"), QJsonObject{{QStringLiteral("since"), start.toString(Qt::ISODate)}});
}

void NetController::addMessage(const QString& from, const QString& text, bool mine, const QString& to)
{
    m_messages << QVariantMap{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm"))},
                              {QStringLiteral("from"), from},
                              {QStringLiteral("to"), to},
                              {QStringLiteral("text"), text},
                              {QStringLiteral("mine"), mine}};
    while (m_messages.size() > kKeepMessages)
        m_messages.removeFirst();
    emit messagesChanged();
}

void NetController::onReceived(const QString& from, const QString& type, const QJsonObject& body)
{
    // Lo stesso datagramma arriva una volta per ogni scheda di rete.
    const qint64 seq = body.value(QStringLiteral("seq")).toInteger();
    const QString session = body.value(QStringLiteral("session")).toString();
    // I client precedenti alla sessione 2 non inviano il campo: li trattiamo
    // come una sessione legacy, mantenendo la compatibilita' senza farli
    // contaminare le sequenze delle nuove istanze.
    const QString frameSession = session.isEmpty() ? QStringLiteral("legacy") : session;
    QSet<QString>& seen = m_seenSeq[from];
    if (seq > 0) {
        const QString frameId = frameKey(frameSession, seq);
        if (seen.contains(frameId))
            return;
        seen.insert(frameId);
        if (seen.size() > 2000)
            seen.clear();
    }
    ++m_received;
    const QString name = body.value(QStringLiteral("name")).toString(from);
    Peer& p = m_peers[from];
    const bool isNew = p.id.isEmpty();
    p.id = from;
    p.name = name;
    p.seen = QDateTime::currentDateTimeUtc();
    if (isNew && m_ctx.activity)
        m_ctx.activity(QStringLiteral("NET"), tr("%1 joined the network").arg(name), QStringLiteral("info"));

    if (type == QLatin1String("hello")) {
        p.op = body.value(QStringLiteral("op")).toString();
        p.band = body.value(QStringLiteral("band")).toString();
        p.mode = body.value(QStringLiteral("mode")).toString();
        p.khz = body.value(QStringLiteral("khz")).toDouble();
        p.qsos = body.value(QStringLiteral("qsos")).toInt();
    } else if (type == QLatin1String("qso")) {
        const AdifDocument doc = adif::parse(body.value(QStringLiteral("adif")).toString().toUtf8());
        for (const AdifRecord& r : doc.records) {
            bool serialOk = false;
            const int serial = r.value(QStringLiteral("STX")).trimmed().toInt(&serialOk);
            const bool collision = serialOk && m_serials.noteRemote(serial);
            if (collision && m_ctx.activity)
                m_ctx.activity(QStringLiteral("NET"),
                               tr("Serial collision detected: %1 was already assigned locally; verify the contest log")
                                   .arg(serial),
                               QStringLiteral("error"));
            if (m_ctx.insertRemote && m_ctx.insertRemote(r, name))
                ++p.qsos;
        }
    } else if (type == QLatin1String("serial_request_v3")) {
        serveDurableSerialRequest(from, body);
    } else if (type == QLatin1String("serial_grant_v3")) {
        acceptDurableSerialGrant(from, body);
    } else if (type == QLatin1String("serialreq")) {
        if (m_serialSharing && isSerialServer())
            send(QStringLiteral("serialgrant"), QJsonObject{{QStringLiteral("to"), from},
                                                            {QStringLiteral("n"), m_serials.serve()}});
    } else if (type == QLatin1String("serialgrant")) {
        const int n = body.value(QStringLiteral("n")).toInt();
        // Anche quelli dati agli altri sono gia' presi.
        if (body.value(QStringLiteral("to")).toString() == m_id)
            m_serials.reserve(n);
        else
            m_serials.note(n);
    } else if (type == QLatin1String("gab")) {
        const QString to = body.value(QStringLiteral("to")).toString();
        if (to.isEmpty() || to.compare(m_name, Qt::CaseInsensitive) == 0) {
            const QString text = body.value(QStringLiteral("text")).toString();
            addMessage(name, text, false, to);
            emit gabReceived(name, text);
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("NET"), QStringLiteral("%1: %2").arg(name, text), QStringLiteral("highlight"));
        }
    } else if (type == QLatin1String("spot")) {
        const QString call = body.value(QStringLiteral("call")).toString();
        const double khz = body.value(QStringLiteral("khz")).toDouble();
        const QString comment = body.value(QStringLiteral("comment")).toString();
        addMessage(name, tr("spot %1 %2 %3").arg(call).arg(khz, 0, 'f', 1).arg(comment), false);
        if (m_ctx.spot)
            m_ctx.spot(call, khz, comment, name);
    } else if (type == QLatin1String("syncreq") && m_ctx.db) {
        // Si rimandano i QSO della gara fatti qui (non quelli arrivati dalla rete:
        // quelli li rimanda chi li ha fatti).
        const QString since = body.value(QStringLiteral("since")).toString();
        QSqlQuery q(m_ctx.db->connection());
        q.prepare(QStringLiteral("SELECT id FROM qso WHERE deleted = 0 AND qso_datetime_on >= ? AND source <> 'network' "
                                 "ORDER BY qso_datetime_on"));
        q.addBindValue(since);
        if (q.exec()) {
            while (q.next())
                if (!m_resend.contains(q.value(0).toLongLong()))
                    m_resend << q.value(0).toLongLong();
        }
        if (!m_resend.isEmpty())
            m_resendTimer.start();
    }
    emit peersChanged();
    emit stateChanged();
}

QString NetController::frameKey(const QString& session, qint64 seq)
{
    return session + QLatin1Char(':') + QString::number(seq);
}

} // namespace decolog::app
