// Attivazioni e contest: campi dell'attivatore sul QSO, numero progressivo,
// nome del file per POTA, e il giro completo di una sessione sul log.
#include "app/ActivationController.h"
#include "core/Activation.h"
#include "core/LogDatabase.h"

#include <QSqlQuery>
#include <QSignalSpy>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace decolog::core;
using decolog::app::ActivationController;

class TestActivation : public QObject {
    Q_OBJECT

private slots:
    void activationHasStableSessionIdentity()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        QVERIFY(!act.state().value("sessionUuid").toString().isEmpty());
    }
    void trackedLifecycleIsAtomicAndLegacyStaysUntracked()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("CREATE TRIGGER reject_run BEFORE INSERT ON activation_run BEGIN SELECT RAISE(ABORT,'rejected'); END"));
        QVERIFY(!act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        QVERIFY(!act.active());
        QVERIFY(q.exec("DROP TRIGGER reject_run"));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        const QString id = act.state().value("sessionUuid").toString(); QVERIFY(!id.isEmpty());
        AdifRecord first{{"CALL", "K1ABC"}}; act.session().applyTo(first, 0);
        QCOMPARE(first.value("APP_DECOLOG_ACTIVATION_UUID"), id);
        AdifRecord external{{"APP_DECOLOG_ACTIVATION_UUID", "external"}}; act.session().applyTo(external, 0);
        QCOMPARE(external.value("APP_DECOLOG_ACTIVATION_UUID"), QString("external"));
        auto updated = act.state(); updated["reference"] = "IT-0002"; updated["sessionUuid"] = "forged";
        QVERIFY(q.exec("CREATE TRIGGER reject_change BEFORE INSERT ON activation_run_change BEGIN SELECT RAISE(ABORT,'rejected'); END"));
        QVERIFY(!act.update(updated).isEmpty()); QCOMPARE(act.state().value("reference").toString(), QString("IT-0001"));
        act.stop(); QVERIFY(act.active());
        QVERIFY(q.exec("DROP TRIGGER reject_change"));
        QVERIFY(act.update(updated).isEmpty()); QCOMPARE(act.state().value("sessionUuid").toString(), id);
        act.stop(); QVERIFY(!act.active());
        QVERIFY(q.exec("SELECT ended_at, (SELECT COUNT(*) FROM activation_run_change WHERE session_uuid=activation_run.uuid) FROM activation_run"));
        QVERIFY(q.next()); QVERIFY(!q.value(0).toString().isEmpty()); QCOMPARE(q.value(1).toInt(), 3);
        db.setSetting("activation.session", R"({"active":true,"kind":"pota","reference":"IT-0003","startedAt":"2026-11-23T10:00:00Z"})");
        ActivationController::Context legacyContext; legacyContext.db = &db;
        ActivationController legacy(std::move(legacyContext)); legacy.load(); QVERIFY(legacy.active());
        QVERIFY(legacy.state().value("sessionUuid").toString().isEmpty());
        QVERIFY(!legacy.partitionPreview().value("error").toString().isEmpty());
        AdifRecord old{{"CALL", "K1OLD"}}; legacy.session().applyTo(old, 0);
        QVERIFY(old.value("APP_DECOLOG_ACTIVATION_UUID").isEmpty());
        legacy.stop(); QVERIFY(!legacy.active());
        QVERIFY(q.exec("SELECT COUNT(*) FROM activation_run")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
    }
    void previewGroupsReportedQsosWithoutAttributingUnrelatedContacts()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                           {"programs", QVariantMap{{"sota", "I/LM-001"}}}}).isEmpty());
        AdifRecord one{{"CALL","K1AAA"},{"QSO_DATE","20261231"},{"TIME_ON","235900"}, {"BAND","20m"},{"MODE","CW"},{"OPERATOR","9H1AA"},{"MY_GRIDSQUARE","JN35"},{"POTA_REF","US-1234"}};
        act.session().applyTo(one, 0); QCOMPARE(db.insertQso(one, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        AdifRecord two{{"CALL","K1BBB"},{"QSO_DATE","20270101"},{"TIME_ON","000100"}, {"BAND","20m"},{"MODE","CW"},{"OPERATOR","9H1BB"},{"MY_GRIDSQUARE","JN36"}};
        act.session().applyTo(two, 0); QCOMPARE(db.insertQso(two, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        AdifRecord unrelated{{"CALL","K1CCC"},{"QSO_DATE","20270101"},{"TIME_ON","000200"},{"BAND","20m"},{"MODE","CW"},{"MY_POTA_REF","IT-0001"}};
        QCOMPARE(db.insertQso(unrelated, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const QVariantMap preview = act.partitionPreview();
        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("reportedCount").toInt(), 2);
        const auto groups = preview.value("groups").toList(); QCOMPARE(groups.size(), 6);
        QSet<QString> observed;
        for (const auto& v : groups) { const auto g = v.toMap(); QCOMPARE(g.value("count").toInt(), 1);
            observed.insert(g.value("program").toString()+"/"+g.value("reference").toString()+"/"+g.value("day").toString()+"/"+g.value("operator").toString()+"/"+g.value("grid").toString()); }
        QVERIFY(observed.contains("POTA/IT-0001/2026-12-31/9H1AA/JN35"));
        QVERIFY(observed.contains("POTA/IT-0002/2026-12-31/9H1AA/JN35"));
        QVERIFY(observed.contains("SOTA/I/LM-001/2027-01-01/9H1BB/JN36"));
        QVERIFY(!observed.contains("POTA/US-1234/2026-12-31/9H1AA/JN35"));
        AdifRecord noRef{{"CALL","K1DDD"},{"QSO_DATE","20270101"},{"TIME_ON","000300"},{"BAND","20m"},{"MODE","CW"},
                          {"APP_DECOLOG_ACTIVATION_UUID",act.state().value("sessionUuid").toString()}};
        QCOMPARE(db.insertQso(noRef, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const auto again = act.partitionPreview(); QCOMPARE(again.value("missingReference").toInt(), 1);
        QCOMPARE(again.value("reportedCount").toInt(), 3);
        QVERIFY(db.connection().transaction());
        QVERIFY(!act.partitionPreview().value("error").toString().isEmpty());
        QVERIFY(db.connection().rollback());
    }
    void previewScalesPastFormerLimit()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("large.sqlite")));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        for (int n = 0; n < 1001; ++n) {
            AdifRecord r{{"CALL", QString("K%1AAA").arg(n)}, {"QSO_DATE", "20261231"},
                         {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}};
            act.session().applyTo(r, 0);
            QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        const auto result = act.partitionPreview();
        QVERIFY(result.value("error").toString().isEmpty());
        QCOMPARE(result.value("reportedCount").toInt(), 1001);
        QCOMPARE(result.value("groups").toList().at(0).toMap().value("count").toInt(), 1001);
        QSignalSpy finished(&act, &ActivationController::previewFinished);
        QVERIFY(act.startPartitionPreview());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.at(0).at(0).toBool());
        QCOMPARE(act.previewResult().value("reportedCount").toInt(), 1001);
        QCOMPARE(act.previewResult().value("groups").toList(), result.value("groups").toList());
    }
    void asynchronousPreviewCanCompleteCancelAndInvalidate()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        AdifRecord r{{"CALL", "K1ABC"}, {"QSO_DATE", "20261231"}, {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}};
        act.session().applyTo(r, 0); QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        QSignalSpy finished(&act, &ActivationController::previewFinished);
        QVERIFY(act.startPartitionPreview()); QVERIFY(act.previewBusy());
        QVERIFY(!act.startPartitionPreview());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.takeFirst().at(0).toBool());
        QCOMPARE(act.previewResult().value("reportedCount").toInt(), 1);
        QVERIFY(act.startPartitionPreview()); act.cancelPartitionPreview();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.takeFirst().at(0).toBool());
        QVERIFY(!act.previewResult().value("error").toString().isEmpty());
        QVERIFY(act.startPartitionPreview()); act.invalidateScore();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.takeFirst().at(0).toBool());
        QVERIFY(!act.previewResult().value("error").toString().isEmpty());
        QVERIFY(act.previewResult().value("groups").toList().isEmpty());
    }
    void partitionExportIsIsolatedAndRejectsStalePreview()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                           {"programs", QVariantMap{{"wwff", "IFF-0123"}, {"sota", "I/LM-001"}, {"iota", "EU-025"}}}}).isEmpty());
        AdifRecord r{{"CALL", "K1ABC"}, {"QSO_DATE", "20261231"}, {"TIME_ON", "235900"},
                     {"BAND", "20m"}, {"MODE", "CW"}, {"POTA_REF", "US-1234"}, {"SOTA_REF", "W2/WE-003"}, {"IOTA", "NA-001"}, {"OPERATOR", "9H1AA"}};
        act.session().applyTo(r, 0); const auto inserted = db.insertQso(r, "manual", {}, true, 0, false);
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);
        AdifRecord next{{"CALL", "K1DEF"}, {"QSO_DATE", "20270101"}, {"TIME_ON", "000100"},
                        {"BAND", "20m"}, {"MODE", "CW"}, {"OPERATOR", "9H1BB"}};
        act.session().applyTo(next, 0);
        QCOMPARE(db.insertQso(next, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        QSignalSpy previewed(&act, &ActivationController::previewFinished);
        QVERIFY(act.startPartitionPreview()); QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 5000); QVERIFY(previewed.at(0).at(0).toBool());
        QVariantMap chosen, wwff, sota, iota;
        for (const auto& value : act.previewResult().value("groups").toList()) {
            const auto group = value.toMap();
            if (group.value("day") != "2026-12-31") continue;
            if (group.value("program") == "POTA" && group.value("reference") == "IT-0001") chosen = group;
            if (group.value("program") == "WWFF" && group.value("reference") == "IFF-0123") wwff = group;
            if (group.value("program") == "SOTA") sota = group;
            if (group.value("program") == "IOTA") iota = group;
        }
        QVERIFY(!chosen.isEmpty()); QVERIFY(!wwff.isEmpty()); QVERIFY(!sota.isEmpty()); QVERIFY(!iota.isEmpty());
        QVERIFY(!act.startPartitionExport(chosen, QUrl::fromLocalFile(dir.filePath("log.sqlite"))));
        const QString path = dir.filePath("park.adi"); QSignalSpy exported(&act, &ActivationController::partitionExportFinished);
        QVERIFY(act.startPartitionExport(chosen, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY2(exported.at(0).at(0).toBool(), qPrintable(exported.at(0).at(1).toString()));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data = file.readAll(); file.close();
        const auto records = adif::parse(data).records; QCOMPARE(records.size(), 1);
        QCOMPARE(records.at(0).value("MY_POTA_REF"), QString("IT-0001"));
        QCOMPARE(records.at(0).value("MY_SIG"), QString("POTA"));
        QCOMPARE(records.at(0).value("MY_SIG_INFO"), QString("IT-0001"));
        QCOMPARE(records.at(0).value("POTA_REF"), QString("US-1234"));
        QCOMPARE(db.record(inserted.id)->value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
        exported.clear(); const QString wwffPath = dir.filePath("wwff.adi");
        QVERIFY(act.startPartitionExport(wwff, QUrl::fromLocalFile(wwffPath)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY2(exported.at(0).at(0).toBool(), qPrintable(exported.at(0).at(1).toString()));
        QFile wwffFile(wwffPath); QVERIFY(wwffFile.open(QIODevice::ReadOnly)); const auto wwffRecords = adif::parse(wwffFile.readAll()).records;
        QCOMPARE(wwffRecords.size(), 1); QCOMPARE(wwffRecords.at(0).value("MY_SIG"), QString("WWFF"));
        QCOMPARE(wwffRecords.at(0).value("MY_SIG_INFO"), QString("IFF-0123"));
        QCOMPARE(wwffRecords.at(0).value("POTA_REF"), QString("US-1234"));
        for (const auto& group : {sota, iota}) {
            exported.clear(); const QString out = dir.filePath(group.value("program").toString() + ".adi");
            QVERIFY(act.startPartitionExport(group, QUrl::fromLocalFile(out)));
            QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY2(exported.at(0).at(0).toBool(), qPrintable(exported.at(0).at(1).toString()));
            QFile output(out); QVERIFY(output.open(QIODevice::ReadOnly)); const auto selected = adif::parse(output.readAll()).records;
            QCOMPARE(selected.size(), 1); QCOMPARE(selected.at(0).value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(selected.at(0).value("SOTA_REF"), QString("W2/WE-003"));
            QCOMPARE(selected.at(0).value("MY_IOTA"), QString("EU-025")); QCOMPARE(selected.at(0).value("IOTA"), QString("NA-001"));
            QCOMPARE(selected.at(0).value("MY_SIG"), QString("POTA"));
        }
        r.set("COMMENT", "changed after preview"); QCOMPARE(db.updateQso(inserted.id, r).status, InsertResult::Status::Inserted);
        exported.clear(); QVERIFY(act.startPartitionExport(chosen, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY(!exported.at(0).at(0).toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), data); file.close();
        const QString sessionId = act.state().value("sessionUuid").toString();
        act.stop(); QVERIFY(!act.active());
        const auto runs = act.closedRuns(0); QCOMPARE(runs.size(), 1);
        QCOMPARE(runs.at(0).toMap().value("uuid").toString(), sessionId);
        previewed.clear(); QVERIFY(act.startRunPartitionPreview(sessionId));
        QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 5000); QVERIFY(previewed.at(0).at(0).toBool());
        QVariantMap closedGroup;
        for (const auto& item : act.previewResult().value("groups").toList()) {
            const auto g = item.toMap();
            if (g.value("program") == "POTA" && g.value("reference") == "IT-0001" && g.value("day") == "2026-12-31") closedGroup = g;
        }
        QVERIFY(!closedGroup.isEmpty()); exported.clear();
        QVERIFY(act.startPartitionExport(closedGroup, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY(exported.at(0).at(0).toBool());
        LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("log.sqlite")));
        ActivationController::Context reopenContext; reopenContext.db = &reopened;
        ActivationController restored(std::move(reopenContext)); restored.load(); QVERIFY(!restored.active());
        QCOMPARE(restored.closedRuns(0).at(0).toMap().value("uuid").toString(), sessionId);
        QSignalSpy reopenedPreview(&restored, &ActivationController::previewFinished);
        QVERIFY(restored.startRunPartitionPreview(sessionId));
        QTRY_COMPARE_WITH_TIMEOUT(reopenedPreview.size(), 1, 5000); QVERIFY(reopenedPreview.at(0).at(0).toBool());
        QCOMPARE(restored.previewResult().value("reportedCount").toInt(), 2);
    }
    void closedSessionListingIsPaged()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); QSqlQuery q(db.connection());
        QVERIFY(q.exec("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<102) INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) SELECT printf('run-%d',x),'2026-01-01','2026-01-02','{}','{\"title\":\"Test\"}' FROM n"));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        const auto first = act.closedRuns(0); QCOMPARE(first.size(), 101);
        QCOMPARE(first.at(0).toMap().value("uuid").toString(), QString("run-102"));
        const auto second = act.closedRuns(first.at(99).toMap().value("rowid").toLongLong());
        QCOMPARE(second.size(), 2); QCOMPARE(second.at(1).toMap().value("uuid").toString(), QString("run-1"));
    }
    void simultaneousProgramReferencesStayIndependent()
    {
        const QVariantMap programs{{"sota", "I/LM-001"}, {"wwff", "IFF-0123"}};
        Activation a = Activation::fromMap({{"active", true}, {"kind", "pota"},
                                             {"reference", "IT-0001,IT-0002"}, {"programs", programs}});
        AdifRecord r{{"CALL", "K1ABC"}, {"POTA_REF", "US-1234"}, {"SOTA_REF", "W2/WE-003"}};
        a.applyTo(r, 0);
        QCOMPARE(r.value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
        QCOMPARE(r.value("MY_SOTA_REF"), QString("I/LM-001"));
        QCOMPARE(r.value("MY_WWFF_REF"), QString("IFF-0123"));
        QCOMPARE(r.value("POTA_REF"), QString("US-1234"));
        QCOMPARE(r.value("SOTA_REF"), QString("W2/WE-003"));
        QCOMPARE(r.value("MY_SIG"), QString("POTA"));
        QCOMPARE(Activation::fromMap(a.toMap()).toMap().value("programs").toMap(), programs);
        AdifRecord already{{"CALL", "K1ABC"}, {"MY_SOTA_REF", "G/LD-003"}, {"WWFF_REF", "KFF-4655"}};
        a.applyTo(already, 0);
        QCOMPARE(already.value("MY_SOTA_REF"), QString("G/LD-003"));
        QCOMPARE(already.value("WWFF_REF"), QString("KFF-4655"));
    }
    void additionalProgramSyntaxIsChecked()
    {
        QVariantMap normalized;
        QString error;
        QVERIFY(Activation::normalizePrograms({{"pota", " it-0001, it-0002@it-25 "}, {"sota", " i/lm-001 "},
                                               {"wwff", " iff-0123 "}, {"iota", " eu-025 "}}, &normalized, &error));
        QCOMPARE(normalized.value("pota").toString(), QString("IT-0001,IT-0002@IT-25"));
        QCOMPARE(normalized.value("sota").toString(), QString("I/LM-001"));
        QCOMPARE(normalized.value("wwff").toString(), QString("IFF-0123"));
        QCOMPARE(normalized.value("iota").toString(), QString("EU-025"));
        QVERIFY(!Activation::normalizePrograms({{"pota", "IT-0001,it-0001"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"pota", "IT-0001,"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"sota", "I/LM-001,I/LM-002"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"wwff", "IFF-123"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"iota", "EU-000"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"unknown", "X-0001"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"sota", 123}}, &normalized, &error));
    }
    void multiProgramSessionPersistsAndOnlyAffectsFutureQsos()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("log.sqlite");
        qint64 firstId = 0, secondId = 0;
        {
            LogDatabase db; QVERIFY(db.open(path));
            ActivationController::Context ctx; ctx.db = &db;
            ActivationController act(std::move(ctx)); act.load();
            QVERIFY(!act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                                {"programs", QVariantMap{{"sota", "I/LM-001,I/LM-002"}}}}).isEmpty());
            QVERIFY(!act.active());
            QVERIFY(!act.start({{"kind", "pota"}, {"reference", "IT-0001"},
                                {"programs", QVariantMap{{"pota", "IT-0002"}}}}).isEmpty());
            QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                               {"programs", QVariantMap{{"sota", "i/lm-001"}, {"wwff", "iff-0123"}}}}).isEmpty());
            AdifRecord first{{"CALL", "K1ABC"}, {"QSO_DATE", "20261123"}, {"TIME_ON", "100000"}, {"BAND", "20m"}, {"MODE", "CW"}, {"SOTA_REF", "G/LD-003"}};
            act.session().applyTo(first, 0);
            const auto one = db.insertQso(first, "manual", {}, true, 0, false); QCOMPARE(one.status, InsertResult::Status::Inserted); firstId = one.id;
            QVariantMap changed = act.state();
            changed["programs"] = QVariantMap{{"sota", "I/LM-002"}, {"wwff", "IFF-0123"}};
            QVERIFY(act.update(changed).isEmpty());
            AdifRecord second{{"CALL", "K1DEF"}, {"QSO_DATE", "20261123"}, {"TIME_ON", "101000"}, {"BAND", "20m"}, {"MODE", "CW"}};
            act.session().applyTo(second, 0);
            const auto two = db.insertQso(second, "manual", {}, true, 0, false); QCOMPARE(two.status, InsertResult::Status::Inserted); secondId = two.id;
            changed["programs"] = QVariantMap{{"sota", "invalid"}};
            QVERIFY(!act.update(changed).isEmpty());
            QCOMPARE(act.state().value("programs").toMap().value("sota").toString(), QString("I/LM-002"));
            QVariantMap legacyUpdate = act.state(); legacyUpdate.remove("programs");
            QVERIFY(act.update(legacyUpdate).isEmpty());
            QCOMPARE(act.state().value("programs").toMap().value("sota").toString(), QString("I/LM-002"));
            QCOMPARE(db.record(firstId)->value("APP_DECOLOG_ACTIVATION_UUID"), act.state().value("sessionUuid").toString());
            QCOMPARE(db.record(secondId)->value("APP_DECOLOG_ACTIVATION_UUID"), act.state().value("sessionUuid").toString());
            QCOMPARE(db.record(firstId)->value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(db.record(firstId)->value("SOTA_REF"), QString("G/LD-003"));
            QCOMPARE(db.record(secondId)->value("MY_SOTA_REF"), QString("I/LM-002"));
        }
        {
            LogDatabase db; QVERIFY(db.open(path));
            ActivationController::Context ctx; ctx.db = &db;
            ActivationController act(std::move(ctx)); act.load(); QVERIFY(act.active());
            QCOMPARE(act.state().value("programs").toMap().value("sota").toString(), QString("I/LM-002"));
            QCOMPARE(db.record(firstId)->value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(db.record(secondId)->value("MY_WWFF_REF"), QString("IFF-0123"));
            const QByteArray exported = db.exportAdif();
            LogDatabase imported; QVERIFY(imported.open(":memory:")); QCOMPARE(imported.importAdif(exported).inserted, 2);
            const auto records = adif::parse(imported.exportAdif()).records; QCOMPARE(records.size(), 2);
            QCOMPARE(records.at(0).value("APP_DECOLOG_ACTIVATION_UUID"), act.state().value("sessionUuid").toString());
            QCOMPARE(records.at(0).value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
            QCOMPARE(records.at(0).value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(records.at(0).value("MY_WWFF_REF"), QString("IFF-0123"));
            QCOMPARE(records.at(0).value("SOTA_REF"), QString("G/LD-003"));
        }
    }
    void potaFieldsAndFileName()
    {
        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Pota;
        a.reference = "IT-1234";
        a.myGrid = "JN71DC";
        a.tag = "pota";
        QCOMPARE(a.requiredQsos(), 10);

        AdifRecord r{{"CALL", "K1ABC"}, {"BAND", "20m"}, {"MODE", "FT8"}};
        a.applyTo(r, 0);
        // I campi dell'attivatore, non quelli del cacciatore.
        QCOMPARE(r.value("MY_SIG"), QString("POTA"));
        QCOMPARE(r.value("MY_SIG_INFO"), QString("IT-1234"));
        QCOMPARE(r.value("MY_POTA_REF"), QString("IT-1234"));
        QVERIFY(r.value("POTA_REF").isEmpty());
        QCOMPARE(r.value("MY_GRIDSQUARE"), QString("JN71DC"));
        QCOMPARE(r.value("APP_DECOLOG_TAGS"), QString("pota"));
        QCOMPARE(a.exportFileName("IU8LMC/P", QDate(2026, 9, 17)), QString("IU8LMC_P@IT-1234-20260917.adi"));

        // Un campo che il QSO ha gia' non si tocca.
        AdifRecord mine{{"CALL", "K1ABC"}, {"MY_GRIDSQUARE", "JN70AA"}, {"APP_DECOLOG_TAGS", "pota,test"}};
        a.applyTo(mine, 0);
        QCOMPARE(mine.value("MY_GRIDSQUARE"), QString("JN70AA"));
        QCOMPARE(mine.value("APP_DECOLOG_TAGS"), QString("pota,test"));
    }

    void contestSerial()
    {
        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Contest;
        a.contestId = "CQ-WW-SSB";
        a.serialEnabled = true;
        AdifRecord r{{"CALL", "K1ABC"}};
        a.applyTo(r, 7);
        QCOMPARE(r.value("CONTEST_ID"), QString("CQ-WW-SSB"));
        QCOMPARE(r.value("STX"), QString("7"));
        QCOMPARE(r.value("APP_DECOLOG_TAGS"), QString("cq-ww-ssb"));
        QCOMPARE(a.exportFileName("IU8LMC", QDate(2026, 9, 17)), QString("IU8LMC-CQ-WW-SSB-20260917.adi"));

        const Activation back = Activation::fromMap(a.toMap());
        QCOMPARE(back.contestId, a.contestId);
        QVERIFY(back.serialEnabled);
        // Le regole dell'edizione, la rete e il call history restano nella sessione.
        a.sharedSerial = true;
        a.rules = {{"submitHours", 72}};
        a.callHistory = "C:/x/cqww.txt";
        const Activation again = Activation::fromMap(a.toMap());
        QVERIFY(again.sharedSerial);
        QCOMPARE(again.rules.value("submitHours").toInt(), 72);
        QCOMPARE(again.callHistory, QString("C:/x/cqww.txt"));
    }

    void multipleActivationReferencesArePreserved()
    {
        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Pota;
        a.reference = QStringLiteral("IT-0001,IT-0002");
        a.myGrid = QStringLiteral("JN70");

        AdifRecord r{{"CALL", "K1ABC"}};
        a.applyTo(r, 0);
        QCOMPARE(r.value("MY_SIG_INFO"), QStringLiteral("IT-0001,IT-0002"));
        QCOMPARE(r.value("MY_POTA_REF"), QStringLiteral("IT-0001,IT-0002"));

        const Activation restored = Activation::fromMap(a.toMap());
        QCOMPARE(restored.reference, a.reference);
    }

    void multipleActivationReferencesSurviveAdifExport()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));

        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Pota;
        a.reference = QStringLiteral("IT-0001,IT-0002");
        a.myGrid = QStringLiteral("JN70");

        AdifRecord first{{"CALL", "K1ABC"}, {"QSO_DATE", "20261015"}, {"TIME_ON", "101500"},
                         {"BAND", "20m"}, {"MODE", "SSB"}};
        AdifRecord second{{"CALL", "W1AW"}, {"QSO_DATE", "20261015"}, {"TIME_ON", "101600"},
                          {"BAND", "40m"}, {"MODE", "CW"}};
        a.applyTo(first, 0);
        a.applyTo(second, 0);
        const auto one = db.insertQso(first, QStringLiteral("manual"), {}, true);
        const auto two = db.insertQso(second, QStringLiteral("manual"), {}, true);
        QCOMPARE(one.status, InsertResult::Status::Inserted);
        QCOMPARE(two.status, InsertResult::Status::Inserted);

        const AdifDocument exported = adif::parse(db.exportAdif(QList<qint64>{one.id, two.id}));
        QCOMPARE(exported.records.size(), 2);
        for (const AdifRecord& record : exported.records) {
            QCOMPARE(record.value(QStringLiteral("MY_SIG_INFO")), QStringLiteral("IT-0001,IT-0002"));
            QCOMPARE(record.value(QStringLiteral("MY_POTA_REF")), QStringLiteral("IT-0001,IT-0002"));
            QCOMPARE(record.value(QStringLiteral("MY_GRIDSQUARE")), QStringLiteral("JN70"));
        }
    }

    void callHistoryAndSharedSerials()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QTemporaryDir dir;
        const QString file = dir.filePath("cqww.txt");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("!!Order!!,Call,Name,CQZone\nK9ZZZ,BOB,4\n");
        f.close();

        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.locate = [](const QString&) { return decolog::core::ContestStation{291, QStringLiteral("NA"), 5, 8}; };
        // La rete che distribuisce i numeri: da' il 100.
        int asked = 0;
        ctx.takeSerial = [&asked](int) { ++asked; return 100; };
        bool shared = false;
        ctx.shareSerials = [&shared](bool on) { shared = on; };
        ActivationController act(std::move(ctx));
        act.load();

        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"}, {"serialEnabled", true},
                           {"sharedSerial", true}}).isEmpty());
        QVERIFY(shared);
        // Senza call history: la zona dal paese.
        QCOMPARE(act.suggestExchange("K9ZZZ").value("from").toString(), QString("cty"));
        QVERIFY(act.loadCallHistory(QUrl::fromLocalFile(file)).isEmpty());
        QCOMPARE(act.state().value("callHistoryCount").toInt(), 1);
        // Con: la zona che ha mandato l'anno scorso.
        const QVariantMap s = act.suggestExchange("K9ZZZ");
        QCOMPARE(s.value("value").toString(), QString("4"));
        QCOMPARE(s.value("from").toString(), QString("history"));

        // Il numero lo da' la rete, e il contatore riparte da li'.
        AdifRecord r{{"CALL", "K9ZZZ"}, {"QSO_DATE", "20261024"}, {"TIME_ON", "1200"}, {"BAND", "20m"}, {"MODE", "CW"}};
        act.applyTo(r);
        QCOMPARE(r.value("STX"), QString("100"));
        QCOMPARE(asked, 1);
        act.qsoLogged();
        QCOMPARE(act.nextSerial(), 101);

        // Le regole di questa edizione.
        QCOMPARE(act.editionRules().value("bands").toStringList().size(), 6);
        QVariantMap state = act.state();
        state["rules"] = QVariantMap{{"bands", QStringList{"40m", "20m"}}};
        act.update(state);
        QCOMPARE(act.editionRules().value("bands").toStringList(), QStringList({"40m", "20m"}));
        QVERIFY(act.editionRules().value("changed").toBool());
        // Il call history resta dopo un cambio della sessione.
        QCOMPARE(act.state().value("callHistoryCount").toInt(), 1);
        act.stop();
        QVERIFY(!shared);
    }

    void theScoreReadsTheExchangeAndFollowsTheLog()
    {
        // Il punteggio adesso si calcola una volta e si tiene in memoria: la
        // prova guarda che lo scambio ricevuto arrivi al conto (sta fra i campi
        // ADIF in piu', non in una colonna) e che un QSO corretto fuori dalla
        // sessione faccia rifare il conto invece di lasciarlo vecchio.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.station = [] { return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28}; };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "IARU-HF"}}).isEmpty());

        auto log = [&db, &act](const char* call, int dxcc, const char* cont, int ituz, const char* srx) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")},
                         {"TIME_ON", QDateTime::currentDateTimeUtc().toString("hhmmss")},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", QString::number(dxcc)},
                         {"CONT", cont}, {"ITUZ", QString::number(ituz)},
                         {"SRX", srx}, {"SRX_STRING", srx}};
            act.applyTo(r);
            const auto res = db.insertQso(r, "manual", {}, true);
            if (res.status == InsertResult::Status::Inserted)
                act.qsoLogged();
            return res;
        };

        // Una stazione HQ: si riconosce solo dallo scambio, "DARC" al posto
        // della zona. Se lo scambio non arrivasse al conto, varrebbe 3 punti
        // (stesso continente, altra zona) invece di 1, e porterebbe una zona
        // invece del moltiplicatore HQ.
        const auto hq = log("DA0HQ", 230, "EU", 28, "DARC");
        QCOMPARE(hq.status, InsertResult::Status::Inserted);
        QVariantMap score = act.score();
        QCOMPARE(score.value("points").toInt(), 1);
        QCOMPARE(score.value("multipliers").toInt(), 1);

        // Un americano: 5 punti e una zona nuova.
        QCOMPARE(log("W1AW", 291, "NA", 8, "8").status, InsertResult::Status::Inserted);
        score = act.score();
        QCOMPARE(score.value("points").toInt(), 6);
        QCOMPARE(score.value("multipliers").toInt(), 2);
        QCOMPARE(score.value("score").toInt(), 12);

        // Il QSO della HQ viene cancellato da un'altra parte del programma (la
        // tabella del log): la sessione non ne sa niente finche' non le si
        // dice che il log e' cambiato. Da li' il conto dev'essere quello nuovo.
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(QStringLiteral("UPDATE qso SET deleted = 1 WHERE id = %1").arg(hq.id)));
        act.invalidateScore();
        score = act.score();
        QCOMPARE(score.value("points").toInt(), 5);
        QCOMPARE(score.value("multipliers").toInt(), 1);
    }

    void theExchangeIsSuggestedWhileTypingTheCall()
    {
        // Si scrive il nominativo e lo scambio si riempie da solo: la zona dal
        // paese, o quello che la stazione ha mandato l'ultima volta. Il
        // progressivo non si puo' sapere, e non si inventa.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        // Un QSO di un anno fa con I2XYZ, nel 40/80: mando' MI.
        QVERIFY(db.insertQso({{"CALL", "I2XYZ"}, {"QSO_DATE", "20250105"}, {"TIME_ON", "0800"},
                              {"BAND", "40m"}, {"MODE", "SSB"}, {"DXCC", "248"},
                              {"CONTEST_ID", "ARI-40-80"}, {"SRX_STRING", "MI"}}, "import").status
                == InsertResult::Status::Inserted);
        // Uno con I8ABC, fuori dai contest, con la provincia nel campo STATE.
        QVERIFY(db.insertQso({{"CALL", "I8ABC"}, {"QSO_DATE", "20250301"}, {"TIME_ON", "0900"},
                              {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "248"}, {"STATE", "NA"}},
                             "import").status == InsertResult::Status::Inserted);
        // Una HQ nella IARU dell'anno scorso: mando' la sigla della societa'.
        QVERIFY(db.insertQso({{"CALL", "DA0HQ"}, {"QSO_DATE", "20250712"}, {"TIME_ON", "1300"},
                              {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "230"}, {"ITUZ", "28"},
                              {"CONTEST_ID", "IARU-HF"}, {"SRX_STRING", "DARC"}}, "import").status
                == InsertResult::Status::Inserted);

        // W3RT nel CQ WW RTTY: zona e stato. W6XX fuori dai contest, con lo STATE.
        QVERIFY(db.insertQso({{"CALL", "W3RT"}, {"QSO_DATE", "20250927"}, {"TIME_ON", "1000"},
                              {"BAND", "20m"}, {"MODE", "RTTY"}, {"DXCC", "291"},
                              {"CONTEST_ID", "CQ-WW-RTTY"}, {"SRX_STRING", "05 PA"}}, "import").status
                == InsertResult::Status::Inserted);
        QVERIFY(db.insertQso({{"CALL", "W6XX"}, {"QSO_DATE", "20250401"}, {"TIME_ON", "1000"},
                              {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "291"}, {"STATE", "CA"}},
                             "import").status == InsertResult::Status::Inserted);

        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.station = [] { return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28}; };
        ctx.locate = [](const QString& call) {
            if (call.startsWith(QStringLiteral("W")))
                return decolog::core::ContestStation{291, QStringLiteral("NA"), 5, 8};
            if (call.startsWith(QStringLiteral("I")))
                return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28};
            if (call.startsWith(QStringLiteral("D")))
                return decolog::core::ContestStation{230, QStringLiteral("EU"), 14, 28};
            return decolog::core::ContestStation{};
        };
        ActivationController act(std::move(ctx));
        act.load();

        // CQ WW RTTY: la zona e, per gli americani, lo stato: dalla gara
        // dell'anno scorso o dallo STATE del log. Un europeo solo la zona.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-RTTY"}}).isEmpty());
        QCOMPARE(act.suggestExchange(QStringLiteral("W3RT")).value("value").toString(), QString("5 PA"));
        QCOMPARE(act.suggestExchange(QStringLiteral("W6XX")).value("value").toString(), QString("5 CA"));
        QCOMPARE(act.suggestExchange(QStringLiteral("DL9ZZT")).value("value").toString(), QString("14"));
        act.stop();
        // E lo "05 PA" del RTTY vale zona 5 anche per il CQ WW in CW.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"}}).isEmpty());
        QCOMPARE(act.suggestExchange(QStringLiteral("W3RT")).value("value").toString(), QString("5"));
        act.stop();

        // CQ WW: la zona CQ dal paese.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"}}).isEmpty());
        QVariantMap s = act.suggestExchange(QStringLiteral("W1AW"));
        QCOMPARE(s.value("value").toString(), QString("5"));
        QCOMPARE(s.value("from").toString(), QString("cty"));
        act.stop();

        // IARU: la HQ ha mandato DARC, e DARC si suggerisce; un americano la
        // sua zona ITU.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "IARU-HF"}}).isEmpty());
        s = act.suggestExchange(QStringLiteral("DA0HQ"));
        QCOMPARE(s.value("value").toString(), QString("DARC"));
        QCOMPARE(s.value("from").toString(), QString("log"));
        QCOMPARE(act.suggestExchange(QStringLiteral("W1AW")).value("value").toString(), QString("8"));
        act.stop();

        // 40/80: la provincia di un QSO di gara, o quella del campo STATE.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "ARI-40-80"}}).isEmpty());
        s = act.suggestExchange(QStringLiteral("I2XYZ"));
        QCOMPARE(s.value("value").toString(), QString("MI"));
        QCOMPARE(s.value("from").toString(), QString("log"));
        QCOMPARE(act.suggestExchange(QStringLiteral("I8ABC")).value("value").toString(), QString("NA"));
        // Un italiano mai lavorato: non si sa, e non si indovina.
        QVERIFY(act.suggestExchange(QStringLiteral("I1NEW")).value("value").toString().isEmpty());
        act.stop();

        // WPX: il progressivo non si puo' sapere prima.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WPX-CW"}}).isEmpty());
        QVERIFY(act.suggestExchange(QStringLiteral("W1AW")).value("value").toString().isEmpty());
        act.stop();
    }

    void theContestKnowsWhatEachSpotIsWorth()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        // Io sono in Italia: Europa, zona CQ 15.
        ctx.station = [] { return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28}; };
        // Un cty.csv in miniatura: quel tanto che basta alla prova.
        ctx.locate = [](const QString& call) {
            if (call.startsWith(QStringLiteral("W")))
                return decolog::core::ContestStation{291, QStringLiteral("NA"), 5, 8};
            if (call.startsWith(QStringLiteral("JA")))
                return decolog::core::ContestStation{339, QStringLiteral("AS"), 25, 45};
            if (call.startsWith(QStringLiteral("DL")))
                return decolog::core::ContestStation{230, QStringLiteral("EU"), 14, 28};
            return decolog::core::ContestStation{};
        };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"},
                           {"serialEnabled", true}}).isEmpty());

        // Prima di lavorare qualcuno, ogni spot porta due moltiplicatori nuovi.
        const QVariantMap before = act.spotValue(QStringLiteral("W1AW"), QStringLiteral("20m"),
                                                 QStringLiteral("CW"));
        QCOMPARE(before.value("points").toInt(), 3);
        QCOMPARE(before.value("newMultiplier").toBool(), true);
        QCOMPARE(before.value("duplicate").toBool(), false);

        auto log = [&db, &act](const char* call, const char* band, const char* mode,
                               int dxcc, const char* cont, int cqz) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")},
                         {"TIME_ON", QDateTime::currentDateTimeUtc().toString("hhmmss")},
                         {"BAND", band}, {"MODE", mode}, {"DXCC", QString::number(dxcc)},
                         {"CONT", cont}, {"CQZ", QString::number(cqz)}};
            act.applyTo(r);
            const auto res = db.insertQso(r, "manual", {}, true);
            if (res.status == InsertResult::Status::Inserted)
                act.qsoLogged();
            return res;
        };
        QCOMPARE(log("W1AW", "20m", "CW", 291, "NA", 5).status, InsertResult::Status::Inserted);

        // Adesso il punteggio c'e': 3 punti, zona 5 e paese 291 sui 20 metri.
        const QVariantMap score = act.score();
        QCOMPARE(score.value("valid").toBool(), true);
        QCOMPARE(score.value("points").toInt(), 3);
        QCOMPARE(score.value("multipliers").toInt(), 2);
        QCOMPARE(score.value("score").toInt(), 6);

        // Un altro americano sulla stessa banda non porta piu' niente: stessa
        // zona, stesso paese. E' quello che il cluster deve smettere di segnare.
        const QVariantMap again = act.spotValue(QStringLiteral("W2XX"), QStringLiteral("20m"),
                                                QStringLiteral("CW"));
        QCOMPARE(again.value("points").toInt(), 3);
        QCOMPARE(again.value("newMultiplier").toBool(), false);
        // Ma sui 40 metri sono moltiplicatori nuovi: nel CQ WW si contano per
        // banda, ed e' per questo che si cambia banda.
        const QVariantMap lower = act.spotValue(QStringLiteral("W2XX"), QStringLiteral("40m"),
                                                QStringLiteral("CW"));
        QCOMPARE(lower.value("newMultiplier").toBool(), true);
        // Lo stesso nominativo gia' lavorato su quella banda e' un duplicato.
        const QVariantMap dupe = act.spotValue(QStringLiteral("W1AW"), QStringLiteral("20m"),
                                               QStringLiteral("CW"));
        QCOMPARE(dupe.value("duplicate").toBool(), true);

        // Il giapponese porta zona e paese nuovi, e vale 3 punti.
        const QVariantMap japan = act.spotValue(QStringLiteral("JA1ABC"), QStringLiteral("20m"),
                                                QStringLiteral("CW"));
        QCOMPARE(japan.value("points").toInt(), 3);
        QCOMPARE(japan.value("newMultiplier").toBool(), true);

        // Il tedesco e' nel mio continente: 1 punto.
        const QVariantMap german = act.spotValue(QStringLiteral("DL9ZZT"), QStringLiteral("20m"),
                                                 QStringLiteral("CW"));
        QCOMPARE(german.value("points").toInt(), 1);

        // E il contest e' in CW: la finestra della telegrafia serve.
        QVERIFY(act.isCwContest());
    }

    void aContestWithoutRulesHasNoScoreAndSaysIt()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "AL-QSO-PARTY"}}).isEmpty());

        const QVariantMap score = act.score();
        QCOMPARE(score.value("valid").toBool(), false);
        QCOMPARE(score.value("score").toInt(), 0);
        // E nessuno spot risulta moltiplicatore: meglio niente che un numero
        // inventato.
        QCOMPARE(act.spotValue(QStringLiteral("W1AW"), QStringLiteral("20m"),
                               QStringLiteral("SSB")).value("newMultiplier").toBool(), false);
        // Un contest in SSB non apre la finestra della telegrafia.
        QVERIFY(!act.isCwContest());
    }

    void activationReferencesRejectEmptyAndDuplicateItems()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ActivationController act(std::move(ctx));
        act.load();

        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,,IT-0002"}})
                    .contains(QStringLiteral("empty"), Qt::CaseInsensitive));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001, it-0001"}})
                    .contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", " it-0001, IT-0002 "}}).isEmpty());
        QCOMPARE(act.state().value(QStringLiteral("reference")).toString(),
                 QStringLiteral("IT-0001,IT-0002"));
        act.update({{"kind", "pota"}, {"reference", "IT-0001,,IT-0002"}});
        QCOMPARE(act.state().value(QStringLiteral("reference")).toString(),
                 QStringLiteral("IT-0001,IT-0002"));
    }

    void sessionOnTheLog()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QStringList notes;
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC/P"); };
        ctx.stationGrid = [] { return QStringLiteral("JN71DC"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.activity = [&notes](const QString&, const QString& text, const QString&) { notes << text; };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(!act.active());

        // Senza referenza non parte.
        QVERIFY(!act.start({{"kind", "pota"}}).isEmpty());
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "it-1234"}, {"serialEnabled", true}}).isEmpty());
        QVERIFY(act.active());
        QCOMPARE(act.requiredQsos(), 10);
        QCOMPARE(act.nextSerial(), 1);
        QCOMPARE(act.suggestedFileName().left(18), QString("IU8LMC_P@IT-1234-2"));

        auto log = [&db, &act](const char* call, const char* band, const char* mode) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")},
                         {"TIME_ON", QDateTime::currentDateTimeUtc().toString("hhmmss")},
                         {"BAND", band}, {"MODE", mode}};
            act.applyTo(r);
            const auto res = db.insertQso(r, "manual", {}, true);
            if (res.status == InsertResult::Status::Inserted)
                act.qsoLogged();
            return res;
        };

        QCOMPARE(log("K1ABC", "20m", "FT8").status, InsertResult::Status::Inserted);
        QCOMPARE(act.qsoCount(), 1);
        QCOMPARE(act.nextSerial(), 2);
        QCOMPARE(log("JA1XX", "20m", "FT8").status, InsertResult::Status::Inserted);
        QCOMPARE(act.qsoCount(), 2);
        QCOMPARE(act.uniqueCalls(), 2);

        // Dentro l'attivazione K1ABC su 20m FT8 e' un duplicato, su 40m no.
        QVERIFY(act.isDuplicate("k1abc", "20M", "FT8"));
        QVERIFY(!act.isDuplicate("K1ABC", "40m", "FT8"));
        QVERIFY(!act.isDuplicate("K1ABC", "20m", "CW"));

        // Il primo QSO ha la referenza e il numero.
        const auto first = db.record(db.qsosToUpload("lotw").first());
        QVERIFY(first);
        QCOMPARE(first->value("MY_SIG_INFO"), QString("IT-1234"));
        QCOMPARE(first->value("STX"), QString("1"));
        QCOMPARE(first->value("APP_DECOLOG_TAGS"), QString("pota"));

        QCOMPARE(act.qsoIds().size(), 2);
        QCOMPARE(act.perBand().size(), 1);

        // La sessione sopravvive alla chiusura del programma.
        ActivationController again(ActivationController::Context{&db, ctx.stationCall, ctx.stationGrid,
                                                                 ctx.activeProfileId, {}, {}});
        again.load();
        QVERIFY(again.active());
        QCOMPARE(again.session().reference, QString("IT-1234"));
        QCOMPARE(again.qsoCount(), 2);
        QCOMPARE(again.nextSerial(), 3);

        act.stop();
        QVERIFY(!act.active());
        QVERIFY(!act.isDuplicate("K1ABC", "20m", "FT8"));
        QVERIFY(notes.last().contains("2"));
    }
};

QTEST_GUILESS_MAIN(TestActivation)
#include "tst_activation.moc"
