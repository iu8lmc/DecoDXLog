// DecoDXLog — i progressivi di una gara multi-operatore, uno solo per tutti.
//
// In una gara con un solo progressivo per la stazione (multi-single, o una
// sequenza unica per regolamento) i PC in rete non possono contare ognuno per
// conto suo: due QSO nello stesso minuto avrebbero lo stesso numero. Uno dei
// PC distribuisce i numeri — quello con l'identita' piu' piccola fra quelli
// presenti. Le assegnazioni sono volatili: una partizione, un riavvio o una
// risposta persa possono causare collisioni. Non garantiscono unicita globale.
#pragma once

#include <QString>
#include <QStringList>
#include <QSet>

namespace decolog::core {

class SharedSerials {
public:
    // Un progressivo visto in giro (un QSO della rete, uno dato a un altro).
    void note(int serial);
    // Un progressivo arrivato da un altro nodo: torna vero se collide con uno
    // gia' assegnato localmente durante una partizione.
    bool noteRemote(int serial);
    // Il distributore: il prossimo numero, che da adesso e' preso.
    int serve();
    // Chi distribuisce: il piu' piccolo fra se' e gli altri presenti.
    static bool isServer(const QString& me, const QStringList& others);
    // Il numero per il QSO di adesso. `server`: questo PC distribuisce;
    // `online`: la rete c'e'. Con un numero avuto in anticipo si usa quello.
    int take(int localNext, bool server, bool online);
    // Un numero avuto dal distributore, per il prossimo QSO.
    void reserve(int serial);
    bool hasReserved() const { return m_reserved > 0; }
    int highest() const { return m_max; }
    bool collisionDetected() const { return m_collision; }

private:
    int m_max{0};
    int m_reserved{0};
    int m_next{0};
    QSet<int> m_issued;
    bool m_collision{false};
};

} // namespace decolog::core
