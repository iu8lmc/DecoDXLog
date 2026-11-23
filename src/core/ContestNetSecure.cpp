#include "core/ContestNetSecure.h"
#include "monocypher.h"
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace decolog::core::contestsecure {
namespace {
constexpr int maxPlaintext=4096;
bool id(const QString& value) { static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$")); return re.match(value).hasMatch(); }
bool typeAllowed(const QString& type) { return type==QLatin1String("qso_chunk_v1") || type==QLatin1String("qso_first_v1") || type==QLatin1String("qso_missing_v1"); }
QByteArray aad(const QString& group,const QString& sender,const QString& recipient) {
    const auto normalized=group.trimmed().toCaseFolded().toUtf8();
    if (normalized.isEmpty() || normalized.size()>256 || !id(sender) || !id(recipient) || sender==recipient) return {};
    QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0);
    stream << QByteArrayLiteral("DecoDXLog-QSO-secure-v1") << normalized << sender.toUtf8() << recipient.toUtf8();
    return stream.status()==QDataStream::Ok?bytes:QByteArray{};
}
QByteArray decode(const QJsonValue& value,int maxEncoded) {
    if (!value.isString()) return {};
    const QByteArray encoded=value.toString().toLatin1(); if (encoded.isEmpty() || encoded.size()>maxEncoded) return {};
    const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64()!=encoded) return {};
    return decoded.decoded;
}
const uint8_t* ptr(const QByteArray& data) { return reinterpret_cast<const uint8_t*>(data.constData()); }
uint8_t* ptr(QByteArray& data) { return reinterpret_cast<uint8_t*>(data.data()); }
}
Sealed seal(const QByteArray& key,const QString& group,const QString& sender,const QString& recipient,const QString& type,const QJsonObject& body) {
    Sealed result; const auto associated=aad(group,sender,recipient);
    if (key.size()!=32 || associated.isEmpty() || !typeAllowed(type) || body.value(QStringLiteral("to")).toString()!=recipient) {
        result.error=QStringLiteral("Invalid secure frame parameters"); return result;
    }
    QByteArray plain=QJsonDocument(QJsonObject{{QStringLiteral("type"),type},{QStringLiteral("body"),body}}).toJson(QJsonDocument::Compact);
    if (plain.isEmpty() || plain.size()>maxPlaintext) { result.error=QStringLiteral("Secure frame exceeds size limit"); return result; }
    QByteArray nonce(24,Qt::Uninitialized); for (char& c:nonce) c=char(QRandomGenerator::system()->bounded(256));
    QByteArray cipher(plain.size(),Qt::Uninitialized), tag(16,Qt::Uninitialized);
    crypto_aead_lock(ptr(cipher),ptr(tag),ptr(key),ptr(nonce),ptr(associated),size_t(associated.size()),ptr(plain),size_t(plain.size()));
    crypto_wipe(plain.data(),size_t(plain.size()));
    result.frame={{QStringLiteral("v"),1},{QStringLiteral("to"),recipient},{QStringLiteral("nonce"),QString::fromLatin1(nonce.toBase64())},
                  {QStringLiteral("box"),QString::fromLatin1(cipher.toBase64())},{QStringLiteral("tag"),QString::fromLatin1(tag.toBase64())}};
    result.ok=true; return result;
}
Opened open(const QByteArray& key,const QString& group,const QString& sender,const QString& recipient,const QJsonObject& frame) {
    Opened result; const auto associated=aad(group,sender,recipient);
    if (key.size()!=32 || associated.isEmpty() || frame.value(QStringLiteral("v"))!=QJsonValue(1) || frame.value(QStringLiteral("to")).toString()!=recipient) {
        result.error=QStringLiteral("Invalid secure frame routing"); return result;
    }
    const auto nonce=decode(frame.value(QStringLiteral("nonce")),32), tag=decode(frame.value(QStringLiteral("tag")),24), cipher=decode(frame.value(QStringLiteral("box")),((maxPlaintext+2)/3)*4);
    if (nonce.size()!=24 || tag.size()!=16 || cipher.isEmpty() || cipher.size()>maxPlaintext) { result.error=QStringLiteral("Invalid secure frame encoding"); return result; }
    QByteArray plain(cipher.size(),Qt::Uninitialized);
    if (crypto_aead_unlock(ptr(plain),ptr(tag),ptr(key),ptr(nonce),ptr(associated),size_t(associated.size()),ptr(cipher),size_t(cipher.size()))!=0) {
        crypto_wipe(plain.data(),size_t(plain.size())); result.error=QStringLiteral("Secure frame authentication failed"); return result;
    }
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(plain,&error); crypto_wipe(plain.data(),size_t(plain.size()));
    if (error.error!=QJsonParseError::NoError || !doc.isObject()) { result.error=QStringLiteral("Invalid secure frame payload"); return result; }
    const auto object=doc.object(); const auto type=object.value(QStringLiteral("type")).toString();
    if (!typeAllowed(type) || !object.value(QStringLiteral("body")).isObject() || object.value(QStringLiteral("body")).toObject().value(QStringLiteral("to")).toString()!=recipient) {
        result.error=QStringLiteral("Invalid secure frame contents"); return result;
    }
    result.type=type; result.body=object.value(QStringLiteral("body")).toObject(); result.ok=true; return result;
}
}
