#include "core/NewQsoFields.h"
#include "core/LogDatabase.h"

#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

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
    void derivedNumericFieldsAreDeterministicAndRejectCycles()
    {
        QVariantList defs{QVariantMap{{"name","APP_A"},{"type","number"}},QVariantMap{{"name","APP_B"},{"type","number"}},
            QVariantMap{{"name","APP_TOTAL"},{"type","number"},{"formula","({APP_A} + {APP_B}) * 2"}},
            QVariantMap{{"name","APP_RESULT"},{"type","number"},{"formula","{APP_TOTAL} / 4"}}};
        const auto checked=newqso::validateCustomFields(defs); QVERIFY(checked.ok());
        QCOMPARE(newqso::decodeCustomFields(newqso::encodeCustomFields(defs)),checked.fields);
        const auto result=newqso::computeDerivedValues(checked.fields,{{"APP_A","1,5"},{"APP_B","2,5"}},QLocale(QLocale::Italian));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.values.value("APP_TOTAL").toString(),QString("8")); QCOMPARE(result.values.value("APP_RESULT").toString(),QString("2"));
        const auto missing=newqso::computeDerivedValues(checked.fields,{{"APP_A","1"}},QLocale::c()); QVERIFY(missing.error.isEmpty()); QVERIFY(!missing.values.contains("APP_TOTAL"));
        QVERIFY(!newqso::computeDerivedValues(checked.fields,{{"APP_A","1"},{"APP_B","2"},{"APP_TOTAL","999"}},QLocale::c()).error.isEmpty());
        defs[2]=QVariantMap{{"name","APP_TOTAL"},{"type","number"},{"formula","{APP_RESULT}+1"}}; QVERIFY(!newqso::validateCustomFields(defs).ok());
        defs[2]=QVariantMap{{"name","APP_TOTAL"},{"type","number"},{"formula","system(1)"}}; QVERIFY(!newqso::validateCustomFields(defs).ok());
        defs[2]=QVariantMap{{"name","APP_TOTAL"},{"type","number"},{"formula","{APP_A}/0"}}; const auto divide=newqso::validateCustomFields(defs); QVERIFY(divide.ok());
        QVERIFY(!newqso::computeDerivedValues(divide.fields,{{"APP_A","1"}},QLocale::c()).error.isEmpty());
        const QVariantList fractions{QVariantMap{{"name","APP_A"},{"type","number"}},QVariantMap{{"name","APP_OUT"},{"type","number"},{"formula","-({APP_A} + 1.5) / 4"}}};
        const auto fractionDefs=newqso::validateCustomFields(fractions); QVERIFY(fractionDefs.ok());
        const auto fraction=newqso::computeDerivedValues(fractionDefs.fields,{{"APP_A","1,5"}},QLocale(QLocale::Italian)); QVERIFY(fraction.error.isEmpty()); QCOMPARE(fraction.values.value("APP_OUT").toString(),QString("-0.75"));
        QVariantMap combined{{"APP_A","1,5"}}; combined.insert("APP_OUT",fraction.values.value("APP_OUT")); QVERIFY(newqso::validateCustomValues(fractionDefs.fields,combined,QLocale(QLocale::Italian)).isEmpty());
        const QVariantList unknown{QVariantMap{{"name","APP_OUT"},{"type","number"},{"formula","{CALL}+1"}}}; QVERIFY(!newqso::validateCustomFields(unknown).ok());
        const QVariantList tooDeep{QVariantMap{{"name","APP_OUT"},{"type","number"},{"formula",QString(40,'(')+"1"+QString(40,')')}}}; QVERIFY(!newqso::validateCustomFields(tooDeep).ok());
        const QVariantList required{QVariantMap{{"name","APP_A"},{"type","number"}},QVariantMap{{"name","APP_SWITCH"},{"type","text"}},
            QVariantMap{{"name","APP_OUT"},{"type","number"},{"formula","{APP_A}*2"},{"requiredIf",QVariantMap{{"field","APP_SWITCH"},{"equals","Y"}}}}};
        const auto requiredDefs=newqso::validateCustomFields(required); QVERIFY(requiredDefs.ok());
        const auto noInput=newqso::computeDerivedValues(requiredDefs.fields,{{"APP_SWITCH","Y"}},QLocale::c()); QVERIFY(noInput.error.isEmpty()); QVERIFY(noInput.values.isEmpty());
        QVERIFY(!newqso::validateCustomValues(requiredDefs.fields,{{"APP_SWITCH","Y"}},QLocale::c()).isEmpty());
    }
    void derivedProvenanceDetectsChangedInputsAndTransitiveResults()
    {
        const QVariantList defs{QVariantMap{{"name","APP_A"},{"type","number"}},
            QVariantMap{{"name","APP_B"},{"type","number"},{"formula","{APP_A} * 2"}},
            QVariantMap{{"name","APP_C"},{"type","number"},{"formula","{APP_B} + 1"}}};
        const auto checked=newqso::validateCustomFields(defs); QVERIFY(checked.ok()); QVariantMap values{{"APP_A","5"}};
        const auto computed=newqso::computeDerivedValues(checked.fields,values,QLocale::c()); QVERIFY(computed.error.isEmpty());
        for (auto it=computed.values.cbegin();it!=computed.values.cend();++it) values.insert(it.key(),it.value());
        values.insert("APP_DECOLOG_DERIVED_V1",newqso::makeDerivedTrace(checked.fields,values,computed.values,QLocale::c()));
        auto status=newqso::inspectDerivedTrace(values); QCOMPARE(status.value("state").toString(),QString("matches"));
        values.insert("APP_UNRELATED","hello"); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("matches"));
        values.insert("APP_A","6"); status=newqso::inspectDerivedTrace(values); QCOMPARE(status.value("state").toString(),QString("stale"));
        const auto affected=status.value("affected").toStringList(); QVERIFY(affected.contains("APP_B") && affected.contains("APP_C"));
        values.insert("APP_A","5"); values.insert("APP_B","999"); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("stale"));
        auto trace=QJsonDocument::fromJson(values.value("APP_DECOLOG_DERIVED_V1").toString().toUtf8()).object();
        auto fields=trace.value("fields").toObject(), entry=fields.value("APP_B").toObject(); entry.insert("inputs",QJsonObject{}); fields.insert("APP_B",entry); trace.insert("fields",fields);
        values.insert("APP_DECOLOG_DERIVED_V1",QString::fromUtf8(QJsonDocument(trace).toJson(QJsonDocument::Compact))); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("unknown"));
        entry.insert("expression","{APP_B}"); entry.insert("inputs",QJsonObject{{"APP_B","10"}}); fields.insert("APP_B",entry); trace.insert("fields",fields);
        values.insert("APP_DECOLOG_DERIVED_V1",QString::fromUtf8(QJsonDocument(trace).toJson(QJsonDocument::Compact))); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("unknown"));
        values.insert("APP_DECOLOG_DERIVED_V1",R"({"version":1,"fields":{}})"); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("unknown"));
        values.insert("APP_DECOLOG_DERIVED_V1","not json"); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("unknown"));
        values.remove("APP_DECOLOG_DERIVED_V1"); QCOMPARE(newqso::inspectDerivedTrace(values).value("state").toString(),QString("none"));
    }
};

QTEST_GUILESS_MAIN(TestNewQsoFields)
#include "tst_newqsofields.moc"
