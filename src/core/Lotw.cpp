#include "core/Lotw.h"

#include "core/NetworkError.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace decolog::core {

namespace lotw {

Report parseReport(const QByteArray& data)
{
    Report report;
    if (!data.contains("<eoh>") && !data.contains("<EOH>") && !data.contains("<Eoh>")) {
        const QByteArray lower = data.left(8192).toLower();
        if (lower.contains("password incorrect") || lower.contains("username/password"))
            report.error = QCoreApplication::translate("Lotw", "LoTW: username or password incorrect");
        else if (lower.contains("<html"))
            report.error = QCoreApplication::translate("Lotw", "LoTW: the server answered with a web page, not with ADIF");
        else
            report.error = QCoreApplication::translate("Lotw", "LoTW: unexpected answer");
        return report;
    }

    const AdifDocument doc = adif::parse(data);
    report.lastQsl = doc.header.value(QStringLiteral("APP_LOTW_LASTQSL")).trimmed();
    bool ok = false;
    const int declared = doc.header.value(QStringLiteral("APP_LOTW_NUMREC")).trimmed().toInt(&ok);
    report.declaredRecords = ok ? declared : -1;
    for (const AdifRecord& r : doc.records) {
        if (r.value(QStringLiteral("QSL_RCVD")).trimmed().toUpper() == QLatin1String("Y"))
            report.confirmations << r;
    }
    report.ok = true;
    return report;
}

} // namespace lotw

LotwClient::LotwClient(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

void LotwClient::download(const QString& user, const QString& password, const QString& since,
                          const QDate& from, const QDate& to)
{
    if (m_reply)
        return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("login"), user);
    q.addQueryItem(QStringLiteral("password"), password);
    q.addQueryItem(QStringLiteral("qso_query"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("qso_qsl"), QStringLiteral("yes"));
    q.addQueryItem(QStringLiteral("qso_qsldetail"), QStringLiteral("yes"));
    q.addQueryItem(QStringLiteral("qso_withown"), QStringLiteral("yes"));
    if (!since.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("qso_qslsince"), since.trimmed());
    // Il periodo dei QSO (non delle conferme): qso_startdate e qso_enddate.
    if (from.isValid())
        q.addQueryItem(QStringLiteral("qso_startdate"), from.toString(Qt::ISODate));
    if (to.isValid())
        q.addQueryItem(QStringLiteral("qso_enddate"), to.toString(Qt::ISODate));
    // LoTW vuole le credenziali nella query, solo su HTTPS; l'URL non si scrive
    // da nessuna parte e gli errori lo tolgono (NetworkError.h).
    QUrl url = m_url;
    url.setQuery(q);
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    // Il primo sync di un log grande puo' durare minuti: LoTW prepara il file
    // prima di mandare il primo byte.
    request.setTransferTimeout(300'000);
    m_reply = m_net->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        emit progress(received);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        lotw::Report report;
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            report.error = tr("LoTW: download cancelled");
        } else if (reply->error() != QNetworkReply::NoError) {
            report.error = tr("LoTW: %1").arg(network::safeErrorString(reply));
        } else {
            report = lotw::parseReport(reply->readAll());
        }
        emit finished(report);
    });
}

void LotwClient::cancel()
{
    if (m_reply)
        m_reply->abort();
}

} // namespace decolog::core
