// DecoDXLog — scegliere una scheda audio in modo preciso.
//
// Il decoder CW ascolta un ingresso, il DVK parla su un'uscita e registra da un
// microfono. Prima la scelta si salvava con il solo nome della periferica, e se
// il nome non si ritrovava — Windows numera le schede uguali, «2- USB Audio
// CODEC», e il numero cambia con le porte USB — si usava in silenzio la scheda
// predefinita del sistema, mentre l'elenco continuava a mostrare un'altra
// riga. Risultato: la scheda cambiava da sola e nessuno sapeva quale.
//
// Adesso la scelta si salva con l'identificativo della periferica (quello
// stabile che da' Windows) e il suo nome com'era; si ritrova prima per
// identificativo, poi per nome se e' uno solo; e se non si ritrova, NON si
// ripiega sul predefinito: si dice che manca e non si parte. «Predefinito di
// sistema» e' una scelta, scritta, non un ripiego.
#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace decolog::app::audiodev {

struct Entry {
    QByteArray id;
    QString name;
};

// Come e' salvata la scelta: l'identificativo e il nome com'era scritto.
// Entrambi vuoti = il predefinito del sistema, scelto apposta.
struct Saved {
    QString id;
    QString name;
};

struct Resolution {
    enum Kind {
        SystemDefault,  // scelto il predefinito di sistema
        Found,          // trovata: per identificativo, o per nome (uno solo)
        Ambiguous,      // piu' periferiche con quel nome e nessun identificativo valido
        Missing,        // non c'e': non si ripiega su un'altra
    };
    Kind kind{SystemDefault};
    int index{-1};      // la riga dell'elenco, per Found e Ambiguous
};

// Il backend audio puo' consegnare il suo formato nativo (per esempio 48 kHz,
// stereo, float) quando 8 kHz mono Int16 non e' disponibile. Il decoder CW
// lavora invece sempre su campioni mono Int16: questa struttura conserva anche
// i byte non ancora completi di un frame fra due readyRead().
struct MonoPcm {
    QByteArray samples;
    qsizetype consumedBytes{0};
};

bool canConvertToMonoInt16(const QAudioFormat& format);
MonoPcm convertToMonoInt16(const QByteArray& input, const QAudioFormat& format);
QString formatDescription(const QAudioFormat& format);

Resolution resolve(const QList<Entry>& entries, const Saved& saved);

// Come si scrivono nell'elenco: lo stesso nome due volte diventa «nome» e
// «nome (2)», cosi' due schede uguali si distinguono.
QStringList labels(const QList<Entry>& entries);

// La riga del menu a tendina, che ha «Predefinito» in cima: 0 il predefinito,
// n+1 la periferica n, -1 se la scelta non si trova.
int comboIndex(const Resolution& resolution);

// Le periferiche del sistema, e quello che serve a sceglierle.
QList<QAudioDevice> inputDevices();
QList<QAudioDevice> outputDevices();
QList<Entry> entriesOf(const QList<QAudioDevice>& devices);
// Per il QML: [{id, name, label}].
QVariantList deviceList(const QList<QAudioDevice>& devices);
// Da una riga del menu (0 predefinito) a come si salva.
Saved savedFor(const QList<QAudioDevice>& devices, int comboRow);

} // namespace decolog::app::audiodev
