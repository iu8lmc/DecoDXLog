# DecoDXLog 1.17.44

Aggiornamenti dalla **1.17.43 alla 1.17.44**.

## Italiano

### Multi-operatore e routing dei QSO

- I QSO ricevuti da Decodium, WSJT-X, N1MM, dall'API o dalla rete vengono associati
  prima al profilo indicato da `STATION_CALLSIGN`.
- Se il nominativo di stazione manca, viene usato `OPERATOR` solo quando corrisponde
  a un unico profilo. Con più corrispondenze il QSO resta non assegnato e viene
  registrato un avviso, invece di finire nel log sbagliato.
- Dalla gestione dei profili stazione è possibile aprire una finestra **Nuovo QSO**
  dedicata a ciascun profilo. La finestra salva nel profilo scelto senza modificare
  il profilo globale attivo della dashboard.

### Profili CAT persistenti

- È possibile memorizzare più profili radio indipendenti, con collegamento Hamlib,
  TCI, flrig o OmniRig, modello, indirizzo, porta seriale, baud rate, parametri
  seriali, PTT, keyer e ingresso audio del decoder.
- Le modifiche vengono salvate automaticamente e l'ultimo profilo radio usato viene
  ripristinato al successivo avvio.
- I valori storici di `QSettings` restano sincronizzati per mantenere la compatibilità
  con configurazioni e componenti esistenti.

### Pannelli e Liquid Glass

- I pannelli staccati usano coordinate globali del desktop virtuale durante il drag:
  possono attraversare il confine tra monitor su macOS, Windows e Linux senza
  dipendere dall'implementazione del compositor di `startSystemMove()`.
- La posizione viene salvata al termine dello spostamento.
- Le finestre modali Liquid Glass hanno una superficie interna più opaca e leggibile;
  il tema classico resta completamente opaco e invariato.

### Verifica

- Test A/B per due operatori con QSO UDP instradati verso profili distinti.
- Test di creazione, separazione, persistenza e ripristino dell'ultimo profilo CAT.
- Test delle finestre QSO dedicate, del movimento globale dei pannelli e della
  leggibilità dei dialoghi Liquid Glass.
- Build macOS e test mirati eseguiti prima dell'avvio dei runner di release.

## English

### Multi-operator QSO routing

- QSOs received from Decodium, WSJT-X, N1MM, the API or the station network are
  first assigned using `STATION_CALLSIGN`.
- When the station callsign is missing, `OPERATOR` is used only if it identifies
  exactly one profile. Ambiguous identities remain unassigned and produce an
  activity warning instead of silently contaminating another operator's log.
- Station profile management can open a dedicated **New QSO** window for each
  profile. Each window writes to its selected profile without changing the global
  dashboard profile.

### Persistent CAT profiles

- Multiple independent radio profiles can now be stored, including Hamlib, TCI,
  flrig or OmniRig link settings, model, address, serial port, baud rate, serial
  parameters, PTT, keyer and decoder audio input.
- Changes are saved automatically and the last-used radio profile is restored on
  the next startup.
- Legacy `QSettings` values remain synchronized for compatibility with existing
  configurations and components.

### Detached panels and Liquid Glass

- Detached panels use global virtual-desktop coordinates while dragging, so they
  can cross between monitors on macOS, Windows and Linux without depending on the
  compositor-specific behavior of `startSystemMove()`.
- The panel position is saved when the drag ends.
- Liquid Glass modal windows now use a more opaque, readable inner surface; the
  classic theme remains fully opaque and unchanged.

### Verification

- A/B tests cover two operators with UDP QSOs routed to separate profiles.
- Tests cover creation, separation, persistence and restoration of the last CAT
  profile.
- Tests cover dedicated QSO windows, global panel movement and readable Liquid
  Glass dialogs.
- The macOS build and focused tests were run before dispatching the release runners.
