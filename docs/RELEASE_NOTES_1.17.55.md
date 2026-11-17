## English

DecoDXLog 1.17.55 includes the changes since 1.17.53, including the alignment with the upstream 1.17.54 release.

### Club Log download and import

- Download the active station's log directly from Club Log using its personal REST token. The download is asynchronous; parsing and import preview run on a worker thread.
- Review the estimated new, duplicate and rejected contacts before confirming the import. The import itself runs in the background for file-backed logbooks.
- New contacts retain their Club Log QSO identifiers and are marked as already uploaded. Existing duplicates follow the normal import rules and are not automatically replaced. If the station or log changes during the preview, run the preview again.

### Club Log upload recovery

- Automatic status checks for an unresolved upload now respect the Club Log automatic-upload switch and require stored credentials. A server HTTP 5xx stops repeated checks in the current session; the checkpoint remains available for manual reconciliation without blindly resending contacts. A new check can occur after restarting with automatic upload enabled.
- This addresses the unwanted five-minute messages reported by users. The cause of freezes when editing, deleting or scrolling QSOs has not yet been established.

### Included from 1.17.54

- Upstream alignment and the Club Log REST integration: secure personal-token storage, station-specific uploads and reconciliation, DXCC and CTY lookup, and paginated direct OQRS requests. Legacy Club Log credentials remain supported.
- Build and packaging alignment across macOS, Windows and Linux.

### Verification and platform notes

- The Club Log download was tested with a simulated service and with a real read-only export. In an isolated temporary database the real export contained 7,393 records; a repeat import added none. Simulated checks covered HTTP errors, duplicates and event-loop responsiveness. The import-preview confirmation was also checked in a test application window.
- The Windows packaging step selects the matching OpenSSL runtime DLL pair provided by MinGW; the installed-runtime smoke test also checks that Qt can initialise its TLS backend without the MSYS2 libraries on PATH.
- macOS bundles are ad-hoc signed, not Developer ID notarised. Windows and Linux packages are produced by their respective GitHub runners and smoke-tested by the release workflows.

## Italiano

DecoDXLog 1.17.55 comprende gli aggiornamenti dalla 1.17.53, incluso l’allineamento alla versione upstream 1.17.54.

### Download e importazione da Club Log

- Scarica direttamente il log della stazione attiva da Club Log usando il token REST personale. Il download è asincrono; lettura e preparazione dell’anteprima avvengono su un worker.
- Prima di confermare l’importazione puoi controllare la stima dei QSO nuovi, duplicati e scartati. L’importazione dei log su file viene eseguita in background.
- I nuovi QSO conservano gli identificativi Club Log e sono segnati come già inviati. I duplicati esistenti seguono le normali regole di importazione e non vengono sostituiti automaticamente. Se la stazione o il log cambiano durante l’anteprima, occorre ripeterla.

### Recupero degli upload Club Log

- I controlli automatici di un upload non ancora riconciliato rispettano l’interruttore dell’invio automatico Club Log e richiedono credenziali memorizzate. Un errore HTTP 5xx ferma i controlli ripetuti nella sessione corrente; il checkpoint resta disponibile per la verifica manuale senza reinviare i QSO alla cieca. Dopo un riavvio, con invio automatico attivo, può essere effettuato un nuovo controllo.
- Questo corregge i messaggi indesiderati ogni cinque minuti segnalati dagli utenti. La causa dei blocchi durante modifica, cancellazione o scorrimento dei QSO non è stata ancora accertata.

### Incluso dalla 1.17.54

- Allineamento upstream e integrazione REST Club Log: token personale protetto, invio e riconciliazione per stazione, ricerca DXCC e aggiornamento CTY, richieste OQRS dirette paginate. Restano disponibili le credenziali Club Log legacy.
- Allineamento delle procedure di compilazione e distribuzione per macOS, Windows e Linux.

### Verifiche e piattaforme

- Download Club Log verificato con un servizio simulato e con un’esportazione reale in sola lettura. In un database temporaneo isolato l’esportazione reale conteneva 7.393 record; un secondo import non ha aggiunto QSO. Le prove simulate hanno coperto errori HTTP, duplicati e reattività dell’event loop. La conferma dell’anteprima è stata verificata anche in una finestra di prova.
- Il packaging Windows seleziona la coppia di DLL OpenSSL del runtime MinGW; lo smoke test dell’installazione verifica anche che Qt inizializzi il backend TLS senza le librerie MSYS2 nel PATH.
- I pacchetti macOS sono firmati ad hoc e non notarizzati Developer ID. I pacchetti Windows e Linux sono generati dai rispettivi runner GitHub e sottoposti agli smoke test dei workflow di release.
