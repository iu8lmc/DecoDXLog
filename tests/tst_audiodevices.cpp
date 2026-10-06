// Scegliere una scheda audio in modo preciso: ritrovarla per identificativo o
// per nome, e non ripiegare mai in silenzio su un'altra.
#include "app/AudioDevices.h"

#include <QAudioFormat>
#include <QTest>

#include <array>
#include <cstring>
#include <limits>

using namespace decolog::app::audiodev;

namespace {

QList<Entry> cards()
{
    return {
        {"{0.0.1.00000000}.{aaa}", "Microfono (Realtek Audio)"},
        {"{0.0.1.00000000}.{bbb}", "Microfono (2- USB Audio CODEC )"},
        {"{0.0.1.00000000}.{ccc}", "Gruppo microfoni (USB Audio CODEC)"},
    };
}

QAudioFormat format(int sampleRate, int channels, QAudioFormat::SampleFormat sampleFormat)
{
    QAudioFormat out;
    out.setSampleRate(sampleRate);
    out.setChannelCount(channels);
    out.setSampleFormat(sampleFormat);
    return out;
}

QByteArray int16Bytes(std::initializer_list<qint16> samples)
{
    QByteArray out(static_cast<qsizetype>(samples.size() * sizeof(qint16)), Qt::Uninitialized);
    std::memcpy(out.data(), samples.begin(), static_cast<std::size_t>(out.size()));
    return out;
}

QList<qint16> unpackInt16(const QByteArray& bytes)
{
    QList<qint16> out;
    for (qsizetype offset = 0; offset + static_cast<qsizetype>(sizeof(qint16)) <= bytes.size(); offset += sizeof(qint16)) {
        qint16 value = 0;
        std::memcpy(&value, bytes.constData() + offset, sizeof(value));
        out << value;
    }
    return out;
}

} // namespace

class TestAudioDevices : public QObject {
    Q_OBJECT

private slots:
    void systemDefaultIsAChoice()
    {
        const Resolution r = resolve(cards(), {});
        QCOMPARE(r.kind, Resolution::SystemDefault);
        QCOMPARE(comboIndex(r), 0);
    }

    void findsByIdentifier()
    {
        const Resolution r = resolve(cards(), {"{0.0.1.00000000}.{bbb}", "Microfono (2- USB Audio CODEC )"});
        QCOMPARE(r.kind, Resolution::Found);
        QCOMPARE(r.index, 1);
        QCOMPARE(comboIndex(r), 2);
    }

    // Windows rinomina la scheda (il numero davanti cambia con le porte USB):
    // l'identificativo la ritrova lo stesso.
    void survivesARename()
    {
        const Resolution r = resolve(cards(), {"{0.0.1.00000000}.{bbb}", "Microfono (3- USB Audio CODEC )"});
        QCOMPARE(r.kind, Resolution::Found);
        QCOMPARE(r.index, 1);
    }

    // L'identificativo e' cambiato (un'altra porta) ma il nome e' lo stesso e
    // uno solo: e' quella.
    void fallsBackToAUniqueName()
    {
        const Resolution r = resolve(cards(), {"{0.0.1.00000000}.{old}", "Gruppo microfoni (USB Audio CODEC)"});
        QCOMPARE(r.kind, Resolution::Found);
        QCOMPARE(r.index, 2);
    }

    // Le impostazioni vecchie hanno solo il nome.
    void legacyNameOnly()
    {
        QCOMPARE(resolve(cards(), {{}, "Microfono (Realtek Audio)"}).index, 0);
        QCOMPARE(resolve(cards(), {{}, "Microfono (Realtek Audio)"}).kind, Resolution::Found);
    }

    // Quello che e' sparito non diventa un'altra scheda: ne' il predefinito, ne'
    // la prima riga dell'elenco.
    void missingStaysMissing()
    {
        const Resolution r = resolve(cards(), {"{0.0.1.00000000}.{gone}", "Microfono (9- Scheda che non c'e')"});
        QCOMPARE(r.kind, Resolution::Missing);
        QCOMPARE(r.index, -1);
        QCOMPARE(comboIndex(r), -1);
        QCOMPARE(resolve({}, {"x", "y"}).kind, Resolution::Missing);
    }

    void twoCardsWithTheSameName()
    {
        QList<Entry> twins = {{"{1}", "USB Audio CODEC"}, {"{2}", "USB Audio CODEC"}, {"{3}", "Altra"}};
        // Con l'identificativo non c'e' dubbio.
        QCOMPARE(resolve(twins, {"{2}", "USB Audio CODEC"}).kind, Resolution::Found);
        QCOMPARE(resolve(twins, {"{2}", "USB Audio CODEC"}).index, 1);
        // Solo col nome: si dice che e' ambiguo, e si prende la prima.
        const Resolution r = resolve(twins, {{}, "USB Audio CODEC"});
        QCOMPARE(r.kind, Resolution::Ambiguous);
        QCOMPARE(r.index, 0);
        // E nell'elenco si distinguono.
        QCOMPARE(labels(twins), QStringList({"USB Audio CODEC", "USB Audio CODEC (2)", "Altra"}));
    }

    void downmixesStereoSigned16()
    {
        const MonoPcm out = convertToMonoInt16(int16Bytes({1000, 3000, -32768, 32767}),
                                                format(48000, 2, QAudioFormat::Int16));
        QCOMPARE(out.consumedBytes, qsizetype(8));
        QCOMPARE(unpackInt16(out.samples), QList<qint16>({2000, 0}));
    }

    void convertsFloatAndKeepsAnIncompleteFrame()
    {
        const QAudioFormat f = format(44100, 1, QAudioFormat::Float);
        const std::array<float, 2> source{0.5f, -1.0f};
        QByteArray input(reinterpret_cast<const char*>(source.data()), static_cast<qsizetype>(sizeof(source) - 1));
        const MonoPcm partial = convertToMonoInt16(input, f);
        QCOMPARE(partial.consumedBytes, qsizetype(sizeof(float)));
        QCOMPARE(unpackInt16(partial.samples), QList<qint16>({16384}));

        input.append(reinterpret_cast<const char*>(source.data()) + sizeof(float) * 2 - 1, 1);
        const MonoPcm complete = convertToMonoInt16(input.mid(partial.consumedBytes), f);
        QCOMPARE(unpackInt16(complete.samples), QList<qint16>({-32768}));
    }

    void convertsUnsigned8AndSigned32()
    {
        const QByteArray unsigned8("\x00\x80\xff", 3);
        QCOMPARE(unpackInt16(convertToMonoInt16(unsigned8, format(8000, 1, QAudioFormat::UInt8)).samples),
                 QList<qint16>({-32768, 0, 32512}));

        const std::array<qint32, 2> signed32{
            std::numeric_limits<qint32>::min(), std::numeric_limits<qint32>::max()};
        const QByteArray bytes(reinterpret_cast<const char*>(signed32.data()), static_cast<qsizetype>(sizeof(signed32)));
        QCOMPARE(unpackInt16(convertToMonoInt16(bytes, format(48000, 1, QAudioFormat::Int32)).samples),
                 QList<qint16>({-32768, 32767}));
    }

    void describesTheActualOpenFormat()
    {
        QCOMPARE(formatDescription(format(48000, 2, QAudioFormat::Float)),
                 QStringLiteral("48000 Hz · 2 channels · float"));
    }
};

QTEST_MAIN(TestAudioDevices)
#include "tst_audiodevices.moc"
