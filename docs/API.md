# L'interfaccia HTTP locale

DecoDXLog puo' rispondere ad altri programmi sulla stessa macchina: sapere se un
nominativo e' gia' stato lavorato, leggere gli ultimi QSO, registrarne uno.

Si accende in Impostazioni → Collegamento a Decodium → «Interfaccia locale per altri
programmi», scegliendo una porta (0 = spenta). DecoDXLog ascolta solo su `127.0.0.1`:
dalla rete non si raggiunge.

## La chiave

Ogni richiesta porta la chiave che si vede nelle impostazioni, in uno di questi modi:

    X-DecoDXLog-Token: 0123456789abcdef0123456789abcdef
    Authorization: Bearer 0123456789abcdef0123456789abcdef
    ?token=0123456789abcdef0123456789abcdef

Senza la chiave giusta la risposta e' `401`. Un sito aperto nel browser potrebbe
provare a parlare con `127.0.0.1`, ma non conosce la chiave; e le risposte non hanno
intestazioni CORS, quindi il browser non le fa leggere alle pagine. «Nuova chiave»
ne fa un'altra: i programmi che usavano quella vecchia vanno aggiornati.

## Chiave in sola lettura

Nelle impostazioni dell'interfaccia locale si può creare, sostituire o revocare
una seconda chiave in sola lettura. Questa consente esclusivamente `GET` di
`/api/v1/capabilities`, `/status`, `/worked`, `/qsos` e `/tracker`; ogni scrittura o altro
percorso riceve `403`. Si presenta negli stessi tre modi della chiave completa.
La revoca impedisce nuove richieste con la vecchia chiave. La chiave completa
preesistente continua a consentire anche l'inserimento di QSO. Le chiavi sono
conservate nelle impostazioni locali dell'applicazione, non nel portachiavi;
chi può leggerle può usarle. C'è una sola chiave condivisa di sola lettura, non
un'identità o una revoca indipendente per ogni estensione.

## Credenziali sperimentali per estensione

Per prove avanzate, l'impostazione locale `api/extensionCredentials` può contenere un array JSON di oggetti con esattamente `id`, `token` e `permissions`. Ogni ID e token deve essere unico; il token è una chiave casuale di 32 caratteri esadecimali minuscoli e i soli permessi ammessi sono `qso.read`, `status.read`, `worked.read`, `tracker.read` e `qso.events`. Una configurazione non valida disabilita tutte queste credenziali. Le modifiche sono caricate all'avvio dell'interfaccia API; riavviala o riavvia l'app dopo aver modificato o rimosso una credenziale. Non usare la chiave completa o quella condivisa: una collisione nega l'autenticazione. Non condividere i token tra estensioni.

In Setup → Decodium link si possono creare e revocare chiavi per ID e scegliere i permessi di lettura. La creazione e la revoca dall’interfaccia aggiornano il server immediatamente; modifiche manuali alle impostazioni richiedono invece un riavvio dell’interfaccia API. Non esiste ancora un collegamento automatico ai manifest. Le chiavi sono conservate in chiaro nelle impostazioni locali, non nel portachiavi; chi può leggere le impostazioni può usarle. I permessi limitano le richieste API, non l'accesso del codice Python a file, rete o credenziali del sistema. La disinstallazione di un'estensione non revoca automaticamente la chiave. Le API di scrittura e TX restano negate a queste credenziali.

## Le richieste

Tutte le risposte sono JSON.

### `GET /api/v1/capabilities?apiVersion=1`

Ritorna la versione del contratto e le operazioni effettivamente disponibili;
una versione richiesta diversa da `1` produce `400`. Il campo `permissionModel` vale `shared-token` per la chiave completa e
`read-only-token` per quella in sola lettura; `operations` riporta soltanto
le operazioni consentite alla chiave utilizzata. Una credenziale sperimentale per estensione restituisce `extension-token`, l’`extensionId` assegnato e i propri permessi di lettura; le altre chiavi rimangono condivise. Il campo `events` indica se la credenziale può leggere il registro eventi; controllo TX e idempotenza transazionale durevole non sono disponibili. La scoperta non costituisce isolamento delle estensioni.

### `GET /api/v1/events?log=IDENTITY&after=0&limit=100`

La chiave completa o una credenziale per estensione con `qso.events` possono leggere il registro locale; la chiave condivisa di sola lettura non ha questo permesso. Leggi `eventLogIdentity` dalle capacità e passalo come `log`; un'identità non corrispondente produce `409`. `after` è un numero decimale non negativo esclusivo, `limit` va da 1 a 500. La risposta contiene `events`, `hasMore` e `next`, con i numeri di sequenza e cursore rappresentati come stringhe per evitare perdite di precisione. Richiedere di nuovo la stessa pagina non la consuma: il client deve salvare il cursore solo dopo avere gestito gli eventi e deve tollerare le ripetizioni.

Gli eventi `added`, `updated`, `deleted` e `confirmed` includono UUID del QSO, revisione e data; una conferma include il servizio e non implica credito diploma. L'attivazione dello schema non ricostruisce eventi per QSO precedenti. Modifiche SQL dirette che non aggiornano la revisione possono non essere rilevate; la rimozione successiva di una conferma non ha un evento separato. Non esistono push, conferme di consegna o garanzie di esecuzione una sola volta. Il registro conserva una finestra massima di 100.000 numeri di sequenza; gli eventi precedenti vengono eliminati automaticamente. Questo non riduce necessariamente le dimensioni del file SQLite o dei backup. Se il cursore precede gli eventi eliminati, l'API risponde `410` con `prunedThrough` e `highWatermark`: i dati mancanti non sono recuperabili da questo registro e il client deve decidere esplicitamente se riprendere da `prunedThrough`. Un cursore oltre l'ultimo evento restituisce `409` e `highWatermark`. Copie o ripristini del log possono condividere l'identità e divergere. Proteggi e verifica i cursori dopo un ripristino o un cambio log.

#### Esempio di lettura con cursore

`examples/extensions/event_counter.py` è un esempio eseguibile separato: assegna in Setup una chiave con ID `org.decodxlog.example.event-counter` e il solo permesso `qso.events`, imposta `DECODXLOG_READ_TOKEN` nell'ambiente e avvia ad esempio `python3 examples/extensions/event_counter.py --port 9876 --state /percorso/separato/event-counter.sqlite`. Sostituisci porta e percorso con i tuoi. Il file di stato è dell'esempio e deve essere separato dal log; l'esempio rifiuta database esistenti che non riconosce. Una chiamata elabora al massimo una pagina e aggiorna conteggi e cursore nella stessa transazione locale. Se `hasMore` è vero, eseguilo nuovamente; ripetere la lettura dopo il salvataggio non somma due volte la stessa pagina.

Il programma si ferma se il log cambia, il cursore è troppo vecchio, la pagina non è valida o il database di stato non è disponibile. Non salta automaticamente eventi persi e non ripara il file di stato. I conteggi riguardano transizioni osservate dopo l'avvio del registro, non QSO unici, conferme valide per diplomi o azioni esterne. L'atomicità copre soltanto il proprio database di stato: non garantisce la consegna dell'output né l'idempotenza di operazioni in altri programmi. Il gestore sperimentale può installare `examples/extensions/event_counter.manifest.json` e avviare l'esempio: mantiene lo stato nella cartella dedicata dell'estensione tra esecuzioni e aggiornamenti. La verifica con `--verify-port` usa uno stato temporaneo e non consuma il cursore gestito. La rimozione dell'estensione cancella anche il suo stato e i conteggi: conservali separatamente se servono. La chiave API associata non viene revocata automaticamente.

### `GET /api/v1/tracker`

Restituisce l'ultima osservazione PREDICT acquisita dall'app, se disponibile,
con satellite, azimut, elevazione, Doppler a 100 MHz, istante di ricezione, numero di catalogo ed
epoca TLE. Le due righe sono controllate per lunghezza, numero di catalogo,
checksum ed epoca; il controllo non autentica la sorgente né valida l'orbita. `available` indica che esiste una lettura; `fresh` vale solo entro
10 secondi e mentre non è in corso un'altra richiesta; `orbitalFresh` indica
un'epoca TLE non futura e non più vecchia di 14 giorni. Le due letture del TLE
e del modo devono coincidere attorno alla lettura di posizione e Doppler;
l'istante di ricezione è quello della risposta di posizione. È un'istantanea, non
una prova di connessione attuale, visibilità, validità della propagazione o
possibilità di contatto. Non avvia richieste al tracker e non controlla radio,
rotore o TX. La capacità corrispondente è `tracker.read`.

### `GET /api/v1/status`

    {"app":"DecoDXLog","version":"1.17.67","log":"decodxlog","qsos":26497,
     "station":"IU8LMC","decodium":true}

### `GET /api/v1/worked?call=K1ABC&band=20m&mode=FT8`

`band` e `mode` si possono omettere.

    {"call":"K1ABC","count":3,"bands":["20m","40m"],"modes":["FT8","CW"],
     "last":"2026-09-10T12:03:00Z","dxcc":291,"country":"United States",
     "workedBand":true,"workedMode":true}

### `GET /api/v1/qsos?call=K1ABC&limit=20`

Gli ultimi QSO, dal piu' recente; senza `call` gli ultimi del log. `limit` fino a 500.
Ogni QSO ha `id` e i campi ADIF in minuscolo.

    {"qsos":[{"id":1234,"call":"K1ABC","qso_date":"20260910","time_on":"120300",
              "band":"20m","mode":"MFSK","submode":"FT2", ...}]}

### `POST /api/v1/qso`

Il corpo e' ADIF (uno o piu' record), oppure JSON `{"adif": "..."}` con
`Content-Type: application/json`. L'intestazione facoltativa `X-App` dice chi scrive:
finisce nella colonna del programma di provenienza.

    POST /api/v1/qso
    X-DecoDXLog-Token: ...
    X-App: MioProgramma

    <CALL:5>K1ABC<QSO_DATE:8>20260928<TIME_ON:4>1200<BAND:3>20m<MODE:3>FT8<EOR>

Risposta `201` se almeno un QSO e' stato registrato, `409` se no (doppioni o errori):

    {"results":[{"status":"logged","id":1235,"message":""}]}

Il QSO passa dallo stesso percorso di quelli che arrivano da Decodium: entita' DXCC,
profilo di stazione, attivazione aperta, doppioni, callbook e invio QSL automatico.

### Retry e idempotenza

Per i retry del client si può aggiungere `Idempotency-Key` (massimo 128 caratteri)
alla POST. Quando la risposta di idempotenza è stata salvata, la stessa chiave e lo stesso
corpo restituiscono la stessa risposta senza creare un secondo QSO. Riutilizzare la chiave con un corpo diverso restituisce
`409`. Per le nuove chiavi vengono confrontati anche il formato effettivo (JSON o ADIF) e il nome `X-App` normalizzato usato per la provenienza: una differenza restituisce `409`. Le chiavi salvate prima della migrazione conservano il confronto storico del solo corpo, perché il contesto originario non è disponibile. Anche senza la chiave, la deduplicazione normale del log impedisce che lo
stesso contatto venga registrato due volte; contatti distinti restano distinti.

Prima di elaborare una richiesta con chiave, l'API registra una prenotazione nel database. Una richiesta concorrente o ripetuta con la stessa chiave e lo stesso contesto, mentre la prenotazione è irrisolta, riceve `503` e `retrySafe: false`: il client deve verificare il log e non ritentare automaticamente. Questo vale anche dopo il riavvio e anche se l'arresto è avvenuto prima di salvare qualsiasi QSO. Un corpo, formato o `X-App` diverso resta un conflitto `409`.

Durante l'elaborazione l'app acquisisce anche un blocco locale per chiave accanto al database del log; il nome usa l'impronta della chiave e non la chiave in chiaro. Se il blocco è occupato o non disponibile, risponde `503` senza elaborare la richiesta. Questo richiede una directory del log scrivibile e non garantisce il coordinamento con versioni precedenti dell'app o percorsi distinti allo stesso file tramite hard link.

Le risposte completate sono persistite per 30 giorni e rimosse alla prima consultazione dopo la scadenza. Le prenotazioni irrisolte non scadono automaticamente; l'operatore può registrare una revisione manuale nella vista locale, senza stabilire l'esito della richiesta. Se il registro o la prenotazione non sono disponibili prima dell'elaborazione, l'API restituisce `503` senza elaborare la richiesta. Se il completamento della prenotazione fallisce dopo l'elaborazione, restituisce `503`, `retrySafe: false` e i risultati disponibili; uno o più QSO possono essere già stati salvati. Il QSO, la risposta e gli effetti esterni non sono ancora atomici: non usare il meccanismo come garanzia esattamente-una-volta, né aggirare una prenotazione irrisolta con una nuova chiave senza prima riconciliare il log.

In **Setup → Decodium link → Esamina richieste API irrisolte**, una vista locale mostra 50 richieste per pagina e, per ciascuna, al massimo 10 contatti estratti dalla richiesta se non supera 64 KiB. Le richieste più grandi o con più contatti sono segnalate come incomplete. La vista mostra un'impronta SHA-256 della chiave, data, sorgente e dati essenziali dei contatti; non mostra la chiave, l'identificatore interno o il corpo completo. Può includere richieste ancora in corso: confrontare con il log non è prova dell'esito remoto. Aggiorna la vista manualmente; aprire un altro log o modificarlo nasconde il risultato precedente. Le pagine non sono una fotografia immutabile: richieste completate o aggiunte mentre si naviga possono modificare i risultati. Per una richiesta con dettagli completi, l'operatore può segnare esplicitamente la revisione dopo aver controllato la richiesta originale, il log e gli eventuali effetti esterni. Il risultato originale resta sconosciuto; nessun QSO viene modificato o reinviato. Una richiesta ancora in uso o cambiata non può essere revisionata. La chiave originale resta bloccata con stato `reviewed-unknown` senza scadenza automatica; il corpo della richiesta viene sostituito logicamente da un'impronta SHA-256 per riconoscere i retry, mentre formato e sorgente restano. Questo non è una cancellazione sicura: vecchi dati possono restare in pagine SQLite, WAL o backup; impronte di contenuti prevedibili possono consentire tentativi di indovinare i dati. Le revisioni salvate da versioni precedenti possono ancora contenere il corpo originale. Non ritentare alla cieca con un'altra chiave. Le richieste con dettagli incompleti non possono essere segnate come esaminate in questa vista.

Con la chiave API completa, `GET /api/v1/idempotency` e l'header `Idempotency-Key` consentono di consultare una singola chiave: lo stato è `unresolved` (con `retrySafe: false`), `reviewed-unknown` (con `retrySafe: false`) oppure `completed` (con il codice `responseStatus` originario). `recordedAt` è la data registrata per lo stato corrente. La risposta non contiene il corpo della richiesta, la risposta originale o l'identificatore interno della prenotazione. Una chiave assente restituisce `404`, un registro non disponibile `503`; la credenziale di sola lettura riceve `403`. La consultazione non modifica o sblocca la prenotazione e non dimostra se i QSO siano stati salvati.
L'API resta spenta per impostazione predefinita e, quando attiva, ascolta solo su
`127.0.0.1`.

## Esempio di rapporto in sola lettura

`examples/extensions/recent_activity.py` è un programma Python 3 autonomo che
usa esclusivamente l'API HTTP locale, senza aprire SQLite. Crea la chiave in
sola lettura nelle impostazioni, rendila disponibile al processo come variabile
`DECODXLOG_READ_TOKEN`, quindi esegui ad esempio:

```sh
python3 examples/extensions/recent_activity.py --port 12061 --limit 100
```

Sostituisci la porta con quella configurata. Il programma richiede che l'API
confermi la chiave in sola lettura e rifiuta quella completa; la chiave non va
passata come argomento e non viene stampata. Il risultato JSON raggruppa per
banda e modo **solo il campione degli ultimi QSO** richiesto (massimo 500), non
l'intero log e non un calcolo di credito diploma. Una risposta troppo grande,
una versione incompatibile o una connessione non disponibile producono errore.
Il processo gira separatamente ma non è sandboxato; la variabile di ambiente
può essere accessibile a processi con privilegi appropriati sul computer.

## Manifest e verifica per sviluppatori

L'esempio include `examples/extensions/recent_activity.manifest.json`. Il formato
sperimentale versione 1 dichiara identificativo, versione, versione API, file
Python nella stessa cartella e permessi richiesti. L'harness accetta attualmente
solo `qso.read`, `status.read`, `worked.read` e `tracker.read`; permessi sconosciuti e TX sono
rifiutati. Il manifest non assegna da solo alcun permesso sul server.

```sh
python3 tools/extension_harness.py examples/extensions/recent_activity.manifest.json
python3 tools/extension_harness.py examples/extensions/recent_activity.manifest.json --port 12061
```

Il primo comando controlla il manifest e il file; il secondo avvia l'esempio con
`DECODXLOG_READ_TOKEN` e verifica il risultato. L'harness usa un ambiente ridotto,
un limite di tempo e controlli sulla dimensione dell'output, e termina il
processo diretto se si blocca. Su macOS/Linux avvia una sessione POSIX separata
e termina il gruppo originale anche dopo l'uscita del processo principale; un
figlio che crea una nuova sessione può comunque sfuggire. Su Windows termina
solo il processo diretto. Non è un gestore di installazione né una sandbox:
non impedisce al programma di accedere ai file o alla rete, avviare altri processi
o leggere credenziali disponibili all'utente. Usare solo codice attendibile.
Il controllo dell'output rifiuta il token esatto anche in chiavi e valori JSON
annidati o rappresentati tramite escape; non è un filtro generale per segreti,
trasformazioni arbitrarie o dati inviati dall'estensione tramite altri canali.

### Esempio di adattatore tracker

`examples/extensions/tracker_status.py --port PORTA` legge l'istantanea tramite
l'API usando `DECODXLOG_READ_TOKEN`; rifiuta la chiave completa e non invia
comandi radio. Il manifest `tracker_status.manifest.json` dichiara `tracker.read`
e può essere controllato o eseguito con `tools/extension_harness.py` come
l'esempio del rapporto. Il campo `freshForReview` riflette solo freschezza della
lettura e del TLE, non visibilità del satellite o opportunità di trasmettere.
La chiave di sola lettura è ancora condivisa e concede anche le altre letture:
il manifest non limita autonomamente i permessi del processo.

### Experimental local extension management

For trusted Python examples, `tools/extension_manager.py` manages a **dedicated** local directory; it is separate from the application and does not access the log database. Python 3 and a configured local read-only API token are required. For example:

```sh
python3 tools/extension_manager.py --root /path/to/extension-directory install examples/extensions/tracker_status.manifest.json
python3 tools/extension_manager.py --root /path/to/extension-directory list
DECODXLOG_READ_TOKEN=your_read_only_token python3 tools/extension_manager.py --root /path/to/extension-directory run org.decodxlog.example.tracker-status --port 9876
python3 tools/extension_manager.py --root /path/to/extension-directory remove org.decodxlog.example.tracker-status
```

Replace the directory, token and port with your own values; avoid putting real tokens in shell history. An update installs a new manifest version and switches the active version after files are prepared. Reinstalling identical content is idempotent; changed content requires a new version. Prior versions remain until removal, which deletes the managed extension directory, including its retained versions. The manager refuses to remove a directory without its ownership marker. Manifests may declare up to 16 local Python `dependencies`; the manager copies the entrypoint and dependencies and checks their hashes before running. Hashes detect accidental local changes, not a malicious operator able to alter both files and metadata.

New installs and reinstalls also record a hash of the installed manifest, so changes to declared permissions or other manifest fields cause the installation to be listed as invalid and prevent execution. Existing pointers without that hash remain readable for compatibility and do not gain this check until the identical trusted version is reinstalled. Older versions of the manager do not understand the new pointer field; keep a compatible manager available when downgrading. These hashes are not signatures and do not enforce per-extension permissions.

The `list` command reports an unreadable, invalid or unmanaged entry as `{"id": "directory-name", "status": "invalid"}` and continues listing other entries. This status does not establish that the directory belongs to the manager. `run` still rejects an invalid installation; `remove` still requires a valid ownership marker. Inspect the dedicated directory before taking action.

This is experimental developer tooling for trusted code, not an in-app installer, operating-system sandbox or independent per-extension permissions. The manifest is advisory: the API's shared read-only credential remains the enforcement boundary and permits its existing read capabilities. The tool does not support arbitrary packages or assets, crash-durable transactional upgrades on all filesystems, or recovery of a partially created installation. Keep the extension directory separate from the log and backups. Its subprocess timeout and process-group cleanup follow the developer harness; they do not prevent filesystem/network access or all descendant escape. The sample report remains limited to recent QSO; the tracker example is read-only and does not control CAT, rotor or TX.

Operations by cooperating manager processes on the same root take a non-blocking operating-system lock. If another operation is active, the command reports `another extension operation is in progress`; retry when it has finished. Installation verification and extension execution hold the lock for their duration; different roots are independent. On POSIX the lock is advisory on the root directory and listing or running does not require a writable root. On Windows the manager uses a lock file in the root, which must be writable. Older manager versions and external changes do not cooperate with this lock, and filesystem locking must be supported. The Windows implementation has not yet been verified on Windows. A process exit releases its OS lock; this does not guarantee that every descendant process has exited.

Before changing the active version, the experimental manager checks that the manifest selects one supported example capability and compiles each declared Python file without executing it. These checks catch unsupported capability combinations and syntax errors; they do not prove that an extension starts successfully, produces valid output or works with the current API. Test a new version with the developer harness before relying on it.

Before a managed run or an install with `--verify-port`, the manager queries the local API. For an extension credential, its assigned ID and complete read-operation set must match the manifest exactly; an incorrect or overprivileged credential is refused before the process starts. The older shared read-only credential remains supported when it contains the declared reads, but cannot enforce per-extension scope. This check is not an OS security boundary: trusted code with a token can use it independently, and grants can change between the check and execution. Installation without verification remains offline.

To check a candidate against the running local API before activating it, append `--verify-port 9876` to `install` and supply `DECODXLOG_READ_TOKEN` in the environment. The candidate runs through the same developer harness before the active version changes. A failed check leaves an existing active version selected; a failed first installation cleans up the directory created for that attempt. The default install remains offline and does not run a candidate. Verification **executes the candidate with your user privileges**: use only trusted code. A successful run validates the harness's limited output contract and that one execution completed; it does not certify future reliability, isolation, all permissions or the candidate's honesty.
