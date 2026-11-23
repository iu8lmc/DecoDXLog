#include "core/ContestNetQsoTransfer.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QDataStream>
#include <QIODevice>
#include <QSqlRecord>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

namespace decolog::core::contestqso {
namespace {
constexpr int chunkSize=512, maxBytes=4*1024*1024;
bool id(const QString& s) { static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$")); return re.match(s).hasMatch(); }
bool hashText(const QString& s) { static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$")); return re.match(s).hasMatch(); }
bool number(const QJsonValue& v, int min, int max, int* out) {
    if (!v.isDouble()) return false;
    const double n=v.toDouble(); if (!std::isfinite(n) || std::floor(n)!=n || n<min || n>max) return false;
    *out=int(n); return true;
}
bool validPayload(const QByteArray& data,const QByteArray& digest) {
    const auto doc=adif::parse(data);
    return doc.records.size()==1 && ContestNetInventory::fingerprint(doc.records.first())==digest;
}
// Local-only guard. Attachment blobs are read in bounded SQL slices.
// Run within a transaction so metadata and slices share one snapshot.
QByteArray localStateFingerprint(const QSqlDatabase& db,qint64 id) {
    const QStringList statements{
        QStringLiteral("SELECT * FROM qso WHERE id=?"),
        QStringLiteral("SELECT service,sent,sent_date,rcvd,rcvd_date,remote_id,last_error,via FROM qsl_status WHERE qso_id=? ORDER BY service"),
        QStringLiteral("SELECT x.attachment_id,x.link_token,x.link_revision,a.sha256,a.filename,a.mime,a.size,a.created_at,length(a.data) FROM qsl_attachment_qso x LEFT JOIN qsl_attachment a ON a.id=x.attachment_id WHERE x.qso_id=? ORDER BY x.attachment_id"),
        QStringLiteral("SELECT id,field,previous_value,value,source,kind,acquired_at,valid_from,valid_to FROM qso_enrichment WHERE qso_id=? ORDER BY id")
    };
    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(QByteArrayLiteral("contestnet-local-review-v1"));
    for (int index=0;index<statements.size();++index) {
        QSqlQuery query(db); if (!query.prepare(statements[index])) return {};
        query.addBindValue(id); if (!query.exec()) return {};
        int count=0; QByteArray marker; QDataStream head(&marker,QIODevice::WriteOnly); head.setVersion(QDataStream::Qt_6_0); head << index; hash.addData(marker);
        const int columns=query.record().count();
        while (query.next()) {
            QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0); stream << columns;
            for (int column=0;column<columns;++column) stream << query.value(column);
            if (stream.status()!=QDataStream::Ok) return {}; hash.addData(bytes); ++count;
        }
        if (query.lastError().isValid() || (index==0 && count!=1)) return {};
        marker.clear(); QDataStream tail(&marker,QIODevice::WriteOnly); tail.setVersion(QDataStream::Qt_6_0); tail << count; hash.addData(marker);
    }
    QSqlQuery attachments(db);
    if (!attachments.prepare(QStringLiteral("SELECT a.id,length(a.data),typeof(a.data) FROM qsl_attachment_qso x LEFT JOIN qsl_attachment a ON a.id=x.attachment_id WHERE x.qso_id=? ORDER BY x.attachment_id"))) return {};
    attachments.addBindValue(id); if (!attachments.exec()) return {};
    QSqlQuery chunk(db); if (!chunk.prepare(QStringLiteral("SELECT substr(data,?,?) FROM qsl_attachment WHERE id=?"))) return {};
    while (attachments.next()) {
        if (attachments.value(0).isNull() || attachments.value(1).isNull() || attachments.value(2).toString()!=QLatin1String("blob")) return {};
        const qint64 attachment=attachments.value(0).toLongLong(), size=attachments.value(1).toLongLong();
        if (size<0) return {};
        QByteArray marker; QDataStream stream(&marker,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0); stream << attachment << size;
        if (stream.status()!=QDataStream::Ok) return {}; hash.addData(marker);
        for (qint64 offset=0;offset<size;) {
            const qint64 length=qMin<qint64>(1024*1024,size-offset);
            chunk.bindValue(0,offset+1); chunk.bindValue(1,length); chunk.bindValue(2,attachment);
            if (!chunk.exec() || !chunk.next()) return {};
            const QByteArray bytes=chunk.value(0).toByteArray();
            if (bytes.size()!=length) return {};
            hash.addData(bytes); chunk.finish(); offset+=length;
        }
    }
    if (attachments.lastError().isValid()) return {};
    return hash.result();
}
}
Encoder::Encoder(const QString& transfer,const QString& log,const ContestNetInventoryRow& row)
    : m_transfer(transfer),m_log(log),m_row(row) {
    if (!id(transfer)||!id(log)||!id(row.uuid)||row.revision<0 || row.adifPayload.isEmpty() || row.adifPayload.size()>maxBytes || row.contentHash.size()!=32 || !validPayload(row.adifPayload,row.contentHash)) {
        m_error=QStringLiteral("Invalid transfer metadata, size or ADIF fingerprint"); return;
    }
    m_payloadHash=QCryptographicHash::hash(row.adifPayload,QCryptographicHash::Sha256);
}
int Encoder::count() const { return ok() ? int((m_row.adifPayload.size()+chunkSize-1)/chunkSize) : 0; }
QJsonObject Encoder::frame(int index) const {
    if (!ok() || index<0 || index>=count()) return {};
    return {{"version",1},{"transfer",m_transfer},{"log",m_log},{"uuid",m_row.uuid},{"revision",m_row.revision},{"deleted",m_row.deleted},
            {"fingerprint",QString::fromLatin1(m_row.contentHash.toHex())},{"sha256",QString::fromLatin1(m_payloadHash.toHex())},
            {"size",int(m_row.adifPayload.size())},{"count",count()},{"index",index},
            {"data",QString::fromLatin1(m_row.adifPayload.mid(index*chunkSize,chunkSize).toBase64())}};
}
QList<QJsonObject> Encoder::framesForRequest(const QJsonObject& request) const {
    if (!ok() || request.value("version")!=QJsonValue(1) || request.value("transfer").toString()!=m_transfer
        || request.value("sha256").toString()!=QString::fromLatin1(m_payloadHash.toHex()) || !request.value("indices").isArray()) return {};
    const auto indices=request.value("indices").toArray();
    if (indices.isEmpty() || indices.size()>32) return {};
    QList<QJsonObject> frames; int previous=-1;
    for (const auto& value:indices) {
        int index; if (!number(value,0,count()-1,&index) || index<=previous) return {};
        previous=index; frames.append(frame(index));
    }
    return frames;
}
QJsonObject Receiver::missingRequest(int fromIndex) const {
    if (m_failed || m_complete || m_chunks.isEmpty() || fromIndex<0 || fromIndex>=m_chunks.size()) return {};
    QJsonArray indices;
    for (int i=fromIndex;i<m_chunks.size() && indices.size()<32;++i) if (m_chunks[i].isEmpty()) indices.append(i);
    if (indices.isEmpty()) return {};
    return {{"version",1},{"transfer",m_transfer},{"sha256",QString::fromLatin1(m_payloadHash.toHex())},{"indices",indices}};
}
bool Receiver::accept(const QString& sender,const QJsonObject& f) {
    auto reject=[this](const char* why) { m_error=QString::fromLatin1(why); return false; };
    if (m_failed) return reject("Transfer has failed; restart required");
    int version,total,count,index,revision;
    const QString transfer=f.value("transfer").toString(),log=f.value("log").toString(),uuid=f.value("uuid").toString();
    const QString fp=f.value("fingerprint").toString(),sha=f.value("sha256").toString();
    if (!id(m_sender)||!id(m_transfer)||sender!=m_sender||transfer!=m_transfer||!id(log)||!id(uuid)||!hashText(fp)||!hashText(sha)
        || !number(f.value("version"),1,1,&version)||!number(f.value("size"),1,maxBytes,&total)
        || !number(f.value("count"),1,maxBytes/chunkSize,&count)||count!=(total+chunkSize-1)/chunkSize
        || !number(f.value("index"),0,count-1,&index)||!number(f.value("revision"),0,INT_MAX,&revision)
        || !f.value("deleted").isBool()||!f.value("data").isString()) return reject("Invalid or misrouted fragment");
    const QByteArray encoded=f.value("data").toString().toLatin1();
    if (encoded.size()>((chunkSize+2)/3)*4) return reject("Fragment is too large");
    const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64()!=encoded || decoded.decoded.size()!=qMin(chunkSize,total-index*chunkSize)) return reject("Invalid fragment data");
    const QByteArray contentHash=QByteArray::fromHex(fp.toLatin1()),payloadHash=QByteArray::fromHex(sha.toLatin1());
    if (m_chunks.isEmpty()) {
        m_total=total; m_log=log; m_row.uuid=uuid; m_row.revision=revision; m_row.deleted=f.value("deleted").toBool();
        m_row.contentHash=contentHash; m_payloadHash=payloadHash; m_chunks.resize(count);
    } else if (m_total!=total || m_chunks.size()!=count || m_log!=log || m_row.uuid!=uuid || m_row.revision!=revision
               || m_row.deleted!=f.value("deleted").toBool() || m_row.contentHash!=contentHash || m_payloadHash!=payloadHash) return reject("Fragment metadata changed");
    if (!m_chunks[index].isEmpty()) {
        if (m_chunks[index]!=decoded.decoded) return reject("Conflicting duplicate fragment");
        m_error.clear(); return true;
    }
    m_chunks[index]=decoded.decoded; ++m_received;
    if (m_received==count) {
        QByteArray data; data.reserve(total); for (const auto& part:m_chunks) data+=part;
        if (QCryptographicHash::hash(data,QCryptographicHash::Sha256)!=m_payloadHash || !validPayload(data,m_row.contentHash)) {
            m_failed=true; return reject("Transfer integrity check failed");
        }
        m_row.adifPayload=data; m_complete=true;
    }
    m_error.clear(); return true;
}
ApplyResult applyMissing(LogDatabase& db,const QString& expectedLogIdentity,const Receiver& receiver) {
    ApplyResult result;
    if (!receiver.complete() || !db.isOpen() || expectedLogIdentity.isEmpty() || db.identity()!=expectedLogIdentity || !db.fileIdentityMatchesOpen()) {
        result.error=QStringLiteral("Unverified transfer or changed destination log"); return result;
    }
    const auto incoming=receiver.row();
    if (incoming.deleted) { result.error=QStringLiteral("Deleted QSO require conflict review"); return result; }
    const auto doc=adif::parse(incoming.adifPayload);
    if (doc.records.size()!=1 || ContestNetInventory::fingerprint(doc.records.first())!=incoming.contentHash) {
        result.error=QStringLiteral("Invalid transferred ADIF"); return result;
    }
    auto connection=db.connection(); QSqlQuery q(connection);
    if (!q.exec(QStringLiteral("BEGIN IMMEDIATE"))) { result.error=q.lastError().text(); return result; }
    auto fail=[&](const QString& message) { connection.rollback(); ApplyResult failed; failed.error=message; return failed; };
    if (db.identity()!=expectedLogIdentity || !db.fileIdentityMatchesOpen()) return fail(QStringLiteral("Destination log changed"));
    q.prepare(QStringLiteral("SELECT id FROM qso WHERE uuid=?")); q.addBindValue(incoming.uuid);
    if (!q.exec()) return fail(q.lastError().text());
    if (q.next()) {
        const auto existing=q.value(0).toLongLong(); QString error;
        const auto metadata=db.meta(existing); const auto record=db.recordChecked(existing,&error);
        if (!metadata || !record || metadata->deleted || metadata->revision!=incoming.revision || ContestNetInventory::fingerprint(*record)!=incoming.contentHash)
            return fail(error.isEmpty()?QStringLiteral("Existing QSO differs; review required"):error);
        if (!connection.commit()) return fail(connection.lastError().text());
        result.ok=true; result.id=existing; return result;
    }
    if (q.lastError().isValid()) return fail(q.lastError().text());
    if (!q.exec(QStringLiteral("SELECT COALESCE(MAX(sequence),0) FROM contest_net_change")) || !q.next()) return fail(QStringLiteral("Cannot read change journal"));
    const qint64 watermark=q.value(0).toLongLong();
    const auto inserted=db.insertQso(doc.records.first(),QStringLiteral("contestnet"),receiver.sourceLog(),false,0,true);
    if (inserted.status!=InsertResult::Status::Inserted) return fail(inserted.message.isEmpty()?QStringLiteral("QSO cannot be inserted"):inserted.message);
    const auto temporary=db.meta(inserted.id);
    if (!temporary || temporary->uuid.isEmpty()) return fail(QStringLiteral("Cannot identify inserted QSO"));
    q.prepare(QStringLiteral("UPDATE qso SET uuid=?, revision=? WHERE id=?")); q.addBindValue(incoming.uuid); q.addBindValue(incoming.revision); q.addBindValue(inserted.id);
    if (!q.exec() || q.numRowsAffected()!=1) return fail(q.lastError().text().isEmpty()?QStringLiteral("Cannot preserve QSO identity"):q.lastError().text());
    // Hide the generated, never externally visible UUID from this transaction's
    // local change journal; the final identity still has an insertion event.
    q.prepare(QStringLiteral("DELETE FROM contest_net_change WHERE sequence>? AND qso_uuid=?"));
    q.addBindValue(watermark); q.addBindValue(temporary->uuid);
    if (!q.exec()) return fail(q.lastError().text());
    q.prepare(QStringLiteral("UPDATE contest_net_change SET previous_qso_uuid=NULL WHERE sequence>? AND qso_uuid=? AND previous_qso_uuid=?"));
    q.addBindValue(watermark); q.addBindValue(incoming.uuid); q.addBindValue(temporary->uuid);
    if (!q.exec() || q.numRowsAffected()!=1) return fail(QStringLiteral("Cannot finalize change journal"));
    QString error; const auto saved=db.recordChecked(inserted.id,&error);
    if (!saved || ContestNetInventory::fingerprint(*saved)!=incoming.contentHash) return fail(error.isEmpty()?QStringLiteral("Stored QSO differs from received ADIF"):error);
    if (!db.fileIdentityMatchesOpen() || db.identity()!=expectedLogIdentity) return fail(QStringLiteral("Destination log changed"));
    if (!connection.commit()) return fail(connection.lastError().text());
    result.ok=true; result.inserted=true; result.id=inserted.id; return result;
}

Review reviewExisting(LogDatabase& db,const QString& expected,const Receiver& receiver) {
    Review review;
    if (!receiver.complete() || !db.isOpen() || expected.isEmpty() || db.identity()!=expected || !db.fileIdentityMatchesOpen()) {
        review.error=QStringLiteral("Unverified transfer or changed destination log"); return review;
    }
    auto connection=db.connection(); QSqlQuery query(connection);
    if (!query.exec(QStringLiteral("BEGIN"))) { review.error=query.lastError().text(); return review; }
    auto fail=[&](const QString& error) { connection.rollback(); Review r; r.error=error; return r; };
    const auto incoming=receiver.row();
    query.prepare(QStringLiteral("SELECT id FROM qso WHERE uuid=?")); query.addBindValue(incoming.uuid);
    if (!query.exec()) return fail(query.lastError().text());
    if (!query.next()) return fail(QStringLiteral("Local QSO not found"));
    const auto id=query.value(0).toLongLong(); QString error;
    const auto meta=db.meta(id); const auto record=db.recordChecked(id,&error);
    if (!meta || !record) return fail(error.isEmpty()?QStringLiteral("Cannot read local QSO"):error);
    if (!db.fileIdentityMatchesOpen() || db.identity()!=expected) return fail(QStringLiteral("Destination log changed"));
    review.logIdentity=expected; review.fileIdentity=db.openFileIdentity(); review.sourceLog=receiver.sourceLog(); review.uuid=incoming.uuid;
    review.id=id; review.localRevision=meta->revision; review.localDeleted=meta->deleted; review.localHash=ContestNetInventory::fingerprint(*record);
    review.localStateHash=localStateFingerprint(connection,id);
    if (review.localStateHash.isEmpty()) return fail(QStringLiteral("Cannot inspect complete local QSO state"));
    review.remoteRevision=incoming.revision; review.remoteDeleted=incoming.deleted; review.remoteHash=incoming.contentHash;
    review.remotePayloadHash=QCryptographicHash::hash(incoming.adifPayload,QCryptographicHash::Sha256);
    if (!connection.commit()) return fail(connection.lastError().text());
    review.ok=true; return review;
}
ApplyResult applyReviewed(LogDatabase& db,const QString& expected,const Receiver& receiver,const Review& review) {
    ApplyResult result;
    if (!review.ok || !receiver.complete() || !db.isOpen() || expected.isEmpty() || review.logIdentity!=expected
        || db.identity()!=expected || !db.fileIdentityMatchesOpen() || db.openFileIdentity()!=review.fileIdentity) {
        result.error=QStringLiteral("Review, transfer or destination log changed"); return result;
    }
    const auto incoming=receiver.row();
    if (receiver.sourceLog()!=review.sourceLog || incoming.uuid!=review.uuid || incoming.revision!=review.remoteRevision
        || incoming.deleted!=review.remoteDeleted || incoming.contentHash!=review.remoteHash
        || QCryptographicHash::hash(incoming.adifPayload,QCryptographicHash::Sha256)!=review.remotePayloadHash) {
        result.error=QStringLiteral("Received QSO changed since review"); return result;
    }
    if (review.localDeleted) { result.error=QStringLiteral("Restoring deleted QSO requires separate review"); return result; }
    auto connection=db.connection(); QSqlQuery q(connection);
    if (!q.exec(QStringLiteral("BEGIN IMMEDIATE"))) { result.error=q.lastError().text(); return result; }
    auto fail=[&](const QString& message) { connection.rollback(); ApplyResult r; r.error=message; return r; };
    if (db.identity()!=expected || !db.fileIdentityMatchesOpen()) return fail(QStringLiteral("Destination log changed"));
    QString error; const auto meta=db.meta(review.id); const auto local=db.recordChecked(review.id,&error);
    if (!meta || !local || meta->uuid!=review.uuid || meta->revision!=review.localRevision || meta->deleted!=review.localDeleted
        || ContestNetInventory::fingerprint(*local)!=review.localHash || review.localStateHash.isEmpty()
        || localStateFingerprint(connection,review.id)!=review.localStateHash)
        return fail(error.isEmpty()?QStringLiteral("Local QSO changed since review"):error);
    if (incoming.deleted) {
        if (!db.softDeleteQso(review.id)) return fail(QStringLiteral("Cannot delete QSO"));
    } else {
        const auto parsed=adif::parse(incoming.adifPayload);
        if (parsed.records.size()!=1 || ContestNetInventory::fingerprint(parsed.records.first())!=incoming.contentHash) return fail(QStringLiteral("Invalid transferred ADIF"));
        const auto update=db.updateQso(review.id,parsed.records.first(),-1,QStringLiteral("contestnet review"));
        if (update.status!=InsertResult::Status::Inserted) return fail(update.message.isEmpty()?QStringLiteral("Cannot update QSO"):update.message);
        const auto saved=db.recordChecked(review.id,&error);
        if (!saved || ContestNetInventory::fingerprint(*saved)!=incoming.contentHash) return fail(error.isEmpty()?QStringLiteral("Stored QSO differs from reviewed ADIF"):error);
    }
    if (db.identity()!=expected || !db.fileIdentityMatchesOpen()) return fail(QStringLiteral("Destination log changed"));
    if (!connection.commit()) return fail(connection.lastError().text());
    result.ok=true; result.id=review.id; return result;
}

}
