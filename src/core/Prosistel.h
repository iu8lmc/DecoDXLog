// DecoDXLog — il protocollo seriale dei control box PRO.SIS.TEL.
//
// E' quello che parlava DecoRotor (decorotor/protocol.py), riscritto qui per
// il gateway integrato. Riferimento: Hamlib, rotators/prosistel/prosistel.c.
//
// Ogni frame e' ASCII, fra STX (0x02) e CR (0x0D):
//
//     comando   ->  \x02 <id> <verbo> [argomento] \r
//     risposta  <-  \x02 <id> , <verbo> , <valore> , <stato> \r
//
//     \x02A?\r        dov'e' l'azimut
//     \x02A,?,290,R\r 290 gradi, fermo
//     \x02AG290\r     vai a 290 gradi
//     \x02AG997\r     stop dolce (999 = rapido)
//     \x02AS\r        CPM spento, da mandare quando si apre la porta
//
// Sui Combi-Track gli angoli viaggiano per dieci: 290.0 gradi e' "2900", e gli
// stop diventano 9777 / 9999.
#pragma once

#include <QByteArray>
#include <QChar>
#include <QList>
#include <QString>

#include <optional>

namespace decolog::core::prosistel {

inline constexpr char kStx = 0x02;
inline constexpr char kCr = 0x0D;
inline constexpr QChar kAzimuth{u'A'};
inline constexpr QChar kElevation{u'E'};
inline constexpr QChar kSecondUnit{u'B'};   // l'elevazione dei Combi-Track
inline constexpr int kStopSoft = 997;
inline constexpr int kStopFast = 999;

// Come e' cablato un control box: gli assi che ha e come conta i gradi.
struct Model {
    QString key;
    QString label;
    QChar azId;        // nullo: l'asse non c'e'
    QChar elId;
    int multiplier{1};
    bool hasAz() const { return !azId.isNull(); }
    bool hasEl() const { return !elId.isNull(); }
};

// d_az, d_el, d_azel, combi — come in Hamlib.
const QList<Model>& models();
// nullptr per "auto" o per una chiave che non c'e'.
const Model* modelFor(const QString& key);

struct Reply {
    QChar axis;
    QChar verb;
    double value{0.0};
    QChar status;
    // 'R' e' fermo; chi sa dire che gira risponde con un'altra lettera.
    bool moving() const { return status != QLatin1Char('R'); }
};

QByteArray encode(QChar axis, QChar verb, const QString& argument = {});
QByteArray queryPosition(QChar axis);
QByteArray gotoAngle(QChar axis, double degrees, int multiplier = 1);
QByteArray stop(QChar axis, bool fast = false, int multiplier = 1);
QByteArray disableCpm(QChar axis);
// La risposta, con o senza STX e CR intorno; nullopt se non e' una risposta.
std::optional<Reply> decode(const QByteArray& frame, int multiplier = 1);
// Un frame leggibile per la diagnostica: <STX>A?<CR>.
QString printable(const QByteArray& frame);

} // namespace decolog::core::prosistel
