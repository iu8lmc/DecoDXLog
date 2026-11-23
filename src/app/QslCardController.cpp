#include "../StartupTrace.h"
#include "app/QslCardController.h"

#include "core/CredentialStore.h"
#include "core/LogDatabase.h"
#include "core/QslCards.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <QDir>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QSettings>
#include <QSet>
#include <QUrl>

namespace decolog::app {

using namespace decolog::core;

namespace {

QString today()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd"));
}

QList<qint64> toIds(const QVariantList& list)
{
    QList<qint64> ids;
    ids.reserve(list.size());
    for (const QVariant& v : list) {
        const qint64 id = v.toLongLong();
        if (id > 0)
            ids << id;
    }
    return ids;
}

QList<QVariantMap> chosenCardRows(LogDatabase& db, const QList<qint64>& ids)
{
    if (ids.isEmpty()) return db.cardRows(QStringLiteral("queue"));
    if (ids.size() <= 500) return db.cardRows(QStringLiteral("all"), 0, ids);
    const QSet<qint64> wanted(ids.cbegin(), ids.cend());
    QList<QVariantMap> out;
    for (const QVariantMap& row : db.cardRows(QStringLiteral("all"))) {
        if (wanted.contains(row.value(QStringLiteral("id")).toLongLong())) out << row;
    }
    return out;
}

struct CardBatchResult {
    int changed{0};
    int failed{0};
    int skipped{0};
    QString error;
};

// Runs with a thread-local connection for file-backed logs. Keeping each
// transaction small allows other log operations to obtain the writer lock.
CardBatchResult changeCardBatch(LogDatabase& db, QList<qint64> ids, const QString& sent,
                                const QString& via, bool unanswered, const QString& received,
                                const QString& expectedUuid = {}, bool onlyUnsent = false, int expectedRevision = -1,
                                const QString& expectedIdentity = {})
{
    CardBatchResult result;
    if (unanswered) {
        ids.clear();
        for (const QVariantMap& row : db.cardRows(QStringLiteral("received"))) {
            if (row.value(QStringLiteral("sent")).toString() != QLatin1String("Y"))
                ids << row.value(QStringLiteral("id")).toLongLong();
        }
    }
    if (ids.isEmpty()) return result;
    QSqlDatabase connection = db.connection();
    if (!connection.transaction()) {
        result.failed = int(ids.size()); result.error = connection.lastError().text(); return result;
    }
    if (!expectedIdentity.isEmpty() && db.identity()!=expectedIdentity) {
        connection.rollback(); result.failed=int(ids.size()); result.error=QObject::tr("Log identity changed before the paper QSL operation"); return result;
    }
    QElapsedTimer held; held.start();
    int inBlock = 0;
    auto commit = [&] {
        if (connection.commit()) result.changed += inBlock;
        else {
            result.failed += inBlock;
            result.error = connection.lastError().text();
            connection.rollback();
        }
        inBlock = 0;
    };
    int done = 0;
    for (const qint64 id : std::as_const(ids)) {
        ++done;
        if (!expectedUuid.isEmpty()) {
            const auto meta = db.meta(id);
            if (!meta || meta->deleted || meta->uuid != expectedUuid || (expectedRevision>=0 && meta->revision!=expectedRevision)) {
                ++result.failed;
                result.error = QObject::tr("The original QSO no longer matches the log.");
                connection.rollback();
                result.failed += inBlock + int(ids.size()) - done;
                return result;
            }
        }
        QslState card; card.service = QStringLiteral("card");
        for (const QslState& existing : db.qslStatus(id)) {
            if (existing.service == QLatin1String("card")) { card = existing; break; }
        }
        if (onlyUnsent) {
            const QString wasSent=card.sent.trimmed().toUpper(), wasReceived=card.rcvd.trimmed().toUpper();
            const QString previousVia=card.via.trimmed().toUpper();
            if ((!wasSent.isEmpty() && wasSent!=QLatin1String("N") && wasSent!=QLatin1String("I")) ||
                (!wasReceived.isEmpty() && wasReceived!=QLatin1String("N")) ||
                (!previousVia.isEmpty() && previousVia!=via)) {
                connection.rollback(); ++result.skipped; return result;
            }
        }
        if (!sent.isEmpty()) {
            if (sent == QLatin1String("Y") && card.sent != QLatin1String("Y")) card.sentDate = today();
            card.sent = sent;
        }
        if (!received.isEmpty()) {
            if (received == QLatin1String("Y") && card.rcvd != QLatin1String("Y")) card.rcvdDate = today();
            else if (received == QLatin1String("N")) card.rcvdDate.clear();
            card.rcvd = received;
        }
        if (!via.isEmpty()) card.via = via;
        QSqlQuery row(connection);
        bool unrecoverable = false;
        if (!row.exec(QStringLiteral("SAVEPOINT qsl_card_row"))) {
            ++result.failed;
            if (result.error.isEmpty()) result.error = row.lastError().text();
            unrecoverable = true;
        } else if (!db.setCardState(id, card)) {
            ++result.failed;
            if (result.error.isEmpty()) result.error = db.lastError();
            QSqlQuery rollback(connection);
            const bool rolledBack = rollback.exec(QStringLiteral("ROLLBACK TO SAVEPOINT qsl_card_row"));
            const bool released = rolledBack && rollback.exec(QStringLiteral("RELEASE SAVEPOINT qsl_card_row"));
            if (!released) {
                if (result.error.isEmpty()) result.error = rollback.lastError().text();
                unrecoverable = true;
            }
        } else if (row.exec(QStringLiteral("RELEASE SAVEPOINT qsl_card_row"))) {
            ++inBlock;
        } else {
            ++result.failed;
            if (result.error.isEmpty()) result.error = row.lastError().text();
            unrecoverable = true;
        }
        if (unrecoverable) {
            // A trigger may have rolled back the entire transaction. Never
            // start another savepoint outside it: RELEASE would commit it.
            connection.rollback();
            result.failed += inBlock + int(ids.size()) - done;
            return result;
        }
        if (done % 200 == 0 || held.elapsed() >= 250 || done == ids.size()) {
            commit();
            if (done < ids.size() && !connection.transaction()) {
                result.failed += int(ids.size()) - done;
                if (result.error.isEmpty()) result.error = connection.lastError().text();
                break;
            }
            if (done < ids.size() && !expectedIdentity.isEmpty() && db.identity()!=expectedIdentity) {
                connection.rollback(); result.failed += int(ids.size()) - done;
                result.error=QObject::tr("Log identity changed before the paper QSL operation"); break;
            }
            held.restart();
        }
    }
    return result;
}

} // namespace

QslCardController::QslCardController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    m_cardPool.setMaxThreadCount(1);
    m_readPool.setMaxThreadCount(1);
    m_exportPool.setMaxThreadCount(1);
    loadCard();
    loadMail();

    connect(&m_mail, &core::MailSender::sent, this, [this](qint64 tag, const QString& to) {
        if (!m_mailTargets.contains(tag)) return;
        const MailTarget target = m_mailTargets.take(tag);
        if (target.generation == m_mailGeneration) ++m_mailSent;
        recordConfirmedMail(target.path, target.id, target.uuid, to);
        emit mailChanged();
    });
    connect(&m_mail, &core::MailSender::failed, this,
            [this](qint64 tag, const QString& to, const QString& error) {
                if (!m_mailTargets.contains(tag)) return;
                const MailTarget target = m_mailTargets.take(tag);
                if (target.generation != m_mailGeneration) return;
                ++m_mailFailed;
                note(to.isEmpty() ? error : tr("QSL not sent to %1: %2").arg(to, error),
                     QStringLiteral("error"));
                emit mailChanged();
            });
    connect(&m_mail, &core::MailSender::finished, this, [this] {
        if (m_mailWaiting > 0)
            return;   // altre stanno ancora aspettando l'email dal callbook
        m_mailStatus = m_mailFailed == 0
                           ? tr("%n QSL sent by email", nullptr, m_mailSent)
                           : tr("%1 sent, %2 not").arg(m_mailSent).arg(m_mailFailed);
        emit mailChanged();
    });
}

QVariantList QslCardController::rows(const QString& state, int limit) const
{
    decolog::StartupSpan trace("QslCardController::rows");
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return {};
    if (m_ctx.db->path().isEmpty() || m_ctx.db->path() == QLatin1String(":memory:")) {
        QVariantList rows;
        for (const QVariantMap& row : m_ctx.db->cardRows(state, limit)) rows << row;
        return rows;
    }
    ensureCacheIdentity();
    const QString key = state + QLatin1Char(':') + QString::number(limit);
    const quint64 generation = m_cacheGeneration;
    if (m_rowsVersion.value(key) != generation && m_rowsPending.value(key) != generation) {
        m_rowsPending.insert(key, generation);
        auto* self = const_cast<QslCardController*>(this);
        emit self->cacheLoadingChanged();
        QPointer<QslCardController> weak(self);
        const QString path = m_cachePath;
        const auto latest = m_cacheLatest;
        m_readPool.start([weak, path, state, limit, key, generation, latest] {
            if (latest->load() != generation) return;
            LogDatabase db;
            QVariantList rows;
            const bool ok = db.open(path);
            if (ok) for (const QVariantMap& row : db.cardRows(state, limit)) rows << row;
            const QString error = ok ? QString() : db.lastError();
            if (!weak) return;
            QMetaObject::invokeMethod(weak.data(), [weak, path, key, generation, ok, error, rows = std::move(rows)]() mutable {
                if (!weak || weak->m_cacheGeneration != generation || !weak->m_ctx.db
                    || weak->m_ctx.db->path() != path) return;
                weak->m_rowsPending.remove(key);
                weak->m_rowsVersion.insert(key, generation);
                if (ok) weak->m_cachedRows.insert(key, std::move(rows));
                else weak->note(weak->tr("Could not load paper QSL: %1").arg(error), QStringLiteral("warning"));
                emit weak->cacheLoadingChanged();
                emit weak->changed();
            }, Qt::QueuedConnection);
        });
    }
    return m_cachedRows.value(key);
}

QVariantMap QslCardController::counts() const
{
    decolog::StartupSpan trace("QslCardController::counts");
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return QVariantMap{{QStringLiteral("queue"), 0}, {QStringLiteral("sent"), 0},
                           {QStringLiteral("received"), 0}, {QStringLiteral("unanswered"), 0}};
    if (m_ctx.db->path().isEmpty() || m_ctx.db->path() == QLatin1String(":memory:"))
        return m_ctx.db->cardCounts();
    ensureCacheIdentity();
    const quint64 generation = m_cacheGeneration;
    if (m_countsVersion != generation && m_countsPending != generation) {
        m_countsPending = generation;
        auto* self = const_cast<QslCardController*>(this);
        emit self->cacheLoadingChanged();
        QPointer<QslCardController> weak(self);
        const QString path = m_cachePath;
        const auto latest = m_cacheLatest;
        m_readPool.start([weak, path, generation, latest] {
            if (latest->load() != generation) return;
            LogDatabase db;
            const bool ok = db.open(path);
            const QVariantMap counts = ok ? db.cardCounts() : QVariantMap();
            const QString error = ok ? QString() : db.lastError();
            if (!weak) return;
            QMetaObject::invokeMethod(weak.data(), [weak, path, generation, ok, error, counts] {
                if (!weak || weak->m_cacheGeneration != generation || !weak->m_ctx.db
                    || weak->m_ctx.db->path() != path) return;
                weak->m_countsPending = 0;
                weak->m_countsVersion = generation;
                if (ok) weak->m_cachedCounts = counts;
                else weak->note(weak->tr("Could not count paper QSL: %1").arg(error), QStringLiteral("warning"));
                emit weak->cacheLoadingChanged();
                emit weak->changed();
            }, Qt::QueuedConnection);
        });
    }
    return m_cachedCounts.isEmpty() ? QVariantMap{{QStringLiteral("queue"), 0}, {QStringLiteral("sent"), 0},
                                                   {QStringLiteral("received"), 0}, {QStringLiteral("unanswered"), 0}}
                                    : m_cachedCounts;
}

void QslCardController::ensureCacheIdentity() const
{
    const QString path = m_ctx.db ? m_ctx.db->path() : QString();
    if (path == m_cachePath) return;
    m_cachePath = path;
    ++m_cacheGeneration;
    m_cacheLatest->store(m_cacheGeneration);
    m_cachedRows.clear(); m_rowsVersion.clear(); m_rowsPending.clear();
    m_cachedCounts.clear(); m_countsVersion = m_countsPending = 0;
}

void QslCardController::invalidateCache()
{
    if (m_notifyingLogChange) return;
    ensureCacheIdentity();
    ++m_cacheGeneration;
    m_cacheLatest->store(m_cacheGeneration);
    m_rowsPending.clear(); m_countsPending = 0;
    emit cacheLoadingChanged();
    emit changed();
}

QVariantList QslCardController::sheets() const
{
    QVariantList out;
    for (const qslcard::Sheet& s : qslcard::sheets()) {
        out << QVariantMap{{QStringLiteral("id"), s.id},
                           {QStringLiteral("label"), s.label},
                           {QStringLiteral("perPage"), s.columns * s.rows}};
    }
    return out;
}

void QslCardController::note(const QString& text, const QString& level)
{
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("QSL"), text, level);
}

void QslCardController::touch(qint64 id, const QString& sent, const QString& rcvd, const QString& via)
{
    if (!m_ctx.db)
        return;
    QslState card;
    card.service = QStringLiteral("card");
    // Quello che c'e' gia' resta: una data di invio non si cancella perche' e'
    // arrivata la risposta.
    for (const QslState& existing : m_ctx.db->qslStatus(id)) {
        if (existing.service == QLatin1String("card")) {
            card = existing;
            break;
        }
    }
    if (!sent.isEmpty()) {
        if (sent == QLatin1String("Y") && card.sent != QLatin1String("Y"))
            card.sentDate = today();
        card.sent = sent;
    }
    if (!rcvd.isEmpty()) {
        if (rcvd == QLatin1String("Y") && card.rcvd != QLatin1String("Y"))
            card.rcvdDate = today();
        else if (rcvd == QLatin1String("N"))
            card.rcvdDate.clear();
        card.rcvd = rcvd;
    }
    if (!via.isEmpty())
        card.via = via;
    m_ctx.db->setCardState(id, card);
}

int QslCardController::changeCards(const QList<qint64>& ids, const QString& sent,
                                   const QString& via, bool unanswered, const QString& received,
                                   const QString& expectedUuid, bool onlyUnsent, int expectedRevision)
{
    if (!m_ctx.db || !m_ctx.db->isOpen() || (!unanswered && ids.isEmpty())) return 0;
    if (m_operationBusy) {
        note(tr("A paper QSL operation is already running"), QStringLiteral("warning"));
        return 0;
    }
    const QString path = m_ctx.db->path();
    const QString identity = (path.isEmpty() || path==QLatin1String(":memory:")) ? QString() : m_ctx.db->identity();
    if (!path.isEmpty() && path!=QLatin1String(":memory:") && identity.isEmpty()) {
        m_status=tr("Cannot verify log identity"); note(m_status,QStringLiteral("warning")); emit changed(); return 0;
    }
    auto done = [this, sent, unanswered, received](const CardBatchResult& result) {
        m_operationBusy = false;
        emit operationChanged();
        m_status = !received.isEmpty() ? tr("%n QSL confirmation(s) updated", nullptr, result.changed)
                   : unanswered ? (result.changed ? tr("%n QSL to answer put in the queue", nullptr, result.changed)
                                                 : tr("no QSL waiting for an answer"))
                   : sent == QLatin1String("Q") ? tr("%n QSO in the paper queue", nullptr, result.changed)
                   : sent == QLatin1String("Y") ? tr("%n QSL marked as sent", nullptr, result.changed)
                                                  : tr("%n QSO taken out of the queue", nullptr, result.changed);
        if (result.failed > 0)
            m_status += tr("; %1 failed: %2").arg(result.failed).arg(result.error);
        if (result.skipped > 0)
            m_status += tr("; unchanged: already queued, sent, confirmed or route changed");
        note(m_status, result.failed ? QStringLiteral("warning") : QStringLiteral("info"));
        refresh();
    };
    if (path.isEmpty() || path == QLatin1String(":memory:")) {
        const CardBatchResult result = changeCardBatch(*m_ctx.db, ids, sent, via, unanswered, received,expectedUuid,onlyUnsent,expectedRevision);
        done(result);
        return result.changed;
    }
    m_operationBusy = true;
    emit operationChanged();
    QPointer<QslCardController> self(this);
    m_cardPool.start([self, path, identity, ids, sent, via, unanswered, received, expectedUuid, onlyUnsent, expectedRevision] {
        LogDatabase db;
        CardBatchResult result;
        if (db.openExistingReadWrite(path)) result = changeCardBatch(db, ids, sent, via, unanswered, received,expectedUuid,onlyUnsent,expectedRevision,identity);
        else { result.failed = qMax(1, int(ids.size())); result.error = db.lastError(); }
        QMetaObject::invokeMethod(self.data(), [self, path, sent, unanswered, received, result] {
            if (!self) return;
            self->m_operationBusy = false;
            emit self->operationChanged();
            if (!self->m_ctx.db || self->m_ctx.db->path() != path) {
                self->note(self->tr("Paper QSL operation on the previous log: %1 completed, %2 failed. %3")
                               .arg(result.changed).arg(result.failed).arg(result.error),
                           result.failed ? QStringLiteral("warning") : QStringLiteral("info"));
                return;
            }
            self->m_status = !received.isEmpty() ? self->tr("%n QSL confirmation(s) updated", nullptr, result.changed)
                : unanswered ? (result.changed ? self->tr("%n QSL to answer put in the queue", nullptr, result.changed)
                                                          : self->tr("no QSL waiting for an answer"))
                : sent == QLatin1String("Q") ? self->tr("%n QSO in the paper queue", nullptr, result.changed)
                : sent == QLatin1String("Y") ? self->tr("%n QSL marked as sent", nullptr, result.changed)
                                               : self->tr("%n QSO taken out of the queue", nullptr, result.changed);
            if (result.failed > 0)
                self->m_status += self->tr("; %1 failed: %2").arg(result.failed).arg(result.error);
            if (result.skipped > 0)
                self->m_status += self->tr("; unchanged: already queued, sent, confirmed or route changed");
            self->note(self->m_status, result.failed ? QStringLiteral("warning") : QStringLiteral("info"));
            self->refresh();
        }, Qt::QueuedConnection);
    });
    return -1;
}

void QslCardController::enqueue(const QVariantList& ids, const QString& via)
{
    changeCards(toIds(ids), QStringLiteral("Q"), via);
}

bool QslCardController::enqueueIfUnsent(qint64 id, const QString& uuid, int revision, const QString& via, const QString& expectedLogPath)
{
    const QString channel=via.trimmed().toUpper();
    if (!m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path()!=expectedLogPath || id<=0 || revision<0 || uuid.trimmed().isEmpty() ||
        (channel!=QLatin1String("B") && channel!=QLatin1String("D"))) return false;
    return changeCards({id},QStringLiteral("Q"),channel,false,{},uuid,true,revision)!=0;
}

int QslCardController::enqueueUnanswered(const QString& via)
{
    return changeCards({}, QStringLiteral("Q"), via, true);
}

void QslCardController::markSent(const QVariantList& ids, const QString& via)
{
    changeCards(toIds(ids), QStringLiteral("Y"), via);
}

void QslCardController::markReceived(qint64 id, bool received)
{
    changeCards({id}, {}, {}, false, received ? QStringLiteral("Y") : QStringLiteral("N"));
}

void QslCardController::drop(const QVariantList& ids)
{
    changeCards(toIds(ids), QStringLiteral("N"), QString());
}

QString QslCardController::writeLabels(const QUrl& file, const QString& sheetId, int perLabel, bool guides)
{
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return {};
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!m_ctx.db->path().isEmpty() && m_ctx.db->path() != QLatin1String(":memory:")) {
        startOutput(0, path, {}, sheetId, perLabel, guides);
        return {};
    }
    const QList<QVariantMap> queued = m_ctx.db->cardRows(QStringLiteral("queue"));
    const QList<qslcard::Label> labels = qslcard::group(queued, perLabel);
    const QVariantMap station = m_ctx.station ? m_ctx.station() : QVariantMap{};

    QString error;
    if (!qslcard::writePdf(path, labels, qslcard::sheetById(sheetId), station, guides, &error)) {
        m_status = error;
        note(error, QStringLiteral("error"));
        emit changed();
        return {};
    }
    m_lastFile = path;
    m_status = tr("%n label(s) written", nullptr, static_cast<int>(labels.size()));
    note(tr("QSL labels: %1 (%2)").arg(m_status, path), QStringLiteral("success"));
    emit changed();
    return path;
}

void QslCardController::startOutput(int kind, const QString& path, const QVariantList& selected,
                                    const QString& sheetId, int perPage, bool guides)
{
    if (m_exportBusy || !m_ctx.db || !m_ctx.db->isOpen()) return;
    if (m_operationBusy) {
        note(tr("A paper QSL operation is already running"), QStringLiteral("warning"));
        return;
    }
    const QString dbPath = m_ctx.db->path();
    const QList<qint64> ids = toIds(selected);
    const QVariantMap station = stationInfo();
    const auto card = m_card;
    m_exportBusy = true;
    m_status = tr("Writing paper QSL…");
    emit exportBusyChanged();
    emit changed();
    QPointer<QslCardController> self(this);
    m_exportPool.start([self, kind, path, dbPath, ids, sheetId, perPage, guides, station, card] {
        LogDatabase db;
        bool ok = db.open(dbPath);
        QString error = ok ? QString() : db.lastError();
        QString last = path;
        int count = 0;
        if (ok && kind == 0) {
            const auto labels = qslcard::group(db.cardRows(QStringLiteral("queue")), perPage);
            count = int(labels.size());
            ok = qslcard::writePdf(path, labels, qslcard::sheetById(sheetId), station, guides, &error);
        } else if (ok) {
            const QList<QVariantMap> qsos = chosenCardRows(db, ids);
            count = int(qsos.size());
            if (qsos.isEmpty()) {
                ok = false;
                error = QCoreApplication::translate("QslCardController", "No QSO to make a card for.");
            } else if (kind == 1) {
                ok = qsldesign::writePdf(path, card, qsos, station, perPage, &error);
            } else {
                QStringList written;
                ok = qsldesign::writePng(path, card, qsos, station, &written, &error);
                count = int(written.size());
                if (!written.isEmpty()) last = written.last();
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, kind, path, ok, error, last, count] {
            if (!self) return;
            self->m_exportBusy = false;
            emit self->exportBusyChanged();
            if (!ok) {
                self->m_status = error.isEmpty() ? self->tr("Could not write the paper QSL output") : error;
                self->note(self->m_status, QStringLiteral("error"));
            } else {
                self->m_lastFile = last;
                self->m_status = kind == 0 ? self->tr("%n label(s) written", nullptr, count)
                                           : self->tr("%n card(s) written", nullptr, count);
                self->note(kind == 0 ? self->tr("QSL labels: %1 (%2)").arg(self->m_status, path)
                                     : self->tr("QSL cards: %1 (%2)").arg(self->m_status, QDir::toNativeSeparators(path)),
                           QStringLiteral("success"));
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}


// -- La cartolina ------------------------------------------------------------

namespace {

// Dove sta scritto com'e' fatta la cartolina: una riga sola di JSON nelle
// impostazioni, che cosi' segue il resto nella copia di sicurezza e nel Cloud.
const char* kCardKey = "cards/card";

} // namespace

void QslCardController::loadCard()
{
    const QString json = QSettings().value(QLatin1String(kCardKey)).toString();
    if (!json.isEmpty())
        m_card = qsldesign::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
    measureTemplate();
}

void QslCardController::saveCard()
{
    QSettings().setValue(QLatin1String(kCardKey),
                         QString::fromUtf8(QJsonDocument(qsldesign::toJson(m_card))
                                               .toJson(QJsonDocument::Compact)));
    emit cardChanged();
}

void QslCardController::measureTemplate()
{
    m_cardSize = QSize();
    if (m_card.templatePath.isEmpty())
        return;
    // Solo la misura: leggere tutta l'immagine per sapere quanto e' larga
    // sarebbe uno spreco, e la cartolina puo' essere una scansione grossa.
    QImageReader reader(m_card.templatePath);
    if (reader.canRead())
        m_cardSize = reader.size();
}

QVariantList QslCardController::cardFields() const
{
    QVariantList out;
    for (const qsldesign::Field& f : m_card.fields) {
        out << QVariantMap{
            {QStringLiteral("key"), f.key},
            {QStringLiteral("text"), f.text},
            {QStringLiteral("x"), f.x},
            {QStringLiteral("y"), f.y},
            {QStringLiteral("size"), f.size},
            {QStringLiteral("bold"), f.bold},
            {QStringLiteral("color"), f.color},
            {QStringLiteral("align"), f.align},
        };
    }
    return out;
}

QVariantList QslCardController::cardKeys() const
{
    // L'etichetta e' quella che si legge nel menu: tradotta, la chiave no.
    auto label = [](const QString& key) -> QString {
        if (key == QLatin1String("call"))       return tr("Callsign");
        if (key == QLatin1String("date"))       return tr("Date");
        if (key == QLatin1String("day"))        return tr("Day");
        if (key == QLatin1String("month"))      return tr("Month (number)");
        if (key == QLatin1String("monthName"))  return tr("Month (name)");
        if (key == QLatin1String("year"))       return tr("Year");
        if (key == QLatin1String("time"))       return tr("UTC");
        if (key == QLatin1String("freq"))       return tr("MHz");
        if (key == QLatin1String("band"))       return tr("Band");
        if (key == QLatin1String("mode"))       return tr("Mode");
        if (key == QLatin1String("rst"))        return tr("RST");
        if (key == QLatin1String("name"))       return tr("Name");
        if (key == QLatin1String("qth"))        return tr("QTH");
        if (key == QLatin1String("country"))    return tr("Country");
        if (key == QLatin1String("grid"))       return tr("Grid");
        if (key == QLatin1String("via"))        return tr("Via");
        if (key == QLatin1String("myCall"))     return tr("My callsign");
        if (key == QLatin1String("myGrid"))     return tr("My grid");
        if (key == QLatin1String("myName"))     return tr("My name");
        if (key == QLatin1String("myQth"))      return tr("My QTH");
        if (key == QLatin1String("text"))       return tr("Free text");
        return key;
    };
    QVariantList out;
    for (const QString& key : qsldesign::keys())
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label(key)}};
    return out;
}

void QslCardController::setCardTemplate(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path == m_card.templatePath)
        return;
    m_card.templatePath = path;
    measureTemplate();
    if (!path.isEmpty() && !m_cardSize.isValid()) {
        // Un file che non e' un'immagine non diventa una cartolina: meglio
        // dirlo subito che stampare cinquanta fogli bianchi.
        m_status = tr("This file is not an image DecoDXLog can read.");
        note(m_status, QStringLiteral("warning"));
        m_card.templatePath.clear();
        emit changed();
    } else if (!path.isEmpty()) {
        note(tr("QSL card model: %1 (%2 x %3)")
                 .arg(QDir::toNativeSeparators(path))
                 .arg(m_cardSize.width()).arg(m_cardSize.height()),
             QStringLiteral("info"));
    }
    saveCard();
}

void QslCardController::addCardField(const QString& key)
{
    if (!qsldesign::keys().contains(key))
        return;
    qsldesign::Field field;
    field.key = key;
    // In mezzo, che e' dove si vede: poi lo si trascina dove serve.
    field.x = 0.5;
    field.y = 0.5;
    if (key == QLatin1String("text"))
        field.text = tr("text");
    m_card.fields << field;
    saveCard();
}

namespace {

// I riquadri della QSL classica: la fascia "Confirming QSO/SWL to" e la tabella
// DAY MONTH YEAR UTC MHZ MODE RST sotto. Le misure non sono a occhio: sono
// prese dalla cartolina di IU8LMC (1920 x 1080) trovando le righe e le colonne
// della tabella, e poi ridotte a frazione, che vale per qualunque misura.
//
// Su una cartolina fatta in un altro modo finiscono nel posto sbagliato: allora
// si trascinano, che e' il mestiere di questo pannello.
struct Slot {
    const char* key;
    double x;
    double y;
};

const Slot kStandardSlots[] = {
    {"call",  0.8237, 0.4907},   // dopo "Confirming QSO/SWL to:"
    {"day",   0.0721, 0.7065},
    {"month", 0.2156, 0.7065},
    {"year",  0.3471, 0.7065},
    {"time",  0.4708, 0.7065},
    {"freq",  0.6081, 0.7065},
    {"mode",  0.7539, 0.7065},
    {"rst",   0.9128, 0.7065},
};

// Il corpo: sulla cartolina di prova le scritte della tabella sono alte una
// cinquantina di pixel su 1080.
constexpr int kStandardSize = 46;

} // namespace

void QslCardController::addStandardCardFields()
{
    for (const Slot& slot : kStandardSlots) {
        const QString key = QString::fromLatin1(slot.key);
        auto found = std::find_if(m_card.fields.begin(), m_card.fields.end(),
                                  [&key](const qsldesign::Field& f) { return f.key == key; });
        qsldesign::Field& field = found != m_card.fields.end()
                                      ? *found
                                      : (m_card.fields.append(qsldesign::Field{}), m_card.fields.last());
        field.key = key;
        field.x = slot.x;
        field.y = slot.y;
        field.size = kStandardSize;
        field.bold = true;
        // In mezzo al riquadro: e' li' che sta bene un dato dentro una casella.
        field.align = QStringLiteral("center");
    }
    saveCard();
    note(tr("The usual fields are on the card: drag any that do not fall in the right box."),
         QStringLiteral("info"));
}

void QslCardController::moveCardField(int index, double x, double y)
{
    if (index < 0 || index >= m_card.fields.size())
        return;
    m_card.fields[index].x = qBound(0.0, x, 1.0);
    m_card.fields[index].y = qBound(0.0, y, 1.0);
    saveCard();
}

void QslCardController::updateCardField(int index, const QVariantMap& props)
{
    if (index < 0 || index >= m_card.fields.size())
        return;
    qsldesign::Field& f = m_card.fields[index];
    if (props.contains(QStringLiteral("size")))
        f.size = qBound(5, props.value(QStringLiteral("size")).toInt(), 400);
    if (props.contains(QStringLiteral("bold")))
        f.bold = props.value(QStringLiteral("bold")).toBool();
    if (props.contains(QStringLiteral("color")))
        f.color = props.value(QStringLiteral("color")).toString();
    if (props.contains(QStringLiteral("align")))
        f.align = props.value(QStringLiteral("align")).toString();
    if (props.contains(QStringLiteral("text")))
        f.text = props.value(QStringLiteral("text")).toString();
    saveCard();
}

void QslCardController::removeCardField(int index)
{
    if (index < 0 || index >= m_card.fields.size())
        return;
    m_card.fields.removeAt(index);
    saveCard();
}

QVariantMap QslCardController::stationInfo() const
{
    return m_ctx.station ? m_ctx.station() : QVariantMap{};
}

QVariantMap QslCardController::sampleQso() const
{
    if (m_ctx.db && m_ctx.db->isOpen()) {
        const QList<QVariantMap> queued = m_ctx.db->cardRows(QStringLiteral("queue"), 1);
        if (!queued.isEmpty())
            return queued.first();
    }
    // Niente in coda: una cartolina finta, ma con dati che sembrano veri, se no
    // non si capisce dove si stanno mettendo i campi.
    return QVariantMap{
        {QStringLiteral("call"), QStringLiteral("DL9ZZT")},
        {QStringLiteral("date"), QStringLiteral("2026-03-21")},
        {QStringLiteral("time"), QStringLiteral("1432")},
        {QStringLiteral("band"), QStringLiteral("20m")},
        {QStringLiteral("mode"), QStringLiteral("FT8")},
        {QStringLiteral("freq"), QStringLiteral("14.074")},
        {QStringLiteral("rst"), QStringLiteral("599")},
        {QStringLiteral("name"), QStringLiteral("Klaus")},
        {QStringLiteral("country"), QStringLiteral("Germany")},
    };
}

QList<QVariantMap> QslCardController::chosenQsos(const QVariantList& ids) const
{
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return {};
    return chosenCardRows(*m_ctx.db, toIds(ids));
}

QString QslCardController::writeCardsPdf(const QUrl& file, const QVariantList& ids, int perPage)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (m_ctx.db && m_ctx.db->isOpen() && !m_ctx.db->path().isEmpty()
        && m_ctx.db->path() != QLatin1String(":memory:")) {
        startOutput(1, path, ids, {}, perPage, false);
        return {};
    }
    const QList<QVariantMap> qsos = chosenQsos(ids);
    if (qsos.isEmpty()) {
        m_status = tr("No QSO to make a card for.");
        note(m_status, QStringLiteral("warning"));
        emit changed();
        return {};
    }
    QString error;
    if (!qsldesign::writePdf(path, m_card, qsos, stationInfo(), perPage, &error)) {
        m_status = error;
        note(error, QStringLiteral("error"));
        emit changed();
        return {};
    }
    m_lastFile = path;
    m_status = tr("%n card(s) written", nullptr, static_cast<int>(qsos.size()));
    note(tr("QSL cards: %1 (%2)").arg(m_status, QDir::toNativeSeparators(path)), QStringLiteral("success"));
    emit changed();
    return path;
}

QString QslCardController::writeCardsPng(const QUrl& folder, const QVariantList& ids)
{
    const QString dir = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (m_ctx.db && m_ctx.db->isOpen() && !m_ctx.db->path().isEmpty()
        && m_ctx.db->path() != QLatin1String(":memory:")) {
        startOutput(2, dir, ids, {}, 0, false);
        return {};
    }
    const QList<QVariantMap> qsos = chosenQsos(ids);
    if (qsos.isEmpty()) {
        m_status = tr("No QSO to make a card for.");
        note(m_status, QStringLiteral("warning"));
        emit changed();
        return {};
    }
    QStringList written;
    QString error;
    if (!qsldesign::writePng(dir, m_card, qsos, stationInfo(), &written, &error)) {
        m_status = error;
        note(error, QStringLiteral("error"));
        emit changed();
        return {};
    }
    m_lastFile = written.isEmpty() ? dir : written.last();
    m_status = tr("%n card(s) written", nullptr, static_cast<int>(written.size()));
    note(tr("QSL cards: %1 (%2)").arg(m_status, QDir::toNativeSeparators(dir)), QStringLiteral("success"));
    emit changed();
    return dir;
}

// ── Mandare le cartoline per email ──────────────────────────────────────────

namespace {

// Quello che si scrive nell'email quando non lo si e' scritto nelle
// impostazioni. I segnaposto sono gli stessi che si vedono nel riquadro.
const char* kDefaultSubject = "QSL {MYCALL} -> {CALL}";
const char* kDefaultBody =
    "Dear {NAME},\n"
    "\n"
    "thank you for the QSO on {DATE} at {TIME} UTC, {BAND} {MODE}, RST {RST}.\n"
    "My QSL card is attached. A QSL from you would be very welcome.\n"
    "\n"
    "73 de {MYCALL}\n";

QString fill(const QString& text, const QVariantMap& qso, const QVariantMap& station)
{
    QString out = text;
    auto put = [&out](const char* key, const QString& value) {
        out.replace(QLatin1String(key), value);
    };
    put("{CALL}", qso.value(QStringLiteral("call")).toString());
    put("{NAME}", qso.value(QStringLiteral("name")).toString().isEmpty()
                      ? qso.value(QStringLiteral("call")).toString()
                      : qso.value(QStringLiteral("name")).toString());
    put("{DATE}", qso.value(QStringLiteral("date")).toString());
    const QString time = qso.value(QStringLiteral("time")).toString();
    put("{TIME}", time.size() == 4 ? time.left(2) + QLatin1Char(':') + time.mid(2) : time);
    put("{BAND}", qso.value(QStringLiteral("band")).toString());
    put("{MODE}", qso.value(QStringLiteral("mode")).toString());
    put("{RST}", qso.value(QStringLiteral("rst")).toString());
    put("{MYCALL}", station.value(QStringLiteral("call")).toString());
    put("{MYNAME}", station.value(QStringLiteral("name")).toString());
    return out;
}

} // namespace

void QslCardController::loadMail()
{
    QSettings s;
    core::MailAccount account;
    account.host = s.value(QStringLiteral("mail/host"), QStringLiteral("smtp.gmail.com")).toString();
    account.port = s.value(QStringLiteral("mail/port"), 587).toInt();
    account.fromName = s.value(QStringLiteral("mail/fromName")).toString();
    // L'indirizzo e' quello del portachiavi: e' lo stesso con cui ci si
    // autentica, e tenerne due sarebbe un modo per sbagliarli.
    if (m_ctx.credentials)
        account.user = m_ctx.credentials->account(QStringLiteral("mail"));
    m_mail.setAccount(account);
}

QVariantMap QslCardController::mail() const
{
    const core::MailAccount a = m_mail.account();
    QSettings s;
    return QVariantMap{
        {QStringLiteral("host"), a.host},
        {QStringLiteral("port"), a.port},
        {QStringLiteral("address"), a.user},
        {QStringLiteral("fromName"), a.fromName},
        {QStringLiteral("subject"), s.value(QStringLiteral("mail/subject"),
                                            QLatin1String(kDefaultSubject)).toString()},
        {QStringLiteral("body"), s.value(QStringLiteral("mail/body"),
                                         QLatin1String(kDefaultBody)).toString()},
        {QStringLiteral("route"), mailRoute()},
        {QStringLiteral("mailboxReady"), !a.host.trimmed().isEmpty() && !a.user.trimmed().isEmpty()},
        {QStringLiteral("cloudReady"), m_ctx.cloudAccess && !m_ctx.cloudAccess().second.isEmpty()},
        {QStringLiteral("ready"), mailRoute() == QLatin1String("cloud")
                                     ? (m_ctx.cloudAccess && !m_ctx.cloudAccess().second.isEmpty())
                                     : (!a.host.trimmed().isEmpty() && !a.user.trimmed().isEmpty())},
    };
}

bool QslCardController::mailBusy() const
{
    return m_mailPreparing || m_mailWaiting > 0 || m_mail.busy() || m_mailPendingRecords > 0;
}

void QslCardController::setMail(const QVariantMap& settings)
{
    QSettings s;
    if (settings.contains(QStringLiteral("host")))
        s.setValue(QStringLiteral("mail/host"), settings.value(QStringLiteral("host")).toString().trimmed());
    if (settings.contains(QStringLiteral("port")))
        s.setValue(QStringLiteral("mail/port"), qBound(1, settings.value(QStringLiteral("port")).toInt(), 65535));
    if (settings.contains(QStringLiteral("fromName")))
        s.setValue(QStringLiteral("mail/fromName"), settings.value(QStringLiteral("fromName")).toString());
    if (settings.contains(QStringLiteral("subject")))
        s.setValue(QStringLiteral("mail/subject"), settings.value(QStringLiteral("subject")).toString());
    if (settings.contains(QStringLiteral("body")))
        s.setValue(QStringLiteral("mail/body"), settings.value(QStringLiteral("body")).toString());
    loadMail();
    emit mailChanged();
}

QString QslCardController::mailBodyFor(const QVariantMap& qso) const
{
    return fill(QSettings().value(QStringLiteral("mail/body"), QLatin1String(kDefaultBody)).toString(),
                qso, stationInfo());
}

QByteArray QslCardController::drawCard(const QVariantMap& qso) const
{
    // La cartolina si disegna in memoria: non serve lasciare file in giro per
    // mandarne uno.
    const QImage background(m_card.templatePath);
    const QSize size = background.isNull() ? QSize(1748, 1240) : background.size();
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        core::qsldesign::paint(painter, QRectF(QPointF(0, 0), QSizeF(size)), background,
                               m_card, qso, stationInfo());
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

QString QslCardController::mailRoute() const
{
    return QSettings().value(QStringLiteral("mail/route"), QStringLiteral("cloud")).toString();
}

void QslCardController::setMailRoute(const QString& route)
{
    QSettings().setValue(QStringLiteral("mail/route"),
                         route == QLatin1String("mailbox") ? route : QStringLiteral("cloud"));
    emit mailChanged();
}

bool QslCardController::mailOriginIsCurrent() const
{
    return m_ctx.db && m_ctx.db->isOpen() && m_ctx.db->path() == m_mailOriginPath;
}

void QslCardController::recordConfirmedMail(const QString& path, qint64 id,
                                            const QString& uuid, const QString& email)
{
    ++m_mailPendingRecords;
    emit mailChanged();
    const auto complete = [this, path, email](const CardBatchResult& result) {
        --m_mailPendingRecords;
        if (result.changed == 1 && result.failed == 0) {
            note(tr("QSL sent to %1").arg(email), QStringLiteral("success"));
            if (m_ctx.db && m_ctx.db->isOpen() && m_ctx.db->path() == path) refresh();
        } else {
            const QString detail = result.error.isEmpty() ? tr("The original log is unavailable.") : result.error;
            note(tr("QSL sent to %1, but its sent status could not be recorded: %2")
                     .arg(email, detail), QStringLiteral("warning"));
        }
        emit mailChanged();
    };
    if (uuid.isEmpty()) {
        CardBatchResult result; result.failed = 1;
        result.error = tr("The original QSO identity is missing.");
        complete(result);
        return;
    }
    if (path.isEmpty() || path == QLatin1String(":memory:")) {
        CardBatchResult result;
        if (mailOriginIsCurrent())
            result = changeCardBatch(*m_ctx.db, {id}, QStringLiteral("Y"), QStringLiteral("E"), false, {}, uuid);
        else { result.failed = 1; result.error = tr("The original log is unavailable."); }
        complete(result);
        return;
    }
    QPointer<QslCardController> self(this);
    m_cardPool.start([self, path, id, uuid, complete] {
        CardBatchResult result;
        LogDatabase db;
        if (!QFileInfo::exists(path)) {
            result.failed = 1; result.error = QObject::tr("The original log is unavailable.");
        } else if (!db.open(path)) {
            result.failed = 1; result.error = db.lastError();
        } else {
            result = changeCardBatch(db, {id}, QStringLiteral("Y"), QStringLiteral("E"), false, {}, uuid);
        }
        if (self) QMetaObject::invokeMethod(self.data(), [self, complete, result] {
            if (self) complete(result);
        }, Qt::QueuedConnection);
    });
}

// Il Cloud fa da ponte: la cartolina va li', e la casella ce l'ha il server.
// Cosi' sul computer dell'operatore non c'e' nessuna password di posta.
void QslCardController::sendThroughCloud(const QVariantMap& qso, const QString& email,
                                         const QByteArray& png)
{
    const auto access = m_ctx.cloudAccess ? m_ctx.cloudAccess() : QPair<QString, QString>{};
    const qint64 id = qso.value(QStringLiteral("id")).toLongLong();
    const QString call = qso.value(QStringLiteral("call")).toString().toUpper();
    if (access.first.isEmpty() || access.second.isEmpty()) {
        ++m_mailFailed;
        note(tr("%1: the Cloud is not linked, so there is nowhere to send from").arg(call),
             QStringLiteral("error"));
        emit mailChanged();
        return;
    }

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QUrl url(access.first);
    url.setPath(QStringLiteral("/v1/qsl/mail"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + access.second.toUtf8());

    const QJsonObject body{
        {QStringLiteral("to"), email},
        {QStringLiteral("subject"), fill(QSettings().value(QStringLiteral("mail/subject"),
                                                           QLatin1String(kDefaultSubject)).toString(),
                                         qso, stationInfo())},
        {QStringLiteral("body"), mailBodyFor(qso)},
        {QStringLiteral("replyTo"), m_ctx.replyTo ? m_ctx.replyTo() : QString()},
        {QStringLiteral("attachmentName"),
         QStringLiteral("%1-%2.png").arg(call, qso.value(QStringLiteral("date")).toString()
                                                   .remove(QLatin1Char('-')))},
        {QStringLiteral("attachment"), QString::fromLatin1(png.toBase64())},
    };

    ++m_mailWaiting;
    const quint64 generation = m_mailGeneration;
    const QString originPath = m_mailOriginPath;
    const QString uuid = qso.value(QStringLiteral("uuid")).toString();
    QNetworkReply* reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, email, generation, originPath, uuid] {
        reply->deleteLater();
        // Fermato a meta': i conti sono gia' azzerati, non si tocca piu' niente.
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (generation != m_mailGeneration) {
            // A confirmed response may already have been delivered when the
            // user pressed Stop. Preserve the real sent state without touching
            // counters of any newer batch.
            if (code == 200) {
                recordConfirmedMail(originPath, id, uuid, email);
            }
            return;
        }
        --m_mailWaiting;
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        if (code == 200) {
            ++m_mailSent;
            recordConfirmedMail(originPath, id, uuid, email);
        } else {
            ++m_mailFailed;
            const QString detail = answer.value(QStringLiteral("detail")).toString();
            note(tr("QSL not sent to %1: %2")
                     .arg(email, detail.isEmpty() ? reply->errorString() : detail),
                 QStringLiteral("error"));
        }
        if (m_mailWaiting == 0) {
            m_mailStatus = m_mailFailed == 0
                               ? tr("%n QSL sent by email", nullptr, m_mailSent)
                               : tr("%1 sent, %2 not").arg(m_mailSent).arg(m_mailFailed);
        }
        emit mailChanged();
    });
}

void QslCardController::queueCard(const QVariantMap& qso, const QString& email)
{
    const QByteArray png = drawCard(qso);
    if (mailRoute() == QLatin1String("cloud")) {
        sendThroughCloud(qso, email, png);
        return;
    }

    const QString call = qso.value(QStringLiteral("call")).toString().toUpper();
    core::MailMessage message;
    message.tag = ++m_mailNextTag;
    m_mailTargets.insert(message.tag, MailTarget{m_mailOriginPath,
        qso.value(QStringLiteral("id")).toLongLong(),
        qso.value(QStringLiteral("uuid")).toString(), m_mailGeneration});
    message.to = email;
    message.subject = fill(QSettings().value(QStringLiteral("mail/subject"),
                                             QLatin1String(kDefaultSubject)).toString(),
                           qso, stationInfo());
    message.body = mailBodyFor(qso);
    message.attachmentName = QStringLiteral("%1-%2.png")
                                 .arg(call, qso.value(QStringLiteral("date")).toString()
                                                .remove(QLatin1Char('-')));
    message.attachment = png;
    m_mail.send(message);
}

void QslCardController::sendCardsByEmail(const QVariantList& ids)
{
    if (mailBusy()) return;
    if (m_operationBusy) {
        m_mailStatus = tr("A paper QSL operation is already running");
        emit mailChanged();
        return;
    }
    if (!mail().value(QStringLiteral("ready")).toBool()) {
        m_mailStatus = mailRoute() == QLatin1String("cloud")
                           ? tr("Link the Cloud first, or send from your own mailbox: "
                                "Setup -> QSL services.")
                           : tr("Set up the outgoing mailbox first: Setup -> QSL services.");
        note(m_mailStatus, QStringLiteral("warning"));
        emit mailChanged();
        return;
    }
    if (m_card.fields.isEmpty()) {
        m_mailStatus = tr("The card has no fields yet: nothing to send.");
        note(m_mailStatus, QStringLiteral("warning"));
        emit mailChanged();
        return;
    }
    if (!m_ctx.emailFor) {
        m_mailStatus = tr("Sending by email is not available.");
        emit mailChanged();
        return;
    }
    const quint64 generation = ++m_mailGeneration;
    if (!m_ctx.db || !m_ctx.db->isOpen()) {
        m_mailStatus = tr("No QSO to send a card to.");
        emit mailChanged();
        return;
    }
    const QString path = m_ctx.db->path();
    m_mailOriginPath = path;
    if (path.isEmpty() || path == QLatin1String(":memory:")) {
        beginMail(chosenQsos(ids), generation);
        return;
    }
    m_mailPreparing = true;
    m_mailStatus = tr("Loading QSO for email…");
    emit mailChanged();
    QPointer<QslCardController> self(this);
    const QList<qint64> selected = toIds(ids);
    m_readPool.start([self, path, selected, generation] {
        LogDatabase db;
        const bool ok = db.open(path);
        const QList<QVariantMap> rows = ok ? chosenCardRows(db, selected) : QList<QVariantMap>();
        const QString error = ok ? QString() : db.lastError();
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, path, generation, ok, error, rows] {
            if (!self || self->m_mailGeneration != generation) return;
            self->m_mailPreparing = false;
            if (!ok || !self->m_ctx.db || self->m_ctx.db->path() != path) {
                self->m_mailStatus = ok ? self->tr("The log changed before email preparation completed.") : error;
                self->note(self->m_mailStatus, QStringLiteral("warning"));
                emit self->mailChanged();
                return;
            }
            self->beginMail(rows, generation);
        }, Qt::QueuedConnection);
    });
}

void QslCardController::beginMail(const QList<QVariantMap>& qsos, quint64 generation)
{
    if (generation != m_mailGeneration) return;
    if (!mailOriginIsCurrent()) {
        m_mailStatus = tr("The log changed before sending; the remaining QSLs were stopped.");
        note(m_mailStatus, QStringLiteral("warning"));
        emit mailChanged();
        return;
    }
    if (qsos.isEmpty()) {
        m_mailStatus = tr("No QSO to send a card to.");
        note(m_mailStatus, QStringLiteral("warning"));
        emit mailChanged();
        return;
    }
    if (!mail().value(QStringLiteral("ready")).toBool() || m_card.fields.isEmpty() || !m_ctx.emailFor) {
        m_mailStatus = tr("The email or card settings changed; review them before sending.");
        emit mailChanged();
        return;
    }
    m_mailSent = 0;
    m_mailFailed = 0;
    m_mailWaiting = static_cast<int>(qsos.size());
    m_mailStatus = tr("Looking up %n address(es)…", nullptr, static_cast<int>(qsos.size()));
    emit mailChanged();

    // Dal Cloud la password non serve: ce l'ha il server. Si parte e basta.
    if (mailRoute() == QLatin1String("cloud")) {
        lookupAndSend(qsos, generation);
        return;
    }
    // Dalla propria casella invece serve, e sta nel portachiavi: si chiede una
    // volta sola, e solo adesso che serve davvero.
    if (!m_ctx.credentials) {
        m_mailWaiting = 0;
        m_mailStatus = tr("Sending by email is not available.");
        emit mailChanged();
        return;
    }
    QPointer<QslCardController> self(this);
    m_ctx.credentials->readSecret(QStringLiteral("mail"), [self, qsos, generation](const QString& secret,
                                                                      const QString& error) {
        if (!self || self->m_mailGeneration != generation) return;
        if (secret.isEmpty()) {
            self->m_mailWaiting = 0;
            self->m_mailStatus = error.isEmpty()
                               ? self->tr("No password for the outgoing mailbox.")
                               : error;
            self->note(self->m_mailStatus, QStringLiteral("error"));
            emit self->mailChanged();
            return;
        }
        core::MailAccount account = self->m_mail.account();
        account.password = secret;
        self->m_mail.setAccount(account);
        self->lookupAndSend(qsos, generation);
    });
}

void QslCardController::lookupAndSend(const QList<QVariantMap>& qsos, quint64 generation)
{
    // Callbook hits can answer synchronously and render a card immediately.
    // Yield between small groups so a large selection cannot monopolize the
    // event loop and Stop can prevent further lookup/send requests.
    const auto rows = std::make_shared<QList<QVariantMap>>(qsos);
    const auto next = std::make_shared<qsizetype>(0);
    const auto pump = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakPump = pump;
    QPointer<QslCardController> self(this);
    *pump = [self, rows, next, weakPump, generation] {
        if (!self || self->m_mailGeneration != generation) return;
        if (!self->mailOriginIsCurrent()) {
            self->cancelMail();
            self->m_mailStatus = self->tr("The log changed; the remaining QSLs were stopped.");
            self->note(self->m_mailStatus, QStringLiteral("warning"));
            emit self->mailChanged();
            return;
        }
        QElapsedTimer slice; slice.start();
        int started = 0;
        while (self && self->m_mailGeneration == generation && *next < rows->size()
               && started < 8 && (started == 0 || slice.elapsed() < 10)) {
            const QVariantMap qso = rows->at((*next)++);
            const QString call = qso.value(QStringLiteral("call")).toString();
            ++started;
            self->m_ctx.emailFor(call, [self, qso, call, generation](const QString& email, const QString& why) {
                if (!self || self->m_mailGeneration != generation) return;
                if (!self->mailOriginIsCurrent()) {
                    self->cancelMail();
                    self->m_mailStatus = self->tr("The log changed; the remaining QSLs were stopped.");
                    self->note(self->m_mailStatus, QStringLiteral("warning"));
                    emit self->mailChanged();
                    return;
                }
                --self->m_mailWaiting;
                if (email.isEmpty()) {
                    ++self->m_mailFailed;
                    self->note(self->tr("%1: no email in the callbook (%2)").arg(call, why),
                               QStringLiteral("warning"));
                    emit self->mailChanged();
                } else {
                    self->queueCard(qso, email);
                }
                if (self->m_mailWaiting == 0 && !self->m_mail.busy()) {
                    self->m_mailStatus = self->tr("%1 sent, %2 not").arg(self->m_mailSent).arg(self->m_mailFailed);
                    emit self->mailChanged();
                }
            });
        }
        if (self && self->m_mailGeneration == generation && *next < rows->size()) {
            if (const auto continuation = weakPump.lock())
                QTimer::singleShot(0, self.data(), [continuation] { (*continuation)(); });
        }
    };
    (*pump)();
}

void QslCardController::cancelMail()
{
    ++m_mailGeneration;
    m_mailPreparing = false;
    // Quelle gia' consegnate al Cloud sono partite e non tornano indietro; le
    // altre si fermano qui, prima di uscire.
    if (m_network) {
        const QList<QNetworkReply*> flying = m_network->findChildren<QNetworkReply*>();
        for (QNetworkReply* reply : flying)
            reply->abort();
    }
    m_mailWaiting = 0;
    m_mail.cancel();
    m_mailTargets.clear();
    m_mailStatus = tr("Sending stopped.");
    emit mailChanged();
}

void QslCardController::refresh()
{
    invalidateCache();
    if (m_ctx.logChanged) {
        m_notifyingLogChange = true;
        m_ctx.logChanged();
        m_notifyingLogChange = false;
    }
}

} // namespace decolog::app
