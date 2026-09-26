// DecoDXLog — conferme da LoTW (Logbook of the World, ARRL).
//
// L'invio dei QSO a LoTW passa dal TQSL locale, che firma con il certificato
// dell'operatore; qui si fa l'altra meta': scaricare le conferme con
// lotwreport.adi e segnarle sui QSO del log. Si chiede solo quello che e' arrivato
// dopo l'ultima conferma vista (APP_LoTW_LASTQSL), cosi' un sync quotidiano pesa
// pochi kilobyte anche con un log di anni.
#pragma once

#include "core/Adif.h"

#include <QDate>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace decolog::core {

namespace lotw {

struct Report {
    bool ok{false};
    QString error;
    QString lastQsl;            // "yyyy-MM-dd HH:mm:ss", il cursore per il prossimo sync
    int declaredRecords{-1};    // APP_LoTW_NUMREC, -1 se manca
    QList<AdifRecord> confirmations;
};

// La risposta di lotwreport.adi. Con credenziali sbagliate LoTW risponde con una
// pagina HTML invece che con un ADIF.
Report parseReport(const QByteArray& data);

} // namespace lotw

class LotwClient : public QObject {
    Q_OBJECT

public:
    explicit LotwClient(QObject* parent = nullptr);

    // Per i test: un server finto al posto di lotw.arrl.org.
    void setEndpoint(const QUrl& url) { m_url = url; }
    bool busy() const { return m_reply != nullptr; }

    // Le conferme ricevute da `since` in poi ("yyyy-MM-dd HH:mm:ss"; vuoto = tutte).
    // `from` e `to` (valide o no) limitano ai QSO fatti in quei giorni, estremi
    // compresi: e' il "dal … al …" dello scarico. L'esito arriva con finished().
    void download(const QString& user, const QString& password, const QString& since,
                  const QDate& from = {}, const QDate& to = {});
    void cancel();

signals:
    void progress(qint64 bytesReceived);
    void finished(const decolog::core::lotw::Report& report);

private:
    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply{nullptr};
    QUrl m_url{QStringLiteral("https://lotw.arrl.org/lotwuser/lotwreport.adi")};
};

} // namespace decolog::core
