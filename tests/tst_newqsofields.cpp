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
    void typedDefinitionsAndValuesKeepOriginalAdifText()
    {
        const QVariantList fields{QVariantMap{{"name","APP_TEST_NUMBER"},{"label","Power"},{"type","number"},{"unit","W"}},
                                  QVariantMap{{"name","APP_TEST_DATE"},{"type","date"}},
                                  QVariantMap{{"name","APP_TEST_BOOL"},{"type","boolean"}},
                                  QVariantMap{{"name","APP_TEST_LIST"},{"type","list"},{"choices",QStringList{"A","B"}}}};
        const auto result=newqso::validateCustomFields(fields); QVERIFY(result.ok());
        QCOMPARE(newqso::decodeCustomFields(newqso::encodeCustomFields(fields)),result.fields);
        QVERIFY(newqso::validateCustomValue(result.fields[0].toMap(),"12,5",QLocale(QLocale::Italian)).isEmpty());
        QVERIFY(!newqso::validateCustomValue(result.fields[0].toMap(),"12x",QLocale::c()).isEmpty());
        QVERIFY(!newqso::validateCustomValue(result.fields[1].toMap(),"2026-02-30",QLocale::c()).isEmpty());
        QVERIFY(newqso::validateCustomValue(result.fields[1].toMap(),"2026-02-28",QLocale::c()).isEmpty());
        QVERIFY(newqso::validateCustomValue(result.fields[2].toMap(),"Y",QLocale::c()).isEmpty());
        QVERIFY(!newqso::validateCustomValue(result.fields[2].toMap(),"maybe",QLocale::c()).isEmpty());
        QVERIFY(newqso::validateCustomValue(result.fields[3].toMap(),"A",QLocale::c()).isEmpty());
        QVERIFY(!newqso::validateCustomValue(result.fields[3].toMap(),"C",QLocale::c()).isEmpty());
        QVERIFY(newqso::validateCustomValue(result.fields[1].toMap(),"",QLocale::c()).isEmpty());
        QVERIFY(!newqso::validateCustomFields({QVariantMap{{"name","APP_TEST"},{"type","script"}}}).ok());
        QVERIFY(!newqso::validateCustomFields({QVariantMap{{"name","APP_TEST"},{"type","list"},{"choices",QStringList{"A","A"}}}}).ok());
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto qso=db.insertQso(AdifRecord{{"CALL","K1NUM"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_TEST_NUMBER","12,5"}},"manual",{},true);
        QVERIFY(qso.id>0); const QString adif=db.exportAdif(QList<qint64>{qso.id}); QVERIFY(adif.contains("<app_test_number:4>12,5"));
        LogDatabase imported; QVERIFY(imported.open(":memory:")); QCOMPARE(imported.importAdif(adif.toUtf8()).inserted,1);
        QVERIFY(imported.record(1)); QCOMPARE(imported.record(1)->value("APP_TEST_NUMBER"),QString("12,5"));
    }
    void conditionalRequirementsAreValidatedForMissingFields()
    {
        QVariantList defs{QVariantMap{{"name","APP_OPERATOR"},{"type","list"},{"choices",QStringList{"SOLO","TEAM"}}},
                          QVariantMap{{"name","APP_TEAM"},{"requiredIf",QVariantMap{{"field","app_operator"},{"equals","TEAM"}}}}};
        const auto checked=newqso::validateCustomFields(defs); QVERIFY(checked.ok());
        QCOMPARE(newqso::decodeCustomFields(newqso::encodeCustomFields(defs)),checked.fields);
        QVERIFY(newqso::validateCustomValues(checked.fields,{{"APP_OPERATOR","SOLO"}},QLocale::c()).isEmpty());
        QVERIFY(!newqso::validateCustomValues(checked.fields,{{"APP_OPERATOR","TEAM"}},QLocale::c()).isEmpty());
        QVERIFY(newqso::validateCustomValues(checked.fields,{{"app_operator","TEAM"},{"APP_TEAM","Club"}},QLocale::c()).isEmpty());
        QVERIFY(newqso::validateCustomValues(checked.fields,{},QLocale::c()).isEmpty());
        defs[1]=QVariantMap{{"name","APP_TEAM"},{"requiredIf",QVariantMap{{"field","APP_UNKNOWN"},{"equals","TEAM"}}}};
        QVERIFY(!newqso::validateCustomFields(defs).ok());
        defs[1]=QVariantMap{{"name","APP_TEAM"},{"requiredIf",QVariantMap{{"field","APP_TEAM"},{"equals","X"}}}};
        QVERIFY(!newqso::validateCustomFields(defs).ok());
    }
};

QTEST_GUILESS_MAIN(TestNewQsoFields)
#include "tst_newqsofields.moc"
