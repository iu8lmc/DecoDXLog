#include "core/NewQsoFields.h"
#include "core/LogDatabase.h"

#include <QTest>

using namespace decolog::core;

class TestNewQsoFields : public QObject {
    Q_OBJECT

private slots:
    void standardVisibilityIsNormalized()
    {
        const QStringList defaults = newqso::defaultVisibleFields();
        QVERIFY(defaults.contains(QStringLiteral("SAT_NAME")));
        QVERIFY(defaults.contains(QStringLiteral("SAT_MODE")));
        QVERIFY(!defaults.contains(QStringLiteral("STATE")));
        QVERIFY(!defaults.contains(QStringLiteral("TX_PWR")));

        const QStringList normalized = newqso::normalizeVisibleFields(
            {QStringLiteral("state"), QStringLiteral("SAT_MODE"), QStringLiteral("state"),
             QStringLiteral("NOT_A_PANEL_FIELD")});
        QCOMPARE(normalized, QStringList({QStringLiteral("STATE"), QStringLiteral("SAT_MODE")}));
    }

    void customFieldsAreNormalizedAndSerialized()
    {
        const QVariantList requested{
            QVariantMap{{QStringLiteral("name"), QStringLiteral("rda")},
                        {QStringLiteral("label"), QStringLiteral("Russian district")}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("APP_MY_REF")},
                        {QStringLiteral("label"), QString()}},
        };
        const auto checked = newqso::validateCustomFields(requested);
        QVERIFY(checked.ok());
        QCOMPARE(checked.fields.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("RDA"));
        QCOMPARE(checked.fields.at(1).toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("APP_MY_REF"));

        const QString json = newqso::encodeCustomFields(requested);
        QVERIFY(!json.isEmpty());
        QCOMPARE(newqso::decodeCustomFields(json), checked.fields);
        QVERIFY(newqso::decodeCustomFields(QStringLiteral("not json")).isEmpty());
    }

    void legacyCustomIotaBecomesOptionalStandardField()
    {
        QVERIFY(!newqso::defaultVisibleFields().contains(QStringLiteral("IOTA")));
        QCOMPARE(newqso::normalizeVisibleFields({QStringLiteral("iota")}), QStringList{"IOTA"});

        const QString oldJson = QStringLiteral(R"([{"name":"IOTA","label":"Island"},{"name":"RDA","label":"District"}])");
        bool migratedIota = false;
        const auto remaining = newqso::decodeCustomFields(oldJson, &migratedIota);
        QVERIFY(migratedIota);
        const QVariantList expected{QVariantMap{{"name", "RDA"}, {"label", "District"}}};
        QCOMPARE(remaining, expected);
        QCOMPARE(newqso::decodeCustomFields(newqso::encodeCustomFields(remaining), &migratedIota), expected);
        QVERIFY(!migratedIota);
    }

    void unsafeAndDuplicateNamesAreRejected()
    {
        auto check = newqso::validateCustomFields(
            {QVariantMap{{QStringLiteral("name"), QStringLiteral("bad field")}}});
        QCOMPARE(check.error, newqso::CustomFieldError::InvalidName);

        check = newqso::validateCustomFields(
            {QVariantMap{{QStringLiteral("name"), QStringLiteral("CALL")}}});
        QCOMPARE(check.error, newqso::CustomFieldError::ProtectedField);

        check = newqso::validateCustomFields(
            {QVariantMap{{QStringLiteral("name"), QStringLiteral("STATE")}}});
        QCOMPARE(check.error, newqso::CustomFieldError::StandardField);

        check = newqso::validateCustomFields(
            {QVariantMap{{QStringLiteral("name"), QStringLiteral("RDA")}},
             QVariantMap{{QStringLiteral("name"), QStringLiteral("rda")}}});
        QCOMPARE(check.error, newqso::CustomFieldError::Duplicate);
    }

    void arbitraryAdifFieldSurvivesDatabaseAndExport()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        AdifRecord record{{"CALL", "4O4A"}, {"QSO_DATE", "20261022"}, {"TIME_ON", "120000"},
                          {"BAND", "20m"}, {"MODE", "CW"}, {"RDA", "MO-01"}};
        const InsertResult inserted = db.insertQso(record, QStringLiteral("manual"), {}, true);
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);

        const auto stored = db.record(inserted.id);
        QVERIFY(stored);
        QCOMPARE(stored->value(QStringLiteral("RDA")), QStringLiteral("MO-01"));
        QVERIFY(db.exportAdif(QList<qint64>{inserted.id}).contains("<rda:5>MO-01"));
    }
};

QTEST_GUILESS_MAIN(TestNewQsoFields)
#include "tst_newqsofields.moc"
