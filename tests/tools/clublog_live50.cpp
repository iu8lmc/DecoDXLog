// Explicitly authorised, single-attempt 50-record Club Log duplicate test.
// Reads token from stdin. The journal prevents blind resubmission; --resume
// only polls an existing upload. Never use clear=1 and never print the token.
#include "core/ClubLogUploadJournal.h"
#include "core/QslUpload.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QHostAddress>
#include <QTimer>
#include <cstdio>

using namespace decolog::core;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3) { std::fprintf(stderr, "usage: clublog_live50 STAGING_DIR --submit-once|--resume\n"); return 2; }
    const QString root = QString::fromLocal8Bit(argv[1]);
    const QString operation = QString::fromLatin1(argv[2]);
    if (operation != QLatin1String("--submit-once") && operation != QLatin1String("--resume")) return 2;
    QFile selected(root + QStringLiteral("/selected-private.adi"));
    QFile manifest(root + QStringLiteral("/manifest-private.json"));
    if (!selected.open(QIODevice::ReadOnly) || !manifest.open(QIODevice::ReadOnly)) return 3;
    const QByteArray adifData = selected.readAll();
    const QJsonObject metadata = QJsonDocument::fromJson(manifest.readAll()).object();
    const QJsonArray idArray = metadata.value(QStringLiteral("existingQsoIds")).toArray();
    if (metadata.value(QStringLiteral("count")).toInt() != 50 || idArray.size() != 50 ||
        metadata.value(QStringLiteral("callsign")).toString() != QLatin1String("9H1SR")) return 4;
    QSet<QString> expected;
    QStringList ids;
    for (const QJsonValue& entry : idArray) {
        bool valid = false;
        const qlonglong numeric = entry.toString().toLongLong(&valid);
        const QString id = QString::number(numeric);
        if (!valid || numeric <= 0 || expected.contains(id)) return 4;
        expected.insert(id); ids.append(id);
    }
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(adifData, QCryptographicHash::Sha256).toHex());
    if (digest != metadata.value(QStringLiteral("sha256")).toString()) return 4;
    char secret[256] = {};
    if (!std::fgets(secret, sizeof(secret), stdin)) return 5;
    const QString token = QString::fromUtf8(secret).trimmed();
    std::fill(std::begin(secret), std::end(secret), char(0));
    if (!token.startsWith(QStringLiteral("cl_"))) return 5;
    QSettings settings(root + QStringLiteral("/live-journal.ini"), QSettings::IniFormat);
    ClubLogUploadJournal journal(settings);
    QFile previousReceipt(root + QStringLiteral("/receipt.json"));
    if (previousReceipt.exists()) {
        if (operation != QLatin1String("--resume") || !previousReceipt.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "A completed receipt already exists: refusing to send again\n"); return 6;
        }
        const QJsonObject receipt = QJsonDocument::fromJson(previousReceipt.readAll()).object();
        const bool valid = receipt.value(QStringLiteral("count")).toInt() == 50
            && receipt.value(QStringLiteral("allOriginalQsoIdsMatched")).toBool()
            && receipt.value(QStringLiteral("sha256")).toString() == digest;
        if (!valid) { std::fprintf(stderr, "Receipt does not match the staged records\n"); return 6; }
        if (journal.pending()) {
            if (journal.phase() != QLatin1String("queued") || journal.localIds() != ids
                || journal.payloadSha256() != digest
                || receipt.value(QStringLiteral("upload_id")).toString() != journal.uploadId()
                || !journal.clearAfterReconciliation(journal.nonce())) {
                std::fprintf(stderr, "Receipt does not match checkpoint; review required\n"); return 6;
            }
        }
        std::printf("Already reconciled: 50/50 original QSO IDs matched; no network request made\n");
        return 0;
    }
    if (operation == QLatin1String("--submit-once")) {
        if (!journal.begin(QStringLiteral("clublog-live-staging"), QStringLiteral("9H1SR"), ids, digest)) {
            std::fprintf(stderr, "Existing or unwritable checkpoint: refusing to send\n"); return 6;
        }
    } else if (journal.phase() != QLatin1String("queued") || journal.localIds() != ids || journal.payloadSha256() != digest) {
        std::fprintf(stderr, "No matching queued checkpoint: refusing to resume\n"); return 6;
    }
    const QString journalNonce = journal.nonce();
    WebQslUploader web;
    const QByteArray fixture = qgetenv("DECODXLOG_CLUBLOG_FIXTURE_URL");
    if (!fixture.isEmpty()) {
        const QUrl url = QUrl::fromEncoded(fixture);
        const QHostAddress host(url.host());
        if (url.scheme() != QLatin1String("http") || !host.isLoopback() || token != QLatin1String("cl_test_fixture")) return 9;
        web.setClubLogRestBatchBaseUrl(url);
        web.setClubLogRestPollIntervalForTest(1);
    }
    int phase = operation == QLatin1String("--resume") ? 1 : 0;
    QObject::connect(&web, &WebQslUploader::finished, &app, [&](const QslUploadResult& result) {
        if (phase == 0 && !result.remoteId.isEmpty() && journal.phase() == QLatin1String("queued")) {
            phase = 1;
            std::printf("Upload queued; polling its saved identifier\n"); std::fflush(stdout);
            QTimer::singleShot(0, &app, [&] { web.pollClubLogRestBatch(QStringLiteral("9H1SR"), token, journal.uploadId(), 50); });
            return;
        }
        if (phase == 1 && result.clubLogBatchRecords.size() == 50) {
            QSet<QString> observed;
            int duplicate=0, updated=0, created=0;
            for (const QVariant& value : result.clubLogBatchRecords) {
                const QVariantMap row = value.toMap();
                bool valid = false;
                const qlonglong numeric = row.value(QStringLiteral("qsoid")).toLongLong(&valid);
                const QString id = QString::number(numeric);
                const QString status = row.value(QStringLiteral("status")).toString();
                if (!valid || numeric <= 0 || !expected.contains(id) || observed.contains(id)) { std::fprintf(stderr, "Result IDs do not match; checkpoint retained\n"); app.exit(7); return; }
                observed.insert(id);
                if (status == QLatin1String("duplicate")) ++duplicate;
                else if (status == QLatin1String("updated")) ++updated;
                else if (status == QLatin1String("created")) ++created;
                else { std::fprintf(stderr, "Unrecognised or rejected result; checkpoint retained\n"); app.exit(7); return; }
            }
            if (observed != expected) { app.exit(7); return; }
            QSaveFile receipt(root + QStringLiteral("/receipt.json"));
            if (!receipt.open(QIODevice::WriteOnly)) { app.exit(8); return; }
            QJsonObject data{{QStringLiteral("count"), 50}, {QStringLiteral("duplicate"), duplicate},
                             {QStringLiteral("updated"), updated}, {QStringLiteral("created"), created},
                             {QStringLiteral("upload_id"), journal.uploadId()}, {QStringLiteral("allOriginalQsoIdsMatched"), true},
                             {QStringLiteral("sha256"), digest}};
            const QByteArray receiptData = QJsonDocument(data).toJson(QJsonDocument::Indented);
            if (receipt.write(receiptData) != receiptData.size()) { app.exit(8); return; }
            if (!receipt.commit() || !journal.clearAfterReconciliation(journalNonce)) { app.exit(8); return; }
            std::printf("Finished: 50/50 original QSO IDs matched (duplicate=%d updated=%d created=%d)\n", duplicate, updated, created);
            app.exit(0); return;
        }
        std::fprintf(stderr, "Upload not reconciled; checkpoint retained. %s\n", qPrintable(result.message));
        app.exit(7);
    });
    QTimer::singleShot(0, &app, [&] {
        if (operation == QLatin1String("--submit-once")) {
            web.submitClubLogRestBatch(QStringLiteral("9H1SR"), token, adifData, [&](const QString& id) { return journal.queued(id, journalNonce); });
        } else {
            web.pollClubLogRestBatch(QStringLiteral("9H1SR"), token, journal.uploadId(), 50);
        }
    });
    return app.exec();
}
