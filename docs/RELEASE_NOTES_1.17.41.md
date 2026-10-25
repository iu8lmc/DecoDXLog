# DecoDXLog 1.17.41

Aggiornamenti dalla **1.17.38 alla 1.17.41**.

## Italiano

### 1.17.41 — Elenchi nominativi e club

- Nuova pagina **Impostazioni → Elenchi nominativi**, disponibile anche fuori dal
  Contest Mode.
- Importazione di più elenchi locali in formato CSV/TXT e dei file N1MM Call
  History, con riconoscimento del formato, anteprima e associazione delle colonne.
- Supporto a virgolette, separatori virgola/punto e virgola/tab, UTF-8 e Latin-1;
  commenti e righe informative vengono ignorati.
- Attivazione, disattivazione, ricaricamento e rimozione dei singoli elenchi, con
  configurazione conservata al riavvio.
- La Scheda nominativo mostra tutte le appartenenze per fonte e data senza
  modificare QSO, callbook, scambi contest o conteggi dei diplomi.
- Indicizzazione su worker, copie offline persistenti e conservazione dell'ultima
  copia valida se un nuovo import fallisce.
- Test aggiunti per file CSV vari, codifiche, virgolette, appartenenze multiple,
  conflitti, riavvio, file invalidi, elenchi grandi e viste QML.

### 1.17.41 — Cluster, LoTW e CW

- Aggiunta la L verde accanto ai nominativi LoTW nel cluster, anche senza il filtro
  LoTW e nelle viste compatte o contest.
- Il suggerimento distingue l'attività LoTW dalla conferma del QSO.
- Il pulsante **Ferma** del CW ora gestisce pressioni ripetute come una sola
  richiesta, svuota la coda, rilascia il PTT e attende l'esito CAT prima di
  consentire nuove macro.
- Ridotti i comandi CAT duplicati sui sistemi Yaesu con PTT via CAT, con fallback
  di emergenza per connessioni lente o interrotte.
- Aggiunti test per clic ripetuti, risposte tardive, fallback, errori transitori e
  macro successive.

### 1.17.40 — DecoPort per HamHub

- DecoDXLog espone il log a HamHub tramite il protocollo DecoPort locale, con
  registrazione automatica, conferma dei QSO, ritrasmissioni limitate e registro
  attività.
- I pacchetti sono autenticati con HMAC-SHA256 e la sessione resta limitata a
  `127.0.0.1`.

### 1.17.39 — CW Yaesu e QSL

- Ripristinato il CW via CAT per le radio Yaesu, incluso FTDX-3000, con gestione
  corretta del testo CW, dell'arresto e della memoria del keyer.
- LoTW ed eQSL automatici vengono elaborati in una coda sequenziale: eQSL parte
  dopo la conclusione di LoTW anche in caso di errore.

### 1.17.38 — CW via CAT

- Riabilitato il CW via CAT Yaesu collegato direttamente a rigctld.
- Corretti caratteri singoli, macro lunghe e arresto della trasmissione.
- Il runner Windows costruisce il tag richiesto e non il ramo `main` in movimento.

## English

### 1.17.41 — Callsign and club lists

- New **Settings → Callsign lists** page, available outside Contest Mode.
- Import multiple local CSV/TXT lists and N1MM Call History files, with format
  detection, preview and column mapping.
- Supports quoted fields, comma/semicolon/tab separators, UTF-8 and Latin-1;
  comments and informational rows are ignored.
- Enable, disable, reload and remove each list; configuration survives restart.
- The Callsign Information panel displays every source membership and import date
  without changing QSOs, callbook data, contest exchanges or award counts.
- Worker-thread indexing, persistent offline copies and last-known-good cache when
  a reload fails.
- Added tests for separators, encodings, quoting, multiple memberships, conflicts,
  restart, invalid files, large lists and QML views.

### 1.17.41 — Cluster, LoTW and CW

- Added a green L badge beside LoTW callsigns in the cluster, including compact
  and contest views and without enabling the LoTW-only filter.
- The tooltip distinguishes LoTW activity from QSO confirmation.
- Repeated CW **Stop** presses are coalesced into one request; the queue is cleared,
  PTT is released and new macros wait for CAT completion.
- Reduced duplicate CAT commands on Yaesu systems using CAT PTT, while retaining an
  emergency fallback for slow or disconnected links.
- Added regression tests for repeated clicks, delayed replies, fallback, transient
  errors and subsequent macros.

### 1.17.40 — DecoPort for HamHub

- DecoDXLog now exposes its log to HamHub through the local DecoPort protocol, with
  automatic registration, QSO acknowledgements, bounded retries and activity log
  entries.
- Packets use HMAC-SHA256 authentication and the session is restricted to
  `127.0.0.1`.

### 1.17.39 — Yaesu CW and QSL

- Restored CAT CW for Yaesu radios, including the FTDX-3000, with correct CW text,
  stop handling and keyer-memory behavior.
- Automatic LoTW and eQSL uploads now use a sequential queue: eQSL starts after
  LoTW finishes, including when LoTW returns an error.

### 1.17.38 — CAT CW

- Re-enabled CAT CW for Yaesu radios connected directly to rigctld.
- Fixed single characters, long macros and transmission stopping.
- The Windows runner now builds the requested release tag instead of moving `main`.
