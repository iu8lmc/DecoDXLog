#include "core/DxCampaign.h"
#include "core/LogDatabase.h"
#include <QTemporaryDir>
#include <QTest>
using namespace decolog::core;
class TestDxCampaign : public QObject {
    Q_OBJECT
private slots:
    void matrixRespectsProfileAndRejectsInvalidChanges()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); StationProfile a; a.name="A"; a.stationCallsign="9H1AA"; const qint64 first=db.saveStationProfile(a);
        StationProfile b; b.name="B"; b.stationCallsign="9H1BB"; const qint64 second=db.saveStationProfile(b); QVERIFY(first>0 && second>0);
        auto add=[&](const char* call,qint64 profile) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import",{},false,profile).id; };
        QVERIFY(add("ZZ1AAA",first)>0); QVERIFY(add("ZZ1BBB",second)>0);
        dxcampaign::Campaign c; c.title="Trip"; c.calls={"ZZ1AAA","ZZ1BBB"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,7); c.source="fixture";
        c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.stationProfileId=first;
        QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,1);
        c.stationProfileId=second; QVERIFY(dxcampaign::save(db,c,&error)); QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,1);
        c.calls << "ZZ1BBB"; QVERIFY(!dxcampaign::save(db,c,&error)); QVERIFY(!error.isEmpty()); QCOMPARE(dxcampaign::list(db,&error).size(),1);
        const auto stored=dxcampaign::load(db,c.id); QVERIFY(stored); QCOMPARE(stored->revision,2); QCOMPARE(stored->calls.size(),2);
    }
    void independentPeriodsMultiCallConfirmationAndRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"); QString id;
        {
            LogDatabase db; QVERIFY(db.open(path));
            dxcampaign::Campaign first; first.title="Spring"; first.calls={"ZZ1AAA","ZZ1BBB"}; first.start=QDate(2026,1,1); first.end=QDate(2026,1,7); first.source="fixture"; first.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); first.bands={"20m","40m"}; first.modes={"CW","FT8"};
            QString error; QVERIFY2(dxcampaign::save(db,first,&error),qPrintable(error)); id=first.id; QCOMPARE(first.revision,1);
            auto second=first; second.id.clear(); second.revision=0; second.title="Autumn"; second.start=QDate(2026,10,1); second.end=QDate(2026,10,7); QVERIFY(dxcampaign::save(db,second,&error)); QVERIFY(second.id!=id);
            auto add=[&](const char* call,const char* date,const char* band,const char* mode) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND",band},{"MODE",mode}},"import").id; };
            QVERIFY(add("ZZ1AAA","20260102","20m","CW")>0); const qint64 b=add("ZZ1BBB","20260103","40m","FT8"); QVERIFY(b>0);
            QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","000000"},{"BAND","20m"},{"MODE","FT8"}},"import").id>0);
            QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260108"},{"TIME_ON","000000"},{"BAND","20m"},{"MODE","FT8"}},"import").id>0);
            QVERIFY(add("ZZ1AAA","20261002","20m","FT8")>0); QVERIFY(add("ZZ1AAA","20260108","40m","CW")>0);
            auto matrix=dxcampaign::matrix(db,first,&error); QCOMPARE(matrix.size(),4);
            QCOMPARE(matrix[0].worked,1); QCOMPARE(matrix[0].confirmed,0); QCOMPARE(matrix[1].worked,1); QCOMPARE(matrix[3].worked,1);
            QslState card; card.service="card"; card.rcvd="Y"; QVERIFY(db.setCardState(b,card));
            matrix=dxcampaign::matrix(db,first,&error); QCOMPARE(matrix[3].confirmed,1);
            auto autumn=dxcampaign::matrix(db,second,&error); QCOMPARE(autumn[1].worked,1); QCOMPARE(autumn[0].worked,0);
            auto stale=first; first.end=QDate(2026,1,8); QVERIFY(dxcampaign::save(db,first,&error)); QCOMPARE(first.revision,2);
            QVERIFY(!dxcampaign::save(db,stale,&error)); QCOMPARE(dxcampaign::matrix(db,first,&error)[2].worked,1);
            first.archived=true; QVERIFY(dxcampaign::save(db,first,&error)); QVERIFY(dxcampaign::load(db,id)->archived);
            first.archived=false; QVERIFY(dxcampaign::save(db,first,&error));
        }
        LogDatabase reopened; QVERIFY(reopened.open(path)); const auto restored=dxcampaign::load(reopened,id); QVERIFY(restored); QCOMPARE(restored->calls.size(),2); QCOMPARE(restored->revision,4); QVERIFY(!restored->archived);
    }
};
QTEST_GUILESS_MAIN(TestDxCampaign)
#include "tst_dxcampaign.moc"
