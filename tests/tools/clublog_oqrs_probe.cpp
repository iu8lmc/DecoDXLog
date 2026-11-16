// Read-only integration probe. Token goes through stdin, never arguments.
#include "core/ClubLogOqrsClient.h"
#include <QCoreApplication>
#include <QTimer>
#include <algorithm>
#include <cstdio>
using namespace decolog::core;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("DecoDXLogTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ClubLogOqrsProbe"));
    if (argc != 2) return 2;
    char raw[256] = {};
    if (!std::fgets(raw, sizeof(raw), stdin)) return 2;
    const QString token = QString::fromUtf8(raw).trimmed();
    std::fill(std::begin(raw), std::end(raw), char(0));
    ClubLogOqrsClient client;
    QObject::connect(&client, &ClubLogOqrsClient::finished, &app,
                     [&app](bool ok, const QVariantList& rows, const QString& error) {
        if (!ok) { std::fprintf(stderr, "%s\n", qPrintable(error)); app.exit(1); return; }
        std::printf("Read %lld direct OQRS requests; no QSL status was changed\n", static_cast<long long>(rows.size()));
        app.exit(0);
    });
    QTimer::singleShot(0, &app, [&] { client.fetch(QString::fromLocal8Bit(argv[1]), token); });
    QTimer::singleShot(120000, &app, [&] { std::fprintf(stderr, "Timed out\n"); app.exit(3); });
    return app.exec();
}
