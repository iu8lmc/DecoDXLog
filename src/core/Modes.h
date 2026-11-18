// DecoDXLog — i modi con cui si opera, e come li chiama la radio.
//
// Sul display si sceglie il modo come lo si dice in aria: CW, USB, FT8, RTTY.
// La radio, via Hamlib, ne conosce di meno: tutti i modi digitali per lei sono
// una banda laterale con i dati dentro (PKTUSB). Qui sta la corrispondenza, una
// volta sola, cosi' non se la inventa ogni finestra.
#pragma once

#include <QString>
#include <QVector>

namespace decolog::core::modes {

struct Entry {
    QString name;   // come lo scrive l'operatore e come va nel log: FT8, CW, USB
    QString cat;    // come lo vuole Hamlib: CW, USB, LSB, RTTY, PKTUSB…
    QString group;  // "cw", "voice", "data": servono solo a dividere il menu
};

// I modi nell'ordine in cui conviene trovarli: prima CW e fonia, poi i digitali.
QVector<Entry> all();

// Il gruppo a cui appartiene un modo quando si conta: "CW", "PHONE", "DATA".
// E' lo stesso criterio dei diplomi — la fonia e' la fonia, il CW e' il CW, e
// tutto il resto e' digitale — e serve alla griglia banda x modo.
QString groupFor(const QString& mode);

// Modo ADIF effettivo: quando ADIF usa un modo contenitore (per esempio MFSK)
// e un sottomodo noto (FT4/FT8/FT2), il sottomodo guida i filtri; un sottomodo
// sconosciuto non viene trasformato silenziosamente in un modo conosciuto.
QString displayName(const QString& mode, const QString& submode = {});

// Il nome che vuole Hamlib per questo modo. Un modo che non si conosce finisce
// su USB, che e' la scelta che non fa danni.
QString catFor(const QString& mode);

// Lo stesso, sapendo dove sta il VFO. Serve alla fonia: "SSB" non dice quale
// banda laterale, e la regola la sanno tutti tranne il programma — sotto i
// 10 MHz si parla in LSB, sopra in USB. Con mhz a zero vale il caso generale.
QString catFor(const QString& mode, double mhz);

// Cluster FT spots use audio tones on USB. Avoid CAT data modes that some
// Hamlib backends map to the radio's dedicated RTTY modem.
QString catForClusterSpot(const QString& mode, double mhz);

} // namespace decolog::core::modes
