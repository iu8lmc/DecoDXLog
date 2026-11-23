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
`/api/v1/capabilities`, `/status`, `/worked` e `/qsos`; ogni scrittura o altro
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
`409`. Anche senza la chiave, la deduplicazione normale del log impedisce che lo
stesso contatto venga registrato due volte; contatti distinti restano distinti.

Le chiavi sono persistite nel database del log, con una retention di 30 giorni;
le voci scadute vengono rimosse alla prima consultazione. Il primo risultato
salvato per una chiave resta immutabile; questo non rende atomiche la scrittura
del QSO e quella del risultato. Dopo un riavvio, lo stesso retry restituisce quindi l'esito
originale senza creare un secondo QSO, se la registrazione della risposta è riuscita.
Il QSO e la risposta di idempotenza non sono ancora salvati nella stessa transazione:
un arresto o errore tra le due scritture può impedire il replay della risposta
originale. Non usare questo meccanismo come garanzia esattamente-una-volta.
L'API resta spenta per impostazione predefinita e, quando attiva, ascolta solo su
`127.0.0.1`.
