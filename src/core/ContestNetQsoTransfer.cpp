#include "core/ContestNetQsoTransfer.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
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
}
