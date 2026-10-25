# DecoPort per il log — i QSO verso HH, il ponte di HamHub Log

Versione del protocollo: **1** (quella di DecoPort). Questa pagina descrive i tre tipi di
pacchetto che il log aggiunge a DecoPort; il resto — intestazione, firma, finestra di tempo —
è DecoPort come lo descrive `Decodium-4.0/doc/DECOPORT_PROTOCOL.md`.

DecoPort mette in rete una radio. Qui lo stesso protocollo mette in rete **il log**: DecoDXLog
fa la parte del gateway, [HH](https://hamhub.cloud) quella del client. HH è il programma di
HamHub Log che spedisce i QSO a hamhub.cloud; prima li prendeva da DecoDXLog come N1MM in UDP
(indirizzo e porta da scrivere a mano nei due programmi), ora si registra da solo e li riceve
**firmati e confermati**.

## Cosa non cambia

- **Solo `127.0.0.1`.** Il log non va in rete: HH sta sulla stessa macchina.
- **Ogni pacchetto è firmato**, come in DecoPort: HMAC-SHA256 troncato a 128 bit in coda al
  pacchetto, chiave da PBKDF2-HMAC-SHA256 (200 000 giri, sale fisso `Decodium-DecoPort-v1`,
  32 byte) di una password condivisa. Il bit 1 di `flags` dice che la firma c'è. Senza password
  DecoDXLog non apre la sessione e HH non si registra: DecoPort non ha una modalità in chiaro.
- **Silenzio, non rifiuto**: un pacchetto senza firma giusta non riceve risposta. Cinque firme
  sbagliate o assenti dallo stesso mittente (indirizzo e porta) in 60 secondi lo fanno ignorare per
  300 secondi, con una riga nel registro attività.
- **Finestra di ±30 secondi** sull'ora del pacchetto: una firma sola non ferma un pacchetto
  registrato e rimandato.
- Un client che non si fa sentire per **12 secondi** viene lasciato.
- Gli altri client DecoPort non ne sanno niente: il parser di riferimento accetta solo i tipi
  1-9 e scarta gli altri. Per questo i tipi del log partono da 16.

## Porte

| porta | a cosa serve |
|---|---|
| **5561** | sessione del log (predefinita, si cambia nelle Impostazioni di DecoDXLog e con `--decoport PASSWORD PORTA` in HH). La 5559 è del gateway della radio, che può stare sulla stessa macchina. |
| 5560 | l'annuncio del log, a `127.0.0.1:5560` ogni due secondi, per il monitor del traffico e per chi ascolta gli annunci DecoPort |

HH non ha bisogno dell'annuncio per trovare il log: prova la 5561. L'annuncio serve a chi
guarda, e HH lo legge quando arriva come risposta a HELLO.

## Pacchetti

Intestazione di 28 byte, **big-endian**, uguale a quella di DecoPort (`DPRT`, versione 1, tipo,
`flags`, `streamId`, `sequence`, secondi e nanosecondi UNIX UTC, lunghezza del payload,
riservato). Dopo il payload, la firma di 16 byte (non contata nella lunghezza).

| tipo | nome | verso | payload |
|---|---|---|---|
| 2 | HELLO | HH → log | nessuno |
| 3 | BYE | HH → log | nessuno |
| 4 | KEEPALIVE | HH → log | nessuno |
| **16** | **LOG_ANNOUNCE** | log → HH, e → 127.0.0.1:5560 | campi dell'annuncio |
| **17** | **LOG_QSO** | log → HH | id, flag, record ADIF |
| **18** | **LOG_ACK** | HH → log | id |

### Registrazione

HH manda **HELLO** alla porta del log (e **KEEPALIVE** ogni 4 secondi una volta collegato).
Il log risponde a ognuno con un **LOG_ANNOUNCE** a quell'indirizzo: HH sa di essere registrato e
con chi parla; a ogni keepalive sa che il log c'è ancora. **BYE** toglie HH subito. Se HH tace
12 secondi, il log lo lascia.

### LOG_ANNOUNCE (16)

Come i campi di contesto di DecoPort: una maschera a 32 bit, poi i campi presenti in ordine di bit.

| bit | campo | codifica |
|---|---|---|
| 0 | programma | u8 lunghezza + UTF-8 (`DecoDXLog`) |
| 1 | versione | u8 lunghezza + UTF-8 |
| 2 | stazione | u8 lunghezza + UTF-8 (il nominativo del profilo attivo) |
| 3 | porta della sessione | u16 |

### LOG_QSO (17)

| offset | dimensione | campo |
|---|---|---|
| 0 | 8 | id del QSO nel log (int64) |
| 8 | 1 | flag: bit 0 = rimandato |
| 9 | resto | un record ADIF in UTF-8, con `<EOR>` |

Il log lo manda a ogni client registrato appena il QSO è scritto: dal pannello Nuovo QSO e da
Decodium (o da qualunque programma che mandi i QSO in UDP a DecoDXLog). Un record oltre 60 000
byte non parte e lascia una riga nel registro.

### LOG_ACK (18)

L'id del QSO ricevuto (int64). Il log aspetta la conferma: dopo 3 secondi rimanda il QSO con il
flag «rimandato», fino a tre invii in tutto, poi lo lascia e scrive nel registro che HH non l'ha
confermato. **HH conferma anche un rinvio che aveva già preso, e lo mette in coda una volta
sola** (riconosce `streamId` + id); il cloud, comunque, scarta i doppioni dello stesso QSO.

## Quando un QSO non arriva

Il log non tiene una coda per HH: se HH non è registrato quando il QSO viene scritto, il QSO
non parte su DecoPort. Resta nel log di DecoDXLog, e si può rimandare come prima, con «Rimanda questo QSO via
UDP». HH ha la sua coda su disco per quello che ha ricevuto e non è ancora arrivato al cloud.

## Impostare

1. In DecoDXLog: Impostazioni → *Decodium link* → **HamHub · QSO verso HH**. Scrivi una password
   (resta nel portachiavi del sistema, mai in un file).
2. In HH: **Configura** → *Password DecoPort*, la stessa; oppure `HH --decoport PASSWORD [PORTA]`.
3. Nell'icona di HH, il tooltip dice «DecoDXLog collegato via DecoPort». In DecoDXLog la pillola
   passa da *in attesa* a *HH collegato*, e nel registro attività compare ogni QSO inviato.

Il monitor del traffico di DecoDXLog (menu del marchio) mostra gli annunci del log sulla 5560 con
la riga leggibile (`LOG_ANNOUNCE`, programma, versione, stazione, porta).

## Verifica

`tests/tst_decoportlog.cpp` (DecoDXLog) e `HH/tests/test_hh.py` (HH) condividono due vettori
calcolati a parte con `hashlib`/`hmac` di Python: la chiave dalla password `segreto-prova`
(`f5f04a69…e419`) e un pacchetto LOG_QSO firmato con ora e id fissi. Se uno dei due programmi
cambia il formato, l'altro lo dice.
