// Select exactly 50 existing contacts from a Club Log ADIF export for an
// explicitly authorised live duplicate/reconciliation test. Does not network.
#include "core/Adif.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cstdio>
using namespace decolog::core;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 4) return 2;
    QFile source(QString::fromLocal8Bit(argv[1]));
    if (!source.open(QIODevice::ReadOnly)) return 3;
    const AdifDocument input = adif::parse(source.readAll());
    AdifDocument output;
    output.header.set(QStringLiteral("ADIF_VER"), QStringLiteral("3.1.5"));
    output.header.set(QStringLiteral("PROGRAMID"), QStringLiteral("DecoDXLog"));
    QSet<QString> ids;
    QJsonArray originalIds;
    for (const auto& row : input.records) {
        const QString rawId = row.value(QStringLiteral("APP_CLUBLOG_QSOID")).trimmed();
        bool valid = false; const qlonglong numeric = rawId.toLongLong(&valid);
        const QString id = QString::number(numeric);
        if (!valid || numeric <= 0 || ids.contains(id)) continue;
        if (row.value(QStringLiteral("STATION_CALLSIGN")).compare(QStringLiteral("9H1SR"), Qt::CaseInsensitive) != 0) continue;
        if (row.value(QStringLiteral("CALL")).isEmpty() || row.value(QStringLiteral("QSO_DATE")).isEmpty()
            || row.value(QStringLiteral("TIME_ON")).isEmpty() || row.value(QStringLiteral("BAND")).isEmpty()
            || row.value(QStringLiteral("MODE")).isEmpty()) continue;
        ids.insert(id); originalIds.append(id); output.records.append(row);
        if (output.records.size() == 50) break;
    }
    if (output.records.size() != 50) { std::fprintf(stderr, "Fewer than 50 valid unique existing contacts\n"); return 4; }
    const QByteArray selected = adif::writeDocument(output);
    QFile file(QString::fromLocal8Bit(argv[2]));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(selected) != selected.size()) return 5;
    file.close();
    QJsonObject metadata{{QStringLiteral("count"), 50}, {QStringLiteral("callsign"), QStringLiteral("9H1SR")},
                         {QStringLiteral("existingQsoIds"), originalIds},
                         {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(selected, QCryptographicHash::Sha256).toHex())}};
    QFile manifest(QString::fromLocal8Bit(argv[3]));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || manifest.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented)) < 0) return 6;
    std::printf("Staged 50 unique existing contacts; no network or upload\n");
    return 0;
}
