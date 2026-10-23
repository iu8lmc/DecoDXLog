# DecoDXLog 1.17.36

## Italiano

Aggiornamenti dalla `1.17.34` alla `1.17.36`.

### 1.17.34

- Nuovo QSO configurabile con campi opzionali, campi ADIF personalizzati in `adif_extra`, potenza
  TX dal profilo e scorciatoia `Alt+W`.
- Gestione piu' chiara e sicura delle password DX Cluster, con diagnostica del prompt di login e
  test multipiattaforma dedicati.

### 1.17.35

- Allineamento alle modifiche upstream della 1.17.34.
- Corretto il caricamento delle cartoline eQSL: viene accettato soltanto un vero riferimento a
  un'immagine, evitando URL inventati e il relativo errore 404.
- Aggiornate le traduzioni delle nuove funzioni.

### 1.17.36

- I pannelli staccati sono vere finestre indipendenti e possono essere spostati su un monitor
  esterno senza restare legati alla dashboard principale.
- Posizione, dimensione, stato aperto/chiuso e stato flottante vengono salvati e ripristinati,
  anche quando DecoDXLog viene chiuso.
- La stessa gestione e' applicata a Nuovo QSO, Log, Scheda nominativo, CW, Rotore, FT2 Award,
  Mappa, Band map, Decodium, DX Cluster, Contest, Chat, Logbook, Statistiche e QSL.
- La chiusura della dashboard non riaggancia erroneamente i pannelli e non perde l'ultima
  geometria.
- Aggiunti test ad hoc per tutti i percorsi di sgancio e persistenza; la versione e' stata
  verificata con build, CTest e prove operative su piu' monitor macOS.

## English

Updates from `1.17.34` to `1.17.36`.

### 1.17.34

- Configurable New QSO with optional fields, custom ADIF fields stored in `adif_extra`, profile
  based TX power, and the `Alt+W` shortcut.
- Clearer and safer DX Cluster password handling, login-prompt diagnostics, and dedicated
  cross-platform tests.

### 1.17.35

- Synchronized with the upstream 1.17.34 changes.
- Fixed eQSL card loading: only a real image reference is accepted, preventing fabricated URLs
  and the resulting 404 error.
- Updated translations for the new features.

### 1.17.36

- Detached panels are now real independent top-level windows and can be moved to an external
  monitor without remaining attached to the main dashboard.
- Position, size, open/closed state, and floating state are saved and restored, including during
  application shutdown.
- The same behavior is applied to New QSO, Log, Call info, CW, Rotor, FT2 Award, Map, Band map,
  Decodium, DX Cluster, Contest, Chat, Logbook, Statistics, and QSL windows.
- Closing the dashboard no longer accidentally reattaches panels or loses their last geometry.
- Added targeted tests for every detachment and persistence route; verified with the build, CTest,
  and operational multi-monitor checks on macOS.
