# DecoDXLog 1.17.46

Changes from v1.17.45 / Aggiornamenti dalla v1.17.45.

## Italiano

### LoTW e TQSL

- Corretta l'interpretazione dei codici di uscita TQSL, compresi errori, duplicati e risultati parziali.
- Un blocco con esito parziale o ambiguo non viene marcato interamente come inviato; l'invio automatico LoTW viene sospeso per consentire una verifica.
- La scheda Invio QSL indica che ogni richiesta contiene al massimo 500 QSO e distingue il caricamento del file dalla successiva elaborazione su LoTW.
- I QSO già segnati come inviati non vengono riconciliati automaticamente: confrontare gli invii precedenti con Your Activity su LoTW.

### Log e diagnostica CAT

- La tabella del log richiede l'aggiornamento completo delle righe spostate dopo l'inserimento di un QSO, per evitare celle visivamente disallineate.
- Nuova diagnostica CAT facoltativa tramite `DECODXLOG_CAT_DIAGNOSTIC=1`: registra richieste, risposte e dettagli Hamlib nel log del processo. Il log può contenere dati radio e testo CW.
- La riapertura della seriale effettuata da rigctld dopo un timeout Hamlib non è corretta in questa versione e richiede ulteriore lavoro.

### Verifica

- Test dei codici TQSL, della coda QSL, del modello del log e del protocollo CAT con simulatori; build locale macOS.
- Nessun invio LoTW reale è stato effettuato durante questi test; verificare gli esiti degli upload nel proprio account.

## English

### LoTW and TQSL

- Corrected interpretation of TQSL exit codes, including errors, duplicates and partial results.
- A partial or ambiguous batch is not marked entirely as sent; automatic LoTW uploads are paused for review.
- The QSL panel explains the 500-QSO limit per request and distinguishes uploading a file from subsequent LoTW processing.
- Previously sent status is not automatically reconciled: compare earlier uploads with LoTW Your Activity.

### Logbook and CAT diagnostics

- After inserting a QSO, the logbook model requests a full refresh of shifted rows to prevent visually misaligned cells.
- Optional CAT diagnostics via `DECODXLOG_CAT_DIAGNOSTIC=1` records timestamped requests, replies and detailed Hamlib output in the process log. The log may contain radio data and CW text.
- The serial reopen performed by rigctld after a Hamlib timeout is not fixed in this version and needs further work.

### Verification

- Tests cover TQSL exit codes, the QSL queue, the logbook model and CAT protocol simulators; a local macOS build was run.
- No real LoTW upload was performed during these tests; check upload processing in your account.
