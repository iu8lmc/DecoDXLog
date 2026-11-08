// DecoDXLog — il registro degli avvisi di Qt e di QML.
//
// Quando una parte dell'interfaccia si blocca (un menu o le impostazioni che non
// si aprono), il programma di solito non dice niente: gli avvisi di QML finiscono
// nella console, che nessuno guarda. Qui si copiano su un file accanto al
// biglietto di crash, con l'ora e la sorgente. Il file si ruota da solo, cosi'
// non cresce all'infinito.
#pragma once

#include <QString>

namespace decolog::warninglog {

// Da chiamare una volta, presto: `folder` e' la cartella dei file di supporto
// (la stessa dei crash-*.txt). Gli avvisi finiscono in `folder/avvisi.log`.
void install(const QString& folder);

// Il percorso del file, per dirlo all'utente nei messaggi di diagnostica.
QString logPath();

} // namespace decolog::warninglog
