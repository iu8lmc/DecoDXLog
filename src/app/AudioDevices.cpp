#include "app/AudioDevices.h"

#include <QHash>
#include <QMediaDevices>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

qint16 readSample(const char* source, QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return static_cast<qint16>((static_cast<int>(static_cast<unsigned char>(*source)) - 128) * 256);
    case QAudioFormat::Int16: {
        qint16 value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    case QAudioFormat::Int32: {
        qint32 value = 0;
        std::memcpy(&value, source, sizeof(value));
        return static_cast<qint16>(value / 65536);
    }
    case QAudioFormat::Float: {
        float value = 0;
        std::memcpy(&value, source, sizeof(value));
        if (!std::isfinite(value))
            return 0;
        value = std::clamp(value, -1.0f, 1.0f);
        return static_cast<qint16>(std::lround(value * (value < 0 ? 32768.0f : 32767.0f)));
    }
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
        break;
    }
    return 0;
}

QString sampleFormatName(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8: return QStringLiteral("unsigned 8-bit");
    case QAudioFormat::Int16: return QStringLiteral("signed 16-bit");
    case QAudioFormat::Int32: return QStringLiteral("signed 32-bit");
    case QAudioFormat::Float: return QStringLiteral("float");
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

} // namespace

namespace decolog::app::audiodev {

bool canConvertToMonoInt16(const QAudioFormat& format)
{
    if (!format.isValid() || format.sampleRate() <= 0 || format.channelCount() <= 0
        || format.bytesPerFrame() <= 0 || format.bytesPerSample() <= 0)
        return false;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
    case QAudioFormat::Int16:
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        return true;
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
        return false;
    }
    return false;
}

MonoPcm convertToMonoInt16(const QByteArray& input, const QAudioFormat& format)
{
    MonoPcm result;
    if (!canConvertToMonoInt16(format))
        return result;

    const int frameBytes = format.bytesPerFrame();
    const int sampleBytes = format.bytesPerSample();
    const int channels = format.channelCount();
    const qsizetype frames = input.size() / frameBytes;
    if (frames <= 0)
        return result;

    result.samples.resize(frames * static_cast<qsizetype>(sizeof(qint16)));
    const char* in = input.constData();
    char* out = result.samples.data();
    for (qsizetype frame = 0; frame < frames; ++frame) {
        qint64 sum = 0;
        const char* source = in + frame * frameBytes;
        for (int channel = 0; channel < channels; ++channel)
            sum += readSample(source + channel * sampleBytes, format.sampleFormat());
        const qint16 mixed = static_cast<qint16>(sum / channels);
        std::memcpy(out + frame * sizeof(qint16), &mixed, sizeof(mixed));
    }
    result.consumedBytes = frames * frameBytes;
    return result;
}

QString formatDescription(const QAudioFormat& format)
{
    if (!format.isValid())
        return QStringLiteral("unknown format");
    const QString channels = format.channelCount() == 1 ? QStringLiteral("mono")
                                                         : QStringLiteral("%1 channels").arg(format.channelCount());
    return QStringLiteral("%1 Hz · %2 · %3")
        .arg(format.sampleRate())
        .arg(channels, sampleFormatName(format.sampleFormat()));
}

Resolution resolve(const QList<Entry>& entries, const Saved& saved)
{
    if (saved.id.isEmpty() && saved.name.isEmpty())
        return {Resolution::SystemDefault, -1};

    // Prima l'identificativo: e' quello che non cambia quando Windows rinomina
    // la scheda.
    if (!saved.id.isEmpty()) {
        for (int i = 0; i < entries.size(); ++i) {
            if (QString::fromUtf8(entries.at(i).id) == saved.id)
                return {Resolution::Found, i};
        }
    }
    // Poi il nome com'era scritto, ma solo se non lascia dubbi.
    QList<int> byName;
    if (!saved.name.isEmpty()) {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).name == saved.name)
                byName << i;
        }
    }
    if (byName.size() == 1)
        return {Resolution::Found, byName.first()};
    if (byName.size() > 1)
        return {Resolution::Ambiguous, byName.first()};
    return {Resolution::Missing, -1};
}

QStringList labels(const QList<Entry>& entries)
{
    QStringList out;
    QHash<QString, int> seen;
    for (const Entry& e : entries) {
        const int n = ++seen[e.name];
        out << (n == 1 ? e.name : QStringLiteral("%1 (%2)").arg(e.name).arg(n));
    }
    return out;
}

int comboIndex(const Resolution& resolution)
{
    switch (resolution.kind) {
    case Resolution::SystemDefault:
        return 0;
    case Resolution::Found:
    case Resolution::Ambiguous:
        return resolution.index + 1;
    case Resolution::Missing:
        return -1;
    }
    return -1;
}

QList<QAudioDevice> inputDevices()
{
    return QMediaDevices::audioInputs();
}

QList<QAudioDevice> outputDevices()
{
    return QMediaDevices::audioOutputs();
}

QList<Entry> entriesOf(const QList<QAudioDevice>& devices)
{
    QList<Entry> out;
    out.reserve(devices.size());
    for (const QAudioDevice& d : devices)
        out.append(Entry{d.id(), d.description()});
    return out;
}

QVariantList deviceList(const QList<QAudioDevice>& devices)
{
    const QList<Entry> entries = entriesOf(devices);
    const QStringList shown = labels(entries);
    QVariantList out;
    for (int i = 0; i < entries.size(); ++i) {
        out << QVariantMap{{QStringLiteral("id"), QString::fromUtf8(entries.at(i).id)},
                           {QStringLiteral("name"), entries.at(i).name},
                           {QStringLiteral("label"), shown.at(i)}};
    }
    return out;
}

Saved savedFor(const QList<QAudioDevice>& devices, int comboRow)
{
    if (comboRow <= 0 || comboRow > devices.size())
        return {};
    const QAudioDevice& d = devices.at(comboRow - 1);
    return Saved{QString::fromUtf8(d.id()), d.description()};
}

} // namespace decolog::app::audiodev
