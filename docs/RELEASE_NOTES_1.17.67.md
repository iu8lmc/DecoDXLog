# DecoDXLog 1.17.67

## English

This version expands the desktop logger across awards, QSL, portable operation, satellite tracking, reporting and contest workflows. It also includes fixes for search, bulk changes, CAT and data integrity. Existing logs remain the source of truth; review previews and back up your log before a major upgrade or import.

### Logging, search and imports
- Callsign searches distinguish the requested callsign from matching locator and other fields. Bulk paper-QSL changes show how many QSOs were changed, skipped or left unchanged.
- Imports offer a review of source and custom fields, duplicate handling and a durable journal for reviewing and selectively undoing unchanged imported QSOs. Historical entry supports explicitly recorded dates and times.
- Large-log background reads use read-only connections, reducing contention with log writes.
- QSO updates, deletes, QSL state and local API operations include additional transaction, log-identity and error checks to prevent silent partial changes or actions against a different log.

### Awards and QSL
- CQ Marathon review separates entity and zone candidates, records alternatives and makes uncertainty and rule limits visible. VUCC previews support reported boundary grids and distinguish supported confirmation and satellite cases.
- The QSL planner compares locally recorded award needs and alternative confirmation routes, with optional operator-supplied costs and account/profile filtering. Plans are previews, not guarantees of award credit.
- Local QSL image OCR provides field suggestions and candidate searches for explicit operator review; acquisition history and guarded undo preserve provenance.
- An HRDLog pending-upload filter helps identify contacts awaiting upload; a pending QSO is not necessarily the cause of an upload failure.
- LoTW and Club Log paths provide more conservative handling of uncertain outcomes and changed local QSL states. Check pending or uncertain records before retrying external services.

### Portable, radio nets and satellites
- Activation sessions record site and operator changes and support review and partitioned exports for POTA, WWFF, SOTA and IOTA. Offline catalogue support and review of uncertain historical associations are included; official acceptance remains dependent on programme rules and external tools.
- The local Radio Net Manager provides check-ins, attendance status, turn order, session history, reminders and explicit QSO creation. A roster entry alone does not create a QSO.
- The read-only PREDICT integration supports satellite selection, pass planning, tracker snapshots, Doppler previews and explicitly saved QSO drafts with separate TX/RX frequencies. Rover position history and VUCC candidate previews complement these workflows. Tracker drafts do not command CAT or PTT.

### DX, reports and custom fields
- DX campaigns offer date and band/mode objectives, cluster-spot ranking, watchlists and alerts based on locally recorded goals.
- Custom QSO fields support typed values, conditional requirements and bounded derived expressions with explicit recalculation and change review.
- Reports include recorded station activity, confirmation times, annual club activity and saved custom groupings with CSV/PDF export. Custom reports can count QSOs, calls, UTC days and recorded DXCC numbers or calculate mean recorded TX power; exact recorded mode/submode filtering is available. These summaries do not validate award eligibility or measure actual RF power.

### Contest and extensions
- Contest tools include a local callsign queue, CQ WW report review, local audio recording/recovery and QSO navigation, activity-period comparisons and saved goals.
- Experimental station-to-station features add durable serial request/grant state, inventory comparison and explicitly reviewed QSO exchange. They do not guarantee global serial uniqueness during a network partition or full automatic convergence.
- The experimental extension harness and local API add capability discovery, a read-only token, bounded extension management and keyed request inspection/review. Extensions run as trusted local processes, not in a security sandbox. An unresolved request requires operator review before retrying.

### CAT and cluster
- Improve cluster spot handling and FT-mode mapping for supported radios, including the serial Kenwood TS-50 path. Radio, interface and backend behaviour must still be checked on the actual station.

### Important limits
- This release contains ongoing desktop roadmap work; some contest networking, award-rule validation, tracker interoperability, extension isolation and external-service workflows remain incomplete or experimental.
- A custom report CSV with a mode filter includes the filter in each data row; empty CSVs do not retain its value, and CSV does not include all filter metadata. Keep the report definition or PDF for context.
- New mode-filtered named report definitions use a newer format. Older versions may not read them. Previously saved legacy last-used filters containing a mode should be saved again in this version before a downgrade.
- Tests and packaging checks do not replace validation with real radios, external services, customer logs or each target operating system. See `docs/evolution/PROGRESS.md` for the actual evidence and remaining prerequisites.

## Italiano

Questa versione amplia il programma desktop nelle aree diplomi, QSL, attività portatili, satelliti, rapporti e contest. Include inoltre correzioni per ricerca, modifiche multiple, CAT e integrità dei dati. Il log esistente resta la fonte dei dati: controlla le anteprime ed esegui un backup prima di un aggiornamento importante o di un'importazione.

### Log, ricerca e importazione
- La ricerca del nominativo distingue il nominativo richiesto dalle corrispondenze in locator e altri campi. Le modifiche multiple delle QSL cartacee indicano i QSO modificati, saltati o invariati.
- L'importazione offre la revisione dei campi di origine e personalizzati, la gestione dei duplicati e un registro persistente per controllare e annullare selettivamente i QSO importati rimasti invariati. L'inserimento storico usa date e orari indicati esplicitamente.
- Le letture in background dei log grandi usano connessioni in sola lettura, riducendo la competizione con le scritture.
- Modifiche, cancellazioni, stati QSL e API locali comprendono ulteriori controlli su transazioni, identità del log ed errori, per evitare modifiche parziali silenziose o operazioni sul log sbagliato.

### Diplomi e QSL
- La revisione CQ Marathon separa candidati per entità e zona, registra le alternative e mostra incertezze e limiti delle regole. Le anteprime VUCC supportano i locator di confine registrati e distinguono i casi supportati per conferma e satellite.
- Il pianificatore QSL confronta esigenze e percorsi alternativi sulla base dei dati locali, con costi facoltativi inseriti dall'operatore e filtri per account e profilo. Le proposte non garantiscono il credito per un diploma.
- L'OCR locale delle immagini QSL suggerisce campi e QSO candidati per una revisione esplicita; cronologia e annullamento controllato conservano la provenienza.
- Il filtro HRDLog per caricamenti in sospeso aiuta a individuare i QSO ancora da inviare; un QSO in sospeso non è necessariamente la causa di un errore.
- Le procedure LoTW e Club Log gestiscono in modo più prudente gli esiti incerti e le modifiche locali degli stati QSL. Controlla i record in sospeso o incerti prima di riprovare un servizio esterno.

### Portatile, reti radio e satelliti
- Le sessioni di attivazione registrano cambi di sito e operatore e consentono revisione ed esportazioni separate per POTA, WWFF, SOTA e IOTA. Sono disponibili cataloghi offline e revisione delle associazioni storiche incerte; l'accettazione ufficiale dipende dai regolamenti e dagli strumenti esterni.
- Il Net Manager radio locale gestisce check-in, presenza, ordine dei turni, storico, promemoria e creazione esplicita dei QSO. La sola presenza nel roster non crea un QSO.
- L'integrazione PREDICT in sola lettura supporta selezione del satellite, pianificazione dei passaggi, istantanee del tracker, anteprima Doppler e bozze QSO salvate esplicitamente con frequenze TX/RX separate. Sono disponibili cronologia delle posizioni rover e anteprime dei candidati VUCC. Le bozze del tracker non inviano comandi CAT o PTT.

### DX, rapporti e campi personalizzati
- Le campagne DX offrono obiettivi per periodo, banda e modo, ordinamento degli spot, watchlist e avvisi basati sugli obiettivi registrati localmente.
- I campi QSO personalizzati supportano tipi, obbligatorietà condizionale ed espressioni derivate limitate, con ricalcolo esplicito e revisione delle modifiche.
- I rapporti comprendono attività della stazione, tempi di conferma, attività annuale del club e raggruppamenti personalizzati salvabili con export CSV/PDF. Possono contare QSO, nominativi, giorni UTC e numeri DXCC registrati o calcolare la media della potenza TX registrata; è disponibile il filtro esatto per modo/sottomodo. Questi riepiloghi non verificano i diplomi né misurano la potenza RF reale.

### Contest ed estensioni
- Gli strumenti contest comprendono coda locale di nominativi, revisione dei report CQ WW, registrazione audio locale con recupero e navigazione per QSO, confronto dei periodi di attività e obiettivi salvati.
- Le funzioni sperimentali fra postazioni aggiungono stato persistente per richieste e assegnazioni di progressivi, confronto degli inventari e scambio QSO con revisione esplicita. Non garantiscono l'unicità globale dei progressivi durante una partizione di rete né la convergenza automatica completa.
- Il sistema sperimentale di estensioni e l'API locale aggiungono scoperta delle capacità, token in sola lettura, gestione limitata delle estensioni e controllo delle richieste con chiave. Le estensioni sono processi locali attendibili, non isolati in una sandbox di sicurezza. Gli esiti incerti richiedono revisione dell'operatore prima di riprovare.

### CAT e cluster
- Migliorata la gestione degli spot del cluster e la mappatura dei modi FT per radio supportate, incluso il percorso seriale Kenwood TS-50. Il comportamento di radio, interfaccia e backend richiede comunque una verifica sulla stazione reale.

### Limiti importanti
- Questa versione contiene lavoro ancora in corso sulla roadmap desktop; alcune funzioni di rete contest, verifica dei regolamenti, interoperabilità dei tracker, isolamento delle estensioni e servizi esterni restano incomplete o sperimentali.
- Un CSV di rapporto con filtro di modo riporta il filtro in ogni riga; se non ci sono righe il valore non è conservato, e il CSV non include tutti i metadati dei filtri. Conserva la definizione del rapporto o il PDF per il contesto.
- Le nuove definizioni di rapporti con filtro di modo usano un formato più recente, non necessariamente leggibile dalle versioni precedenti. I vecchi ultimi filtri salvati con un modo devono essere salvati nuovamente con questa versione prima di un downgrade.
- Test e controlli dei pacchetti non sostituiscono le verifiche con radio reali, servizi esterni, log degli utenti e singoli sistemi operativi. Il dettaglio delle prove e dei prerequisiti si trova in `docs/evolution/PROGRESS.md`.
