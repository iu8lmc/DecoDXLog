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

## Le richieste

Tutte le risposte sono JSON.

### `GET /api/v1/capabilities?apiVersion=1`

Ritorna la versione del contratto e le operazioni effettivamente disponibili;
una versione richiesta diversa da `1` produce `400`. Il campo `permissionModel` vale `shared-token` per la chiave completa e
`read-only-token` per quella in sola lettura; `operations` riporta soltanto
le operazioni consentite alla chiave utilizzata. Non esistono ancora permessi per estensione,
eventi, controllo TX o idempotenza transazionale durevole; i rispettivi campi
booleani sono `false`. La scoperta non costituisce isolamento delle estensioni.

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

    {"app":"DecoDXLog","version":"1.17.66","log":"decodxlog","qsos":26497,
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

Le chiavi sono persistite nel database del log, con una retention di 30 giorni;
le voci scadute vengono rimosse alla prima consultazione. Il primo risultato
salvato per una chiave resta immutabile; questo non rende atomiche la scrittura
del QSO e quella del risultato. Dopo un riavvio, lo stesso retry restituisce quindi l'esito
originale senza creare un secondo QSO, se la registrazione della risposta è riuscita.
Il QSO e la risposta di idempotenza non sono ancora salvati nella stessa transazione:
un arresto o errore tra le due scritture può impedire il replay della risposta
originale. Se il registro non è consultabile prima dell’elaborazione, l’API restituisce `503` senza elaborare la richiesta. Se la registrazione della risposta fallisce dopo l'elaborazione, l'API restituisce `503` con `retrySafe: false`, un messaggio di errore e i risultati disponibili: uno o più QSO possono già essere stati salvati e vanno verificati prima di riprovare. Un arresto prima della risposta può comunque lasciare l'esito incerto. Non usare questo meccanismo come garanzia esattamente-una-volta.
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
