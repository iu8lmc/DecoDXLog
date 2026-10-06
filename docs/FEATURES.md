# DecoDXLog 0.5.2 — funzioni / features

Ogni voce è in italiano e in inglese: *italiano* / *English*.
Quello che manca è in fondo.

## 1. QSO da Decodium e WSJT-X / QSO capture

- **Ricezione UDP dei QSO** da Decodium, WSJT-X, JTDX e compatibili, porta configurabile,
  anche in multicast. / **UDP capture of logged QSOs** from Decodium, WSJT-X, JTDX and
  compatible programs, configurable port, multicast supported.
- **LoggedADIF preferito a QSOLogged**: il messaggio completo non perde campi; se arrivano
  entrambi si usa uno solo. / **LoggedADIF preferred over QSOLogged**: the full message
  loses no field; if both arrive only one QSO is written.
- **Recupero dal log di Decodium**: ogni 5 minuti i QSO registrati in `decodium_log.adi`
  dall'ultimo controllo che mancano (DecoDXLog chiuso, porta sbagliata, rete giù) si
  salvano; quelli corretti o cancellati qui, o finiti in un altro log dell'elenco, no. A
  mano anche gli ultimi 30 giorni o tutto il file. / **Recovery from the Decodium log**:
  every 5 minutes the QSOs written in `decodium_log.adi` since the last check that are
  missing (DecoDXLog closed, wrong port, network down) are saved; those corrected or
  deleted here, or that went to another log of the list, are not. By hand also the last
  30 days or the whole file.
- **FT2 di prima classe**: `MODE=MFSK`, `SUBMODE=FT2`, e "FT2" ovunque nell'interfaccia. /
  **FT2 as a first-class mode**: `MODE=MFSK`, `SUBMODE=FT2`, shown as "FT2" everywhere.
- **Profilo stazione dal nominativo** del QSO, altrimenti quello attivo. / **Station profile
  from the QSO callsign**, otherwise the active one.
- **Entità DXCC aggiunta al volo** (DXCC, paese, zone, continente). / **DXCC entity filled
  on the fly** (DXCC, country, zones, continent).
- **Finestra dei duplicati** configurabile, diversa per QSO digitali e a mano. / **Duplicate
  window**, configurable, different for digital and manual QSOs.
- **Il nominativo che Decodium sta lavorando** segue nella scheda a destra. / **The DX call
  Decodium is working** follows in the call info panel.
- **Stato del client**: chi è collegato, frequenza, modo, TX, e l'avviso se tace. / **Client
  state**: who is connected, frequency, mode, TX, and a warning when it goes quiet.
- **Ripetitore UDP**: quello che arriva da Decodium va anche ad altri programmi, e le loro
  risposte tornano a Decodium. / **UDP relay**: what comes from Decodium also goes to other
  programs, and their answers go back to Decodium.
- **N1MM Logger+** (ContactInfo via UDP), **log ADIF tenuti d'occhio** (fldigi, WSJT-X,
  JTDX) e un'**interfaccia HTTP locale** con chiave (`docs/API.md`). / **N1MM Logger+**
  (ContactInfo over UDP), **watched ADIF logs** (fldigi, WSJT-X, JTDX) and a **local HTTP
  interface** with a key (`docs/API.md`).

## 2. Il log / The log

- **SQLite in modalità WAL**, nomi di colonna ADIF, un file solo, nessun server. / **SQLite
  in WAL mode**, ADIF column names, a single file, no server.
- **Import/export ADIF senza perdite**: i campi senza colonna restano in `adif_extra` e
  tornano fuori identici. / **Lossless ADIF import/export**: fields without a column live in
  `adif_extra` and come back out unchanged.
- **Storico delle revisioni**: ogni modifica è una nuova revisione, le precedenti si
  rileggono e si ripristinano. / **Revision history**: every edit is a new revision, earlier
  ones can be read and restored.
- **Cancellazione morbida**: il QSO resta nello storico e si recupera. / **Soft delete**: the
  QSO stays in history and can be recovered.
- **Pronto per il sync**: `uuid`, `revision`, `dirty` su ogni QSO. / **Ready for sync**:
  `uuid`, `revision`, `dirty` on every QSO.
- **Stato QSL per servizio** (LoTW, QRZ, Club Log, eQSL, cartolina) in una tabella a parte. /
  **QSL state per service** (LoTW, QRZ, Club Log, eQSL, card) in its own table.
- **Etichette sui QSO** (`APP_DECOLOG_TAGS`): attivazione, contest, evento, portatile. /
  **Tags on QSOs** (`APP_DECOLOG_TAGS`): activation, contest, event, portable.
- **Schema versionato con migrazione** (v2), senza toccare i QSO. / **Versioned schema with
  migration** (v2), leaving the QSOs alone.

## 3. Logbook

- **Tabella del log** con colonne a scelta, nascondibili, e QSL in colonna (L Q C E):
  UTC, nominativo, banda, frequenza, modo, RST, locatore, nome, **citta' / QTH, nazione,
  stato, contea, zona CQ, zona ITU, IOTA**, DXCC, QSL, origine, etichette. /
  **Log table** with the columns you want, hideable, and QSL shown as L Q C E: UTC, call,
  band, frequency, mode, RST, grid, name, **city / QTH, country, state, county, CQ zone,
  ITU zone, IOTA**, DXCC, QSL, source, tags.
- **Finestra separata ("Stacca")** per un secondo monitor. / **Separate window ("Pop")** for
  a second screen.
- **Filtri**: testo, banda, modo, mese, intervallo di date, entità DXCC, stato QSL, profilo
  stazione, etichetta. / **Filters**: text, band, mode, month, date range, DXCC entity, QSL
  state, station profile, tag.
- **Filtri salvati** con un nome, da riapplicare. / **Saved filters** you can name and apply
  again.
- **Correzione sul posto**: F2 sulla riga corregge il nominativo (o la cella cliccata, se si
  scrive a mano) senza aprire la scheda; il nominativo si corregge anche nella scheda del QSO. Se
  cambia, nazione e zone si rifanno e i servizi che avevano il QSO sbagliato, senza conferma, lo
  rimandano. / **Edit in place**: F2 on a row corrects the call (or the clicked cell, if typed by
  hand) without opening the card; the call can also be corrected in the QSO card. When it changes,
  country and zones are worked out again and services that had the wrong QSO, unconfirmed, get it
  again.
- **Azioni sulle righe mostrate**: etichetta di gruppo, togli etichetta, export ADIF. /
  **Actions on the rows shown**: tag them all, remove a tag, export to ADIF.
- **Export di un periodo** (dal… al…): lo stesso riquadro dello scarico da LoTW, con il
  conto dei QSO prima di salvare; si apre dal menu File e dalle azioni del log. / **Export
  of a period** (from… to…): the same box as the LoTW download, with the QSO count before
  saving; opens from the File menu and from the log actions.
- **Menu della riga**: apri, cancella, completa dal callbook, filtra per nominativo,
  filtra per entità, etichetta. / **Row menu**: open, delete, complete from the callbook,
  filter by call, filter by entity, tag.
- **QSL a parole**: il mouse sopra le lettere L Q C E dice com'e' andata con quel servizio
  — confermata, inviata in attesa, non inviata. / **QSL in words**: hovering the L Q C E
  letters says how it went with that service — confirmed, sent and waiting, not sent.
- **Colonne che si tirano**: il bordo fra due intestazioni si trascina, e la larghezza
  resta (per nome di colonna, anche nel log staccato). / **Draggable columns**: the border
  between two headers drags, and the width stays (by column name, in the detached log too).
- **Conferme QSL a gruppo**: scelte piu' righe, il tasto destro le manda a LoTW, eQSL, QRZ
  Logbook o Club Log, o le mette in coda per il bureau o la diretta; quelle gia' inviate a
  quel servizio non ripartono. / **QSL confirmations in bulk**: with several rows chosen,
  the right button sends them to LoTW, eQSL, QRZ Logbook or Club Log, or queues them for
  the bureau or direct; the ones already sent to that service don't go again.
- **Selezione multipla**: il clic sinistro aggiunge o toglie una riga, lo shift prende
  tutto quello che sta in mezzo, **Esc** lascia andare la selezione; il tasto destro le
  cancella tutte, chiedendo **due volte**. / **Multiple selection**: left click adds or
  removes a row, shift takes everything in between, **Esc** drops the selection; the right
  button deletes them all, asking **twice**.
- **Tastiera**: frecce, Invio per aprire, Ctrl+N nuovo QSO, Ctrl+F cerca, Ctrl+I import,
  Ctrl+E export, Ctrl+K cluster, Ctrl+T attivazione. / **Keyboard**: arrows, Enter to open,
  Ctrl+N new QSO, Ctrl+F search, Ctrl+I import, Ctrl+E export, Ctrl+K cluster, Ctrl+T
  activation.
- **Importa da altri programmi**: ADIF, fogli di calcolo CSV (colonne riconosciute da sole)
  e il database di N1MM Logger+. / **Import from other programs**: ADIF, CSV spreadsheets
  (columns recognised by themselves) and the N1MM Logger+ database.
- **Un campo su molti QSO**, con lo storico di ognuno, e **i doppioni** trovati e uniti. /
  **One field on many QSOs**, with each one's history, and **duplicates** found and merged.

## 4. Nuovo QSO / New QSO

- **Pannello rapido** e **scheda completa** (Ctrl+N) con tutti i campi ADIF utili. / **Quick
  panel** and **full form** (Ctrl+N) with every useful ADIF field.
- **Ora UTC con un clic**, banda dalla frequenza, RST predefiniti. / **UTC time with one
  click**, band from frequency, default RST.
- **Il callbook riempie solo i campi vuoti** (nome, QTH, locatore). / **The callbook fills
  only empty fields** (name, QTH, grid).
- **Due callbook invece di uno**: se quello scelto non conosce il nominativo, o risponde
  senza dire dove sta la stazione, si chiede anche all'altro e le due risposte si mettono
  insieme. / **Two callbooks instead of one**: if the chosen one doesn't know the call, or
  answers without saying where the station is, the other one is asked too and the two
  answers are merged.
- **Lavori sul log intero** dal menu Azioni: completare dal callbook tutti i QSO senza
  locatore (in coda, una ricerca per volta, fermabile) e ripulire i QSO rovinati da un
  vecchio import ADIF. / **Whole-log jobs** from the Actions menu: complete every QSO
  without a grid from the callbook (queued, one at a time, stoppable) and clean up the QSO
  damaged by an old ADIF import.
- **Il locatore anche quando il callbook non lo scrive**: si ricava dalla posizione della
  stazione; e un locatore a quattro caratteri diventa quello a sei se il callbook conferma
  lo stesso quadrato. Un quadrato diverso resta com'e'. / **The grid even when the callbook
  doesn't write it**: it is derived from the station's position; a four-character grid
  becomes the six-character one when the callbook confirms the same square. A different
  square is left alone.
- **Il QSO appena scritto si completa da solo**: nome, QTH, locatore, indirizzo, stato,
  contea e zone arrivano dal callbook subito dopo il log, senza toccare quello che c'e'
  gia'. Una ricerca per nominativo, e si puo' rifare a mano sui QSO vecchi (dal menu di una
  riga o su tutte quelle mostrate). / **The QSO just logged completes itself**: name, QTH,
  grid, address, state, county and zones come from the callbook right after logging, never
  overwriting what is already there.
- **Quello che il log sa del nominativo** mentre lo scrivi: già lavorato, nuovo DXCC, nuovo
  su questa banda. / **What the log knows about the call** as you type: worked before, new
  DXCC, new on this band.
- **Etichette** e, nei contest, **numero inviato e ricevuto**. / **Tags** and, in contests,
  **serial sent and received**.
- **Registra e continua** per una serie di QSO. / **Log & keep** for a run of QSOs.

## 5. Scheda del QSO / QSO detail

- **Tutti i campi ADIF** divisi in Generale, Posizione, QSL, ADIF extra, Storico. / **All
  ADIF fields** split into General, Location, QSL, ADIF extra, History.
- **Campi liberi**: si aggiungono e restano nell'export. / **Free fields**: you can add them
  and they survive the export.
- **QSL per servizio** con inviato, ricevuto e date. / **QSL per service** with sent,
  received and dates.
- **Effetto sui diplomi**: questo QSO è un nuovo DXCC in FT2? / **Award impact**: is this QSO
  a new DXCC on FT2?
- **Distanza e azimut** dal locatore. / **Distance and bearing** from the grid.
- **Export del singolo QSO**, ripristino di una revisione, cancellazione morbida. / **Export
  of the single QSO**, restore of a revision, soft delete.

## 6. Profili stazione / Station profiles

- **Più stazioni** (casa, portatile, evento) con nominativo, operatore, locatore, zone,
  radio, antenna, potenza, station location di LoTW. / **Several stations** (home, portable,
  event) with callsign, operator, grid, zones, rig, antenna, power, LoTW station location.
- **Profilo predefinito e profilo attivo**, duplicazione, conteggio dei QSO. / **Default and
  active profile**, duplication, QSO count.
- **Primo profilo creato da Decodium** al primo avvio. / **First profile created from
  Decodium** on the first run.

## 7. Entità DXCC / DXCC entities

- **cty.csv di AD1C incluso**, aggiornabile dalle impostazioni. / **AD1C cty.csv bundled**,
  updatable from the settings.
- **Completa i DXCC mancanti** nei QSO importati, ognuno come nuova revisione. / **Fill
  missing DXCC** on imported QSOs, each as a new revision.

## 8. Scheda del nominativo / Call info

- **Già lavorato**: quante volte, su quali bande e modi, ultimi cinque QSO. / **Worked
  before**: how many times, on which bands and modes, last five QSOs.
- **Nuovo DXCC / nuovo su banda** mentre Decodium lavora. / **New DXCC / new on band** while
  Decodium is working.
- **Distanza, azimut, ora locale approssimata**. / **Distance, bearing, approximate local
  time**.
- **Callbook QRZ.com (XML) o HamQTH**: nome, QTH, locatore, zone, foto, utente LoTW/eQSL,
  risultati tenuti un giorno. / **QRZ.com (XML) or HamQTH callbook**: name, QTH, grid, zones,
  photo, LoTW/eQSL user, results cached for a day.
- **Griglia banda per modo**, due volte: il nominativo e la sua entita' DXCC. Una riga per
  CW, digitale e fonia, una colonna per banda; casella vuota mai lavorata, arancione
  lavorata, verde confermata con le lettere di chi ha confermato (`L` LoTW, `e` eQSL, `C`
  Club Log, `Q` QRZ, `K` cartolina). / **Band by mode grid**, twice: the callsign and its
  DXCC entity. One row for CW, digital and phone, one column per band; an empty cell means
  never worked, amber worked, green confirmed with the letters of who confirmed it.
- **Stato QSL dell'ultimo QSO**. / **QSL state of the last QSO**.

## 8bis. Radio e CW / Radio and CW

- **La radio via Hamlib**: DecoDXLog parla con **rigctld**, legge frequenza e modo, sposta il
  VFO e comanda il PTT. Niente porte seriali: quelle le sa gia' Hamlib. / **The radio through
  Hamlib**: DecoDXLog talks to **rigctld**, reads frequency and mode, moves the VFO and keys
  the PTT. No serial ports: Hamlib already knows them.
- **Il pannello CW**, da solo o staccato in finestra — e staccato **sta davanti alle altre
  finestre**, che mentre si manipola serve: macro, velocita', riga per mandare
  quello che si scrive e decoder. / **The CW panel**, docked or in its own window: macros,
  speed, a line to send what you type, and the decoder.
- **Decoder CW dentro DecoDXLog** (ggmorse, MIT): legge l'audio che esce dalla radio, trova
  il tono da solo fra 200 e 1200 Hz e misura la velocita' mentre ascolta. Sulla banda vuota
  sta zitto: quello che non somiglia abbastanza al Morse non passa. / **CW decoder inside
  DecoDXLog** (ggmorse, MIT): it reads the audio coming out of the radio, finds the tone by
  itself between 200 and 1200 Hz and measures the speed while listening. On an empty band it
  stays quiet: what does not look enough like Morse does not get through.
- **La radio anche col cavo**: modello Hamlib + porta seriale, e rigctld lo avvia DecoDXLog; il
  **PTT** puo' stare su un'altra porta (RTS o DTR), come vuole chi ha due COM. /
  **The radio over the cable too**: Hamlib model + serial port, and DecoDXLog starts rigctld.
- **Cerca la radio**: prova porte e velocita' una per una finche' una risponde, e tiene
  quella. / **Find the radio**: it tries ports and speeds one by one until one answers, and
  keeps that one.
- **Otto macro CW** sui tasti F1-F8 nella finestra contest, col testo che si scrive a mano e
  i buchi riempiti sul momento ({CALL}, {MYCALL}, {RST}, {NR}, {EXCH}); la velocita' in
  parole al minuto si cambia mentre si opera, **Esc** ferma. Il manipolatore e' quello della
  radio. / **Eight CW macros** on F1-F8 in the contest window, with the text you write and
  the gaps filled in at the moment; the speed in words per minute changes while you operate,
  **Esc** stops. The keyer is the radio's own.
- **Split, VFO A/B, RIT e XIT** con rigctld, TCI e flrig, dalla barra in alto. / **Split,
  VFO A/B, RIT and XIT** with rigctld, TCI and flrig, from the top bar.

## 9. Diplomi / Awards

- **Calcolati dal log**: DXCC, **DXCC Challenge**, FT2 Award, **WAC**, **WAAC**, WAZ, WAS,
  **WAJA**, **AJD**, **JCC**, **JCG**, WPX, locatori, IOTA, POTA, SOTA, WWFF. / **Computed
  from the log**: DXCC, **DXCC Challenge**, FT2 Award, **WAC**, **WAAC**, WAZ, WAS, **WAJA**,
  **AJD**, **JCC**, **JCG**, WPX, grids, IOTA, POTA, SOTA, WWFF.
- **JCC** e **JCG**: le citta' e i distretti (gun) del JARL, presi dal numero che sta nel
  campo **CNTY** — quattro cifre (sei per i quartieri) sono una citta', cinque un gun; le
  prime due dicono la prefettura, che fa da nome. Traguardo cento e cento, banda per banda
  come gli altri. / **JCC** and **JCG**: JARL cities and guns, read from the **CNTY** field —
  four digits (six for wards) is a city, five is a gun; the first two give the prefecture,
  used as the name. Target 100 each, per band like the others.
- **WAAC** (Worked All Africa): le entita' africane, col traguardo che il cty.csv stesso
  dichiara. / **WAAC** (Worked All Africa): the African entities, with the target the
  country file itself declares.
- **WAC**: i sei continenti dell'IARU, con l'Antartide che si vede ma non fa numero; si
  legge anche banda per banda, perche' il diploma si fa su piu' bande. / **WAC**: the six
  IARU continents, Antarctica shown but not counted; per band as well.
- **WAJA e AJD**: le 47 prefetture giapponesi (da `STATE`, comunque sia scritta) e i dieci
  distretti (dalla cifra del nominativo). / **WAJA and AJD**: the 47 Japanese prefectures
  and the ten districts.
- **DXCC Challenge**: gli stessi DXCC contati banda per banda dai 160 ai 6 metri (undici
  bande), traguardo mille slot. / **DXCC Challenge**: band slots from 160 to 6 m, eleven
  bands, a thousand to reach.
- **Tabella per banda** (● confermato, ○ lavorato) con totali per banda e **band slot**. /
  **Per-band table** (● confirmed, ○ worked) with per-band totals and **band slots**.
- **Quello che manca**: entità, zone e stati mai lavorati. / **What is missing**: entities,
  zones and states never worked.
- **Mappa dei locatori** lavorati e confermati. / **Grid map** of worked and confirmed
  squares.
- **Filtri**: banda, gruppo di modi, profilo stazione, etichetta; conferme accettate a scelta
  (LoTW, cartolina, eQSL). / **Filters**: band, mode group, station profile, tag; which
  confirmations count (LoTW, card, eQSL).
- **Pannello FT2 Award** sempre in vista. / **FT2 Award panel** always in sight.
- **Conferme valide per diploma** e **entita' con le date** dal cty.xml di Club Log
  (operazioni non accettate, entita' cancellate). / **Valid confirmations per award** and
  **entities with their dates** from Club Log's cty.xml (operations not accepted, deleted
  entities).

## 10. QSL

- **Conferme LoTW scaricate** da `lotwreport.adi`, solo le nuove dall'ultimo sync, a mano o
  ogni 6/12/24 ore. / **LoTW confirmations downloaded** from `lotwreport.adi`, only the new
  ones, manually or every 6/12/24 hours.
- **Conferme eQSL e QRZ Logbook scaricate** a mano (scheda "Invio QSL" o Impostazioni →
  Servizi QSL) o ogni 6/12/24 ore: da eQSL le eQSL arrivate nella casella (senza le
  segnalazioni SWL), da QRZ i QSO del proprio logbook che l'altro ha confermato; solo le
  nuove dall'ultimo scarico. / **eQSL and QRZ Logbook confirmations downloaded** by hand
  ("QSL Upload" tab or Setup → QSL services) or every 6/12/24 hours: the eQSLs in the inbox
  (without SWL reports) and the QSOs of one's QRZ logbook confirmed by the other station;
  only the new ones since the last download.
- **Abbinamento** per nominativo, banda, gruppo di modi e ora entro mezz'ora, come fa LoTW;
  i dettagli riempiono solo i campi vuoti. / **Matching** by call, band, mode group and time
  within 30 minutes, as LoTW does; details fill only empty fields.
- **Invio a LoTW** facendo firmare un ADIF temporaneo al TQSL installato. / **Upload to
  LoTW** by having the installed TQSL sign a temporary ADIF.
- **Invio a QRZ Logbook** (chiave API) e **eQSL** (utente e password). / **Upload to QRZ
  Logbook** (API key) and **eQSL** (user and password).
- **Invio a Club Log**: il QSO appena registrato parte da solo, l'arretrato parte come un
  unico ADIF; servono email, password, nominativo e chiave API. / **Upload to Club Log**: a
  QSO just logged leaves on its own, a backlog leaves as one ADIF; it needs email, password,
  callsign and an API key.
- **A mano o automatico** dopo ogni QSO, con coda e conteggi per servizio. / **Manual or
  automatic** after each QSO, with a queue and per-service counters.
- **Duplicato = inviato**, rifiuto scritto sul QSO con il motivo. / **A duplicate counts as
  sent**, a rejection is written on the QSO with its reason.
- **Account eQSL e QRZ per profilo stazione**, e **Wavelog** fra i servizi. / **eQSL and QRZ
  accounts per station profile**, and **Wavelog** among the services.

## 10b. QSL di carta / Paper QSL

- **Coda** delle cartacee: da mandare, mandate, ricevute, e quante aspettano risposta. /
  **Queue** of paper cards: to send, sent, received, and how many await an answer.
- **In coda** dal menu della riga del log (bureau o diretta) o tutte insieme quelle da
  ricambiare. / **Into the queue** from the log row menu (bureau or direct), or every card
  waiting for an answer at once.
- **Etichette in PDF**: una per corrispondente con fino a sei QSO, quattro fogli in
  commercio, segni di taglio a scelta, senza stampante di mezzo. / **PDF labels**: one per
  correspondent with up to six QSOs, four off-the-shelf sheets, optional cutting guides, no
  printer in the way.
- **La QSL per email**: l'indirizzo lo prende dal callbook (QRZ.com o HamQTH), disegna la
  cartolina di quel QSO e la manda dalla propria casella come allegato PNG; solo su
  connessione cifrata, e chi non ha l'email nel callbook si salta dicendolo. / **QSL by
  email**: the address comes from the callbook (QRZ.com or HamQTH), the card for that QSO is
  drawn and sent from your own mailbox as a PNG attachment; encrypted connections only, and
  stations with no email in the callbook are skipped and named.
- **Gli otto di serie**: un pulsante mette nominativo, giorno, mese, anno, UTC, MHz, modo e
  RST nei riquadri dove una QSL li ha di solito, misurati sulla cartolina invece che a
  occhio; quelli gia' posati si spostano li' invece di sdoppiarsi. / **The usual eight**: one
  button puts callsign, day, month, year, UTC, MHz, mode and RST in the boxes where a QSL
  usually has them; fields already placed move there instead of doubling up.
- **La cartolina QSL**: si carica l'immagine della propria QSL e ci si posano sopra i campi
  trascinandoli col mouse — nominativo, data (intera o spezzata in giorno, mese e anno),
  UTC, MHz, banda, modo, RST, nome, QTH, locatore, i propri dati e il testo libero. Di
  ognuno si sceglie corpo, grassetto, colore e allineamento. Esce un PDF (una, due o
  quattro per foglio A4) o un PNG per QSO. Quello che si vede nell'anteprima e' quello che
  si stampa. / **The QSL card**: load the image of your own card and drop the fields on it,
  dragging them with the mouse — callsign, date (whole or split into day, month and year),
  UTC, MHz, band, mode, RST, name, QTH, grid, your own details and free text. Each one has
  its size, weight, colour and alignment. Out comes a PDF (one, two or four to an A4 sheet)
  or one PNG per QSO. What the preview shows is what gets printed.
- **La via** (bureau, diretta, elettronica) resta sul QSO e torna nell'export come
  `QSL_SENT_VIA`. / **The route** (bureau, direct, electronic) stays on the QSO and comes
  back in the export as `QSL_SENT_VIA`.

## 10c. Contest / Contest

- **Finestra da tastiera** (Ctrl+Shift+T): nominativo, Invio registra, Esc pulisce, la barra
  passa al rapporto. / **Keyboard window** (Ctrl+Shift+T): callsign, Enter logs, Esc clears,
  space moves to the report.
- **Doppio in evidenza** mentre si scrive: stesso nominativo, stessa banda, stesso modo
  dentro la sessione. / **Dupe shown while typing**: same call, same band, same mode inside
  the session.
- **Ritmo**: QSO degli ultimi dieci minuti e dell'ultima ora, QSO/h, DXCC e locatori
  entrati. / **Rate**: QSOs in the last ten minutes and the last hour, QSOs/h, DXCC and
  grids worked.
- **Numero progressivo** dalla sessione, scambio ricevuto in un campo solo. / **Serial
  number** from the session, received exchange in one field.
- **Export Cabrillo 3.0**: testata con categorie, locatore, punteggio e soapbox, righe QSO a
  colonne fisse, frequenze in kHz (numero di banda dai 6 metri in su), modi CW/PH/RY/DG/FM. /
  **Cabrillo 3.0 export**: header with categories, grid, score and soapbox, fixed-column QSO
  lines, frequencies in kHz (band number from 6 m up), modes CW/PH/RY/DG/FM.
- **Regole per edizione**, tempo in aria e pause come da regolamento, **call history** in
  formato N1MM e **progressivi distribuiti** in rete. / **Per-edition rules**, time on the
  air and breaks as the rules say, **call history** in N1MM format and **serial numbers
  shared** on the network.

## 10d. Propagazione / Propagation

- **Numeri del Sole**: SFI, macchie, indice A e K, aurora, raggi X, campo geomagnetico,
  rumore, vento solare. / **Solar numbers**: SFI, sunspots, A and K index, aurora, X-ray,
  geomagnetic field, noise, solar wind.
- **Condizioni banda** di giorno e di notte, più aurora ed E-skip in VHF, colorate. / **Band
  conditions** for day and night, plus aurora and E-skip on VHF, colour-coded.
- **Da sole ogni ora** o a comando, dal XML di N0NBH (hamqsl.com). / **By themselves every
  hour** or on demand, from N0NBH's XML (hamqsl.com).
- **Storico accanto ai tuoi QSO**: ultimi quattordici giorni, barra dei QSO e flusso medio
  del giorno. / **History next to your QSOs**: last fourteen days, bar of QSOs and the day's
  average flux.
- **SFI e K in testa alla mappa**, dove si guarda la propagazione. / **SFI and K on the map
  header**, where propagation is looked at.
- **VOACAP** (ITS) nella previsione sul percorso: per ogni ora e banda la probabilita' del QSO
  nel modo scelto, con potenza, antenne e rumore del posto. / **VOACAP** (ITS) in the path
  forecast: for each hour and band the chance of the QSO in the chosen mode, with power,
  antennas and local noise.

## 10e. Rotore / Rotor

- **DecoRotor sul WebSocket** (8765) o un **rotctld** qualsiasi (4532): DecoDXLog non tocca la
  seriale. / **DecoRotor over WebSocket** (8765) or any **rotctld** (4532): DecoDXLog never
  touches the serial port.
- **Il quadrante di DecoRotor**: corona graduata, mappa azimutale del proprio QTH con cerchi
  di distanza, lobo d'antenna, bersaglio e ago; si punta cliccandoci dentro. / **DecoRotor's
  dial**: graduated ring, azimuthal map of your QTH with range rings, antenna lobe, target and
  needle; click inside it to point.
- **Il posto di comando** (Ctrl+R): la pagina "Controllo" di DecoRotor rifatta com'e' —
  spie, quadrante, mappa satellitare con i riquadri del gateway, display con CCW/CW, sei
  memorie a tasto diretto, passi con STOP al centro, PARK, elenco memorie, puntamento a
  gradi e per locatore (rotta breve o lunga). / **The control desk** (Ctrl+R): DecoRotor's
  "Controllo" page as it is — status lamps, dial, satellite map with the gateway's tiles,
  display with CCW/CW, six direct memories, steps with STOP in the middle, PARK, the memory
  list, pointing by degrees and by locator (short or long path).
- **Diagnostica e impostazioni del gateway**: frame della seriale con l'esadecimale,
  andamento della posizione, contatori, indirizzi di rete; nominativo, locatore, lobo,
  finecorsa, riposo e tolleranza scritti nel config.json del gateway. / **Gateway
  diagnostics and settings**: serial frames with their hex, position trace, counters,
  network addresses; callsign, locator, beamwidth, limits, park and tolerance written into
  the gateway's config.json.
- **Dal cluster**: il menu di uno spot punta il rotore sui gradi già calcolati. / **From the
  cluster**: a spot's menu points the rotor at the bearing already computed.
- **Sul DX** che si sta lavorando, a mano o seguendolo da solo. / **On the DX** being worked,
  by hand or following it by itself.
- **Sulla mappa** si vede dove guarda l'antenna. / **On the map** you see where the antenna
  is pointing.

## 10f. Sync fra dispositivi / Sync between devices

- **DecoDXLog Cloud** (`server/`): FastAPI, SQLite per provarlo e PostgreSQL in servizio,
  Docker pronto. / **DecoDXLog Cloud** (`server/`): FastAPI, SQLite to try it and PostgreSQL
  in production, Docker ready.
- **Accesso** con nominativo e password; DecoDXLog tiene solo il token, nel portachiavi. /
  **Sign in** with callsign and password; DecoDXLog keeps only the token, in the keystore.
- **Un giro di sync**: prima quello che è cambiato altrove, poi quello che è in coda. / **A
  sync round**: first what changed elsewhere, then what is queued.
- **Conflitti**: vince l'ultima modifica, quella che perde resta nello storico; una modifica
  locale non ancora mandata non viene sovrascritta. / **Conflicts**: last edit wins, the
  loser stays in the history; a local change not yet sent is never overwritten.
- **Duplicati** con un altro uuid riconosciuti, **cancellazioni** che viaggiano come
  modifiche. / **Duplicates** under another uuid recognised, **deletions** travelling as
  changes.
- **Coda sempre in vista** nella barra in alto e nella riga di stato. / **The queue in
  sight** in the top bar and in the status rail.
- **Non solo i QSO**: viaggiano anche i **profili stazione** e **tutte le impostazioni** —
  tema, lingua, colonne e filtri salvati del log, cluster, premi, invii automatici,
  propagazione, rotore, dedup UDP, backup, e anche porte, percorsi e indirizzi dei
  programmi accanto: il secondo computer si ritrova la stessa stazione, non una che le
  somiglia. / **Not only the QSOs**: **station profiles** and **every setting** travel too
  — theme, language, log columns and saved filters, cluster, awards, automatic uploads,
  propagation, rotor, UDP dedup, backup, and ports, paths and the addresses of the programs
  next door: the second computer finds the same station, not one that resembles it.
- **Il profilo attivo per uuid**, non per numero di riga: si accende lo stesso profilo
  anche dove ha un altro numero. / **The active profile by uuid**, not by row number: the
  same profile lights up even where its number differs.
- **Anche le password dei servizi** (QRZ, LoTW, Club Log, eQSL, HamQTH, HamAlert), ma
  **chiuse**: AES-256-GCM con una chiave da PBKDF2 sulla password del Cloud, che il server
  conosce solo come impronta Argon2 — al server arriva un blocco che senza quella password
  non si apre. Interruttore in Impostazioni → Sync e Cloud. / **The service passwords too**
  (QRZ, LoTW, Club Log, eQSL, HamQTH, HamAlert), but **sealed**: AES-256-GCM with a key
  from PBKDF2 over your Cloud password, which the server only knows as an Argon2
  fingerprint — what reaches the server does not open without that password. A switch in
  Settings → Sync & Cloud.
- **Quello che resta a casa**, e non e' una scelta di stile: il promemoria di cosa e'
  salvato nel portachiavi di *quella* macchina, e il quaderno del sync. / **What stays
  home**, and it is not a matter of taste: the note of what is stored in *that* machine's
  keystore, and the sync's own logbook.
- **Il tema arriva e si vede**: le impostazioni ricevute valgono subito, senza riavviare. /
  **The theme arrives and shows**: settings received apply at once, with no restart.
- **Il log dal browser e' la stessa finestra**: barra superiore a blocchi, tre colonne
  (scheda del QSO, log, scheda del nominativo con FT2 Award e mappa), le sei schede in
  basso — Diplomi, Statistiche, Invio QSL, Registro attivita', DX Cluster, Propagazione —
  e la barra di stato. Si sceglie un QSO e le colonne seguono. / **The log from the browser is the same
  window**: the block top bar, three columns (QSO sheet, log, call sheet with FT2 Award and
  map), the five bottom tabs and the status rail. Pick a QSO and the side panels follow.
- **Con i colori della stazione**: il tema arriva dalle impostazioni sincronizzate — Ocean
  Blue, Stellar Light, Darkcodium con la sua variante d'accento e la densita' delle righe. /
  **Wearing the station's colours**: the theme comes from the synced settings — accent
  variant and row density included.
- **Dentro le schede**: statistiche (anni, mesi, ore UTC, bande, modi, continenti, mappa di
  calore banda per ora), diplomi (DXCC, FT2, WAZ, WAS, WPX, locatori, IOTA, POTA, SOTA,
  WWFF, per banda, con quello che manca), QSL per servizio con la **coda delle cartacee** e
  la via, il registro di cosa e' arrivato sul Cloud e da quale dispositivo, le fonti del
  cluster, la **propagazione** dalla stessa fonte del programma, e il tasto per riscaricare
  tutto in ADIF. / **In the tabs**: statistics, awards, QSL with the **paper queue**, the
  arrival log, the cluster sources, **propagation** from the program's own source, plus the
  button to take it all back as ADIF.
- **Gli stessi numeri da tutte e due le parti**: i conti si rifanno dai QSO con le regole
  del programma (`analytics.py` e' `Awards.cpp` portato in Python), non da tabelle di
  riepilogo: una correzione a un QSO si vede subito da tutte e due le facce del log. /
  **The same figures on both sides**: they are recomputed from the QSOs with the program's
  own rules, never from summary tables.
- **Log condiviso**: un club o una gara multi-operatore, piu' operatori con il proprio
  account, inviti con codice, operatore o osservatore. / **Shared log**: a club or a
  multi-operator contest, several operators with their own account, invites by code,
  operator or viewer.

## 11. DX cluster

- **Fonti in un elenco solo**: nodi telnet (DX Spider, CC Cluster), Reverse Beacon Network
  (CW/RTTY e FT8/FT4), HamAlert, attivazioni POTA. / **All sources in one list**: telnet
  nodes (DX Spider, CC Cluster), Reverse Beacon Network (CW/RTTY and FT8/FT4), HamAlert,
  POTA activations.
- **Login automatico** col nominativo del profilo (come `CALL-2`, per non chiudere la
  sessione di Decodium), riconnessione da sola. / **Automatic login** with the profile
  callsign (as `CALL-2`, so Decodium keeps its own session), self reconnection.
- **Spot verso il cluster**: riquadro «Segnala una stazione» dal QSO nuovo (SPOT), dall'elenco
  di Decodium (frequenza in aria + scarto audio), dalla finestra del cluster e dal menu;
  controlla nominativo, bande e commento (30 caratteri), avvisa se è già stata segnalata da
  poco, e manda `DX <kHz> <call> <commento>` al nodo scelto. / **Spots to the cluster**:
  a "Spot a station" box from New QSO (SPOT), from the Decodium list (on-air frequency = dial
  + audio offset), from the cluster window and from the menu; it checks call, bands and
  comment (30 characters), warns when it was already spotted recently, and sends
  `DX <kHz> <call> <comment>` to the chosen node.
- **Web cluster nella barra in basso**: un globo accanto all'orologio apre
  decowebcluster.ft2.it nel browser. / **Web cluster in the bottom bar**: a globe next to the
  clock opens decowebcluster.ft2.it in the browser.
- **«Check» su ogni fonte**: il nome, la porta, la prima parola del nodo, e dove si ferma —
  con il suggerimento giusto quando a tenere la connessione è un antivirus o un firewall
  (AVG, Avast…). / **"Check" on every source**: the name, the port, the node's first word,
  and where it stops — with the right hint when an antivirus or a firewall (AVG, Avast…)
  is holding the connection.
- **Ogni spot confrontato col log**: NUOVO DXCC, nuova banda, nuovo modo, nuovo slot, già
  lavorato, entità non confermata, utente LoTW. / **Every spot compared with the log**: NEW
  DXCC, new band, new mode, new slot, worked, unconfirmed entity, LoTW user.
- **Distanza e azimut**, referenze POTA/SOTA/WWFF/IOTA lette dal commento, SNR degli
  skimmer, spot dello stesso DX raggruppati. / **Distance and bearing**, POTA/SOTA/WWFF/IOTA
  references read from the comment, skimmer SNR, spots of the same DX grouped.
- **Filtri**: banda, modo, stato, continente del DX e dello spotter, fonte, nominativi con
  jolly, testo, SNR minimo, età, solo attivazioni, solo utenti LoTW, niente skimmer, banda di
  Decodium; salvabili con un nome. / **Filters**: band, mode, status, DX and spotter
  continent, source, wildcard calls, text, minimum SNR, age, activations only, LoTW users
  only, no skimmers, Decodium's band; saveable by name.
- **Regole d'avviso** con annuncio vocale (voci di sistema, italiano o inglese, alfabeto
  fonetico), riga evidenziata nel registro e invio a Decodium. / **Alert rules** with voice
  announcements (system voices, Italian or English, phonetic alphabet), a highlighted line in
  the log and a message to Decodium.
- **Doppio clic: Decodium si sintonizza** (frequenza di chiamata, modo, DX pronto). /
  **Double-click tunes Decodium** (dial frequency, mode, DX ready).
- **Console** per i comandi al nodo e per pubblicare uno spot. / **Console** for node
  commands and for posting a spot.
- **Elenco utenti LoTW** dell'ARRL, aggiornato una volta alla settimana. / **ARRL LoTW user
  list**, refreshed once a week.

## 12. Attivazioni e contest / Activations and contests

- **Sessione POTA, SOTA, WWFF, IOTA, contest o libera**. / **POTA, SOTA, WWFF, IOTA, contest
  or free session**.
- **Campi dell'attivatore su ogni QSO**: `MY_SIG`, `MY_SIG_INFO`, `MY_POTA_REF`,
  `MY_WWFF_REF`, `MY_SOTA_REF`, `MY_IOTA`, più locatore del posto ed etichetta. / **Activator
  fields on every QSO**: same fields, plus the grid of the place and a tag.
- **Numero progressivo** inviato (`STX`) e ricevuto (`SRX`). / **Serial number** sent (`STX`)
  and received (`SRX`).
- **Duplicati dentro la sessione**, a qualunque ora. / **Duplicates inside the session**, at
  any hour.
- **Conteggi**: QSO, nominativi diversi, durata, per banda e modo, e quanto manca per
  validare (10 POTA, 4 SOTA). / **Counters**: QSOs, different calls, duration, per band and
  mode, and how many QSOs are still needed (10 POTA, 4 SOTA).
- **Export ADIF della sessione** con il nome che POTA si aspetta. / **ADIF export of the
  session** with the file name POTA expects.
- **La sessione sta nel log**: sopravvive alla chiusura del programma. / **The session lives
  in the log**: it survives a restart.

## 13. DecoLink (Decodium ⇄ DecoDXLog)

- **Il log dentro Decodium**: nominativi lavorati e confermati, anche quelli che non sono nel
  suo ADIF. / **The log inside Decodium**: worked and confirmed calls, including those not in
  its own ADIF.
- **Conferma di ogni QSO scritto**, con messaggio nella barra di stato di Decodium. /
  **Confirmation of every QSO written**, with a message in Decodium's status bar.
- **Stato dell'FT2 Award** in Decodium ("LOG FT2 x/y"). / **FT2 Award state** in Decodium
  ("LOG FT2 x/y").
- **Spot del cluster** dentro la lista e la cascata di Decodium, marcati. / **Cluster spots**
  inside Decodium's list and waterfall, marked.
- **Sintonia su uno spot** chiesta da DecoDXLog; non trasmette mai. / **Tuning on a spot**
  asked by DecoDXLog; it never transmits.
- **Solo 127.0.0.1**, JSON su TCP, protocollo in `docs/DECOLINK.md`. / **Localhost only**,
  JSON over TCP, protocol in `docs/DECOLINK.md`.
- **Monitor del traffico**: UDP, DecoLink e annunci DecoPort nei due versi, con filtri,
  pausa e dettaglio; da li' si risponde a una decodifica, si ferma il TX, si manda testo
  libero, locatore, evidenziazioni e righe DecoLink. DecoPort solo in ascolto. / **Traffic
  monitor**: UDP, DecoLink and DecoPort announcements both ways, with filters, pause and
  detail; from there you reply to a decode, halt TX, send free text, locator, highlights and
  DecoLink lines. DecoPort is listen-only.
- **Pannello Decodium**: Full Spectrum e Signal RX di Decodium in un pannello compatto della
  lavagna (si sposta, si ridimensiona, si attacca ai bordi, si stacca), con le righe colorate
  secondo il log: nuovo DXCC, banda, nominativo. / **Decodium panel**: Decodium's Full Spectrum
  and Signal RX in a compact board panel (move, resize, magnetic edges, detach), rows colored
  by what they are worth to the log: new DXCC, band, call.
- **Finestre vere di Decodium**: Full Spectrum e Signal RX (o qualunque finestra di
  Decodium) mostrate vive, ritagliate, in pannelli della lavagna; i clic arrivano a Decodium se
  si vuole. Solo Windows, la finestra non deve essere ridotta a icona. / **Real Decodium
  windows**: Full Spectrum and Signal RX (or any Decodium window) shown live, cropped, in board
  panels; clicks reach Decodium if you want. Windows only, the window must not be minimized.

## 14. Copie di sicurezza / Backup

- **Copia notturna** con `VACUUM INTO`, anche mentre DecoDXLog scrive. / **Nightly backup**
  with `VACUUM INTO`, even while DecoDXLog is logging.
- **Copie a rotazione**, cartella e ora a scelta, e copia a richiesta. / **Rotating copies**,
  folder and time of your choice, and a backup on demand.
- **Se il PC era spento**, la copia si fa appena DecoDXLog è aperto. / **If the PC was off**,
  the copy is made as soon as DecoDXLog opens.

## 15. Credenziali / Credentials

- **Portachiavi di sistema** (Gestione credenziali di Windows, Portachiavi di macOS, Secret
  Service su Linux) per QRZ, QRZ Logbook, LoTW, Club Log, eQSL, HamQTH, HamAlert, Cloud. /
  **System keystore** for the same services.
- **Nel file delle impostazioni solo il nome utente**; senza portachiavi non si salva niente
  in chiaro. / **Only the user name in the settings file**; with no keystore nothing is
  stored in the clear.
- **Gli errori di rete non mostrano mai l'URL con la password**. / **Network errors never
  show the URL with the password**.

## 16. Aspetto e lingua / Look and language

- **Tre temi** (Ocean Blue, Stellar Light, Darkcodium), variante d'accento, densità, colori
  personalizzati: gli stessi di Decodium. / **Three themes** (Ocean Blue, Stellar Light,
  Darkcodium), accent variant, density, custom colours: the same as Decodium.
- **Interfaccia in italiano o in inglese**, o come il sistema. / **Interface in Italian or
  English**, or like the system.
- **Pannelli ridimensionabili** ovunque: ogni divisorio si trascina, e le misure restano.
  / **Resizable panels** everywhere: every splitter drags, and the sizes stay.
- **Ogni finestra e' una finestra del sistema**: diplomi, impostazioni, scheda del QSO,
  nuovo QSO, profili, attivazione e Cabrillo si spostano su qualsiasi schermo, si
  ingrandiscono, e si riaprono dove erano. / **Every window is a real window**: awards,
  setup, QSO card, new QSO, profiles, activation and Cabrillo move to any screen, maximize,
  and reopen where they were.
- **Ogni pannello si stacca** in una finestra sua (posizione e misura ricordate) e **si
  chiude**, dalla testata o dal menu **Pannelli** nella barra in alto, che dice per ognuno
  se e' agganciato, in finestra o chiuso e rimette la disposizione di partenza. / **Every
  panel detaches** into its own window (position and size remembered) and **closes**, from
  its header or from the **Panels** menu in the top bar, which says for each one whether it
  is docked, in a window or closed, and restores the default layout.
- **Contenuti scorrevoli**: statistiche, diplomi, invio QSL, scheda nominativo, nuovo QSO e
  propagazione scorrono invece di tagliare quello che non ci sta. / **Scrollable contents**:
  statistics, awards, QSL upload, callsign card, new QSO and propagation scroll instead of
  cutting off what doesn't fit.
- **Icona propria** in tutte le misure, con i colori del tema. / **Its own icon** in every
  size, in the theme colours.

## 17. Mappa e statistiche / Map and statistics

- **Mappa** con le coste del mondo (Natural Earth, pubblico dominio, dentro l'eseguibile),
  senza scaricare niente. / **Map** with the world coastlines (Natural Earth, public domain,
  inside the executable), nothing downloaded.
- **Linea grigia** calcolata dalla posizione del Sole, aggiornata da sola. / **Grey line**
  computed from the Sun's position, refreshed by itself.
- **Locatori lavorati**, **spot del cluster** colorati per stato, la stazione e il **cerchio
  massimo** verso il nominativo scelto; livelli accendibili e spegnibili. / **Worked grids**,
  **cluster spots** coloured by status, your station and the **great circle** to the selected
  call; layers you can switch on and off.
- **Statistiche** in una finestra propria: totali, QSO per anno, per mese, per ora UTC e per
  banda, modi e continenti, e la **mappa di calore banda per ora**, che dice quando una banda
  e' aperta. Filtri per modo e per anno. / **Statistics** in their own window: totals, QSOs
  per year, month, UTC hour and band, modes and continents, and the **band-by-hour heat
  map**, which shows when a band is open. Filters by mode and by year.
- **Registro attività**: tutto quello che il programma fa, con i colori della gravità. /
  **Activity log**: everything the program does, coloured by severity.

## 18. Strumenti e confezione / Tools and packaging

- **`decodxlog_udpsend`**: finge di essere Decodium e manda un QSO di prova. / **`decodxlog_udpsend`**:
  pretends to be Decodium and sends a test QSO.
- **`decodxlog_clusterprobe`**: prova una fonte di spot da riga di comando. /
  **`decodxlog_clusterprobe`**: tries a spot source from the command line.
- **Opzioni**: `--db`, `--port`, `--import`, `--theme`, `--show`, `--grab`, `--spots`. /
  **Options**: the same.
- **`scripts/deploy.sh`**: cartella autonoma e `DecoDXLog-<versione>-win64.zip`. /
  **`scripts/deploy.sh`**: a stand-alone folder and `DecoDXLog-<version>-win64.zip`.
- **CI su GitHub** (Windows MSYS2 e Linux) con l'artefatto pronto. / **GitHub CI** (Windows
  MSYS2 and Linux) with the artifact ready.
- **53 gruppi di test** automatici nel programma e **131 prove** del servizio Cloud
  (`server/tests`). / **53 automated test suites** in the program and **131 checks** for the
  Cloud service (`server/tests`).

## Non ancora / Not yet

- Le etichette QSL in PDF e i file dei contest si fanno dal programma: sul Cloud si vede
  la coda, non si stampa. / QSL labels and contest files are made in the program: the Cloud
  shows the queue, it does not print.
