# Storia di DecoDXLog

Le date sono quelle del lavoro, non di una pubblicazione: DecoDXLog cresce mentre lo si usa
in stazione.

## 1.17.09 — 6 ottobre 2026

**Allineata alla 1.17.08 di elisir80.** Il CW in ricezione che ascolta il formato vero della scheda
audio (con la scelta di tono e velocita' del corrispondente), le callback dei pannelli QML che non
restano piu' in coda alla chiusura, le macro F1–F12 con fn/Globe e Ctrl su macOS: tutto nella sezione
1.17.08 qui sotto. Il numero 1.17.08 e' della loro release, e la nostra e' la 1.17.09, piu' alta.

**Il web cluster nella barra in basso.** Accanto all'orologio mondiale c'e' un globo disegnato
(non un'emoji, come le altre icone del programma): un clic apre nel browser
decowebcluster.ft2.it, e il suggerimento al passaggio del mouse dice cos'e'. Si raggiunge anche con
Tab e si attiva con Invio o Spazio. Traduzioni nelle 14 lingue.

## 1.17.08 — 6 ottobre 2026

**CW in ricezione: ascolta il formato vero della scheda.** Alcune interfacce USB Audio, soprattutto
su macOS/CoreAudio, dichiaravano disponibile un formato a 8 kHz ma consegnavano al decoder campioni
con un ritmo o una disposizione diversa: il segnale era visibile ma il testo usciva come rumore. Ora
DecoDXLog apre il formato nativo effettivamente negoziato, lo converte esplicitamente in mono signed
16-bit e fa il downmix corretto quando la sorgente e' stereo. Il pannello mostra formato, frequenza di
campionamento e canali realmente aperti; se un formato non e' convertibile, non finge di ascoltare e
spiega il motivo.

Il tono puo' restare in **Auto**, oppure essere fissato fra 300 e 1500 Hz; si puo' fissare anche la
velocita' del corrispondente fra 5 e 59 WPM senza toccare la velocita' del proprio keyer. Cambiando
tono o velocita' la finestra di analisi si azzera, cosi' non mescola punti e linee raccolti con due
parametri diversi. In Auto il pannello non mostra piu' come dato buono un vecchio aggancio mentre sta
ascoltando solo rumore. Le conversioni e le scelte vengono provate separatamente, comprese le sorgenti
mono, stereo, Int16, Int32, float e non supportate.

**Pannelli QML.** Band map, mappa, chat e rete contest non lasciano piu' callback `Qt.callLater` in
coda quando il pannello viene chiuso o scaricato: usano timer che appartengono alla vista. Questo evita
il messaggio `QQmlVMEMetaObject: Internal error - attempted to evaluate a function in an invalid context`.
Su macOS le macro CW F1–F12 si possono attivare con fn/Globe+F1–F12; sulle tastiere senza fn funziona
anche Ctrl+F1–F12.

## 1.17.07 — 5 ottobre 2026

**Il nominativo sbagliato si corregge.** Nella scheda del QSO il nominativo era solo letto, in
grande, e non c'era modo di cambiarlo: un errore di battitura restava nel log. Ora il campo
**Call** e' il primo della scheda (Generale), e lo stesso si fa sulla riga del log senza aprire
niente: **F2** (o «Correggi il nominativo qui» nel menu della riga) trasforma la cella in un campo;
Invio salva, Esc lascia com'era, un clic altrove non salva niente per sbaglio. Dal menu della riga si
puo' correggere sul posto anche un'altra cella che si scrive a mano — rapporti, locatore, nome, QTH,
commento —; data, banda, modo e frequenza restano alla scheda.

Cambiare il nominativo non e' come cambiare un commento, e il programma lo sa: nazione, DXCC, zone
CQ e ITU e continente venivano dal vecchio nominativo e si rifanno dal nuovo (a meno che li si tocchi
nello stesso salvataggio); i servizi — LoTW, QRZ, Club Log, HRDLog, eQSL — che avevano gia' ricevuto il
QSO senza conferma hanno in mano quello sbagliato, e il giusto non e' mai partito: tornano «da
mandare», e il registro lo scrive («… era gia' andato a LoTW, QRZ con il nominativo sbagliato: verra'
mandato di nuovo»). I confermati restano com'erano, perche' una conferma dice che dall'altra parte il
nominativo era giusto. Il QSO vecchio resta su quei servizi — LoTW e QRZ non cancellano da qui —:
se serve, va tolto li'. Ogni correzione e' una revisione nuova con la vecchia nello storico. Prova
sul database dei servizi da rimandare (anche dietro la data limite, e con le cartoline e i
confermati fuori); il resto provato a mano su una copia del log di prova (nominativo che cambia
nazione, vuoto, con spazi, uguale, colonna non ammessa, QSO che non c'e'). Traduzioni nelle 14 lingue.

## 1.17.06 — 5 ottobre 2026

**Mandare gli spot, non solo riceverli.** Segnalare al cluster una stazione c'era gia', ma in fondo
alla console, tre campi e un pulsante. Ora c'e' un riquadro **Segnala una stazione**, e si apre da
dove serve: il pulsante **SPOT** del QSO nuovo (con nominativo, frequenza in kHz, modo e rapporto del
modulo), il menu della riga dell'elenco di Decodium («Segnala questa stazione al cluster…», con la
frequenza in aria — la dial piu' lo scarto audio — e un commento «FT8 -5 dB JN75»), il pulsante
«Invia uno spot…» della finestra del cluster e il menu in alto, che parte dalla stazione di adesso.
Prima di mandare si controlla: il nominativo deve somigliare a un nominativo, la frequenza (in MHz
se scritta come 14.074) deve stare dentro le bande amatoriali, il commento e' al massimo di 30
caratteri come vogliono i nodi; se qualcuno ha gia' segnalato la stessa stazione negli ultimi
quindici minuti, sulla stessa frequenza, il riquadro lo dice (chi, quanto fa, dove) e il pulsante
diventa «Segnala lo stesso». Si sceglie a quale nodo mandare, e uno scelto per nome che non c'e' piu'
non fa finire lo spot su un altro nodo per caso. Resta scritto nel registro attivita' cosa e'
partito. Prove: i controlli, il comando che arriva al nodo (`DX 14074.0 K1ABC FT8 -10 dB`), il
nodo che non c'e', l'avviso di doppione, e la riga dell'elenco con la frequenza in aria; piu' la
prova che legge i QML e pretende il fuoco in ogni popup con un campo di testo, che copre anche questo.
Traduzioni nelle 14 lingue.

## 1.17.05 — 5 ottobre 2026

**Allineata alla 1.17.04 di elisir80.** «Ferma» del CW che annulla anche il CW in coda, rilascia il
keyer locale e manda il PTT a zero alla radio che ha ricevuto la macro, con rigctld, TCI e flrig che
non possono piu' riavviare una trasmissione con una risposta in ritardo; e, con Hamlib diretto sulla
seriale, il CW spento sulle Yaesu per cui Hamlib sovrascriverebbe la memoria 1 del keyer (frequenza e
modo restano; per il CW serve un keyer seriale a parte o un WinKeyer): tutto nella sezione 1.17.04 qui
sotto. Hanno anche sistemato due cose nostre: gli avvisi di Qt 6.9 sulle conversioni UTC nell'elenco
delle decodifiche di Decodium, e l'export che ora dice se il file non si apre o non si scrive tutto.
Il numero 1.17.04 e' della loro release, e la nostra e' la 1.17.05, piu' alta. Le tre stringhe nuove
sono tradotte nelle 14 lingue.

## 1.17.04 — 4 ottobre 2026

**Rilascio integrato dalla 1.17.02.** Questa versione unisce la base 1.17.03 con tutte le
correzioni locali CW/CAT. Il pulsante **Ferma** annulla anche il CW gia' in coda, rilascia il
keyer locale e forza il PTT OFF sulla radio che ha ricevuto la macro; risposte tardive di rigctld,
TCI e flrig non possono riavviare una trasmissione. Per proteggere le memorie personali, il CW
diretto e' disabilitato sulle Yaesu per cui Hamlib sovrascrive la memoria 1 del keyer: CAT per
frequenza e modo rimane disponibile, mentre per il CW si usa un keyer seriale separato o WinKeyer.

## 1.17.03 — 4 ottobre 2026

**Allineata alla 1.17.02 di elisir80.** I tasti F1–F12 del CW che mostrano e mandano la macro
giusta (e «Ferma» che svuota anche la seriale del WinKeyer e toglie il PTT con CAT/Hamlib), e i
controlli delle finestre uguali dappertutto — i tre cerchi a sinistra anche su Windows e Linux —:
tutto nella sezione 1.17.02 qui sotto. Il numero 1.17.02 e' della loro release, e la nostra e'
la 1.17.03, piu' alta, cosi' l'aggiornamento non le confonde.

**Cluster: «Check» su ogni fonte, e messaggi che dicono dove cercare.** A qualcuno il cluster non si
collega, e la pagina del nodo (iq8do.aricaserta.it) nel browser dice ERR_CONNECTION_CLOSED: il nodo
dal PC di chi lo ha scritto risponde con il suo «login:», e il collegamento viene tenuto o chiuso sul
PC di chi non lo vede — l'antivirus (AVG, Avast…) con lo scudo web o la scansione HTTPS, un firewall.
Da fuori non si distingue, e finora il programma diceva solo «connection closed». Ora nella scheda
Fonti ogni nodo ha un pulsante **Check** che prova un passo alla volta — il nome diventa un indirizzo?
la porta risponde? il nodo dice qualcosa? — e scrive dove si ferma, con il consiglio per quel caso
(consenti DecoDXLog nell'antivirus e nel firewall, spegni la scansione di quel collegamento, prova un
altro nodo, controlla il DNS). Anche i messaggi di stato dicono di piu': «collegato ma muto» e
«chiuso prima che il nodo dicesse qualcosa» parlano di antivirus e di firewall e rimandano a Check.
Prove: un nodo che saluta (con la negoziazione telnet davanti), uno che accetta e chiude, uno muto,
una porta chiusa, un nome che non esiste, e il controllo da una fonte con la fonte tolta a meta'.
Traduzioni nelle 14 lingue.

**La data dello scarico per periodo non si poteva scrivere.** In «Invia QSL» il pulsante «Periodo
dal… al…» apre un riquadro in una finestra sua, e li' il campo prendeva il cursore ma i tasti non
arrivavano: il popup non dichiarava il fuoco. Ora lo dichiara e il cursore parte gia' nella prima
data. Per non ricascarci c'e' una prova che legge i QML e pretende il fuoco in ogni popup di
questo tipo che contiene un campo di testo (tolto il fuoco al riquadro di LoTW, fallisce).

**Esportare i QSO di un periodo.** Nel log, dalle azioni (e dal menu File), «Esporta un periodo
(dal… al…)»: lo stesso riquadro con le due date dello scarico da LoTW — in comune ora c'e' un
solo pezzo, `DatePeriodFields` —, estremi compresi (il giorno di «al» intero, anche le 23:59),
un estremo si puo' lasciare vuoto, e il riquadro dice quanti QSO sono prima di chiedere dove
salvare. L'ADIF e' lo stesso di sempre, scritto da un filo a parte come gli altri export, e i
QSO cancellati restano fuori. Prova sul database con QSO a cavallo dei confini, fuori ordine e
uno cancellato.

## 1.17.02 — 4 ottobre 2026

**CW: ogni memoria e il suo tasto.** I tasti F1–F12 ora mostrano la macro
selezionata davvero, invece di tenere F1 evidenziato per impostazione grafica.
Il percorso conserva l'indice della macro: F2 manda F2, F3 manda F3, e il
pulsante **Ferma** azzera la selezione. Con WinKeyer lo svuotamento del buffer
viene scritto subito sulla seriale; con CAT/Hamlib, dopo `stop_morse`, viene
anche tolto il PTT come arresto di sicurezza per i backend che non svuotano il
buffer del manipolatore della radio.

**Controlli finestra uguali dappertutto.** Le finestre staccate su Windows e
Linux usano ora, come macOS, i tre controlli circolari a sinistra (chiudi,
riduci, ingrandisci/ripristina). Anche nei pannelli il controllo rosso chiude e
quello verde o giallo stacca o riaggancia. I vecchi comandi testuali a destra
non restano duplicati.

## 1.17.01 — 4 ottobre 2026

**Allineata alla 1.17.00 di elisir80.** Cornice delle finestre arrotondata e condivisa, menu Pannelli
accanto al pulsante, Voice keyer dalla lista Pannelli anche fuori dal Contest Mode, «Ferma» del CW che
interrompe la radio giusta in SO2R, Band map senza il modello Qt negativo, pacchetti firmati per
piattaforma: tutto nella sezione 1.17.00 qui sotto. Il numero 1.17.00 e' della loro release, e la
nostra e' la 1.17.01, piu' alta, cosi' l'aggiornamento non le confonde.

**Rotore: i comandi restano nell'ordine in cui arrivano.** Il gateway del rotore mette goto e stop
davanti alle domande di posizione, ma con la linea occupata (una domanda in corso, un PC sotto
carico) li metteva anche davanti ai comandi arrivati prima: «vai a 60, fermati, vai a 120» diventava
«vai a 120, fermati, vai a 60» e il rotore si fermava dove non gli si era detto, finendo poi in un
«nessun movimento» di sicurezza. Lo ha scovato la prova del rotctld nella CI di Windows, che ogni
tanto falliva. Ora fra loro i comandi sono in fila come in DecoRotor, e ce n'e' una prova che con
l'ordine vecchio falliva ogni volta.

## 1.17.00 — 4 ottobre 2026

**Finestre e pannelli piu' affidabili.** Le finestre staccate e le finestre
interne condividono ora una cornice arrotondata, con ombra e contenuto ritagliato
quando il sistema lo supporta; su Linux il bordo resta volutamente opaco e non
dipende dal compositor. Su macOS i comandi delle finestre usano i controlli a
semaforo. Il menu **Pannelli** si apre accanto al pulsante che lo ha chiamato,
restando nello schermo anche su finestre strette.

**CW e Voice keyer.** Il Voice keyer e' disponibile dalla lista Pannelli anche
fuori dal Contest Mode. Il pulsante **Ferma** del CW interrompe la radio esatta
a cui e' stata inviata la macro, anche quando in SO2R era selezionata la radio 2.

**Mappe e QSL.** La Band map non puo' piu' generare un modello Qt negativo
durante un aggiornamento momentaneo dell'intervallo di frequenze. Arrivano inoltre
gli scarichi QSL per intervallo di date per LoTW, eQSL e QRZ, senza alterare la
posizione dello scarico incrementale normale.

**Distribuzione.** I workflow manuali predefiniscono ora v1.17.00 e producono
pacchetti firmati selezionabili dall'autoaggiornamento per Windows x64, macOS
Apple Silicon/Intel e Linux x86_64/aarch64.

## 1.16.55 — 4 ottobre 2026

**Allineata alla 1.16.54 di elisir80.**
- **Chiusura affidabile**: chiudendo la finestra principale DecoDXLog ferma in modo esplicito audio,
  CAT, CAT condivisa, scansioni radio e i `rigctld` che ha avviato lui, con un limite preciso: un
  driver seriale bloccato non lascia piu' il programma vivo senza finestre (visto su macOS).
- Il resto di quella 1.16.54 (note di rilascio, firma ad-hoc dei pacchetti macOS) e' quello che
  gia' c'era nelle nostre 1.16.51–1.16.53. Il numero 1.16.54 e' della loro release: la nostra
  prossima e' la 1.16.55, cosi' le due non si confondono nell'aggiornamento.

**Le conferme di un periodo, per LoTW, eQSL e QRZ.** Nella scheda QSL il pulsante «LoTW dal… al…»
diventa «Periodo dal… al…», con la scelta del servizio (LoTW, eQSL, QRZ) e le stesse due date di
LoTW — i QSO fatti dal … al …, estremi compresi, una delle due si puo' lasciare vuota. Prima eQSL e
QRZ sapevano solo scaricare «le nuove dall'ultima volta» o tutto.
- **eQSL** filtra per data del QSO (`LimitDateLo`/`LimitDateHi`), QRZ si chiede per intero e il
  periodo si tiene qui; per tutti e due il filtro si rifa' anche in locale, senza fidarsi.
- Uno scarico per un periodo **non sposta** «da quando» dello scarico di sempre: scaricare settembre
  2024 non vuol dire essere allineati a oggi. Il periodo vale per quello scarico soltanto, anche se
  l'account e' piu' d'uno (i profili della stazione), e il riepilogo dell'importazione lo mostra come gli altri.
- Un periodo che comincia dopo la fine non parte: lo dice.

## 1.16.53 — 3 ottobre 2026

**Il rotore Yaesu G-450 (e i GS-232) dentro il gateway.** Arriva da DecoRotor, che ha imparato il
dialetto seriale Yaesu GS-232 accanto al PRO.SIS.TEL: nuovo modello «Yaesu G-450 / GS-232 - solo
azimut» fra i control box (Impostazioni → Rotore, o `yaesu_gs232` nel config.json di DecoRotor,
che «Prendile da DecoRotor» legge). Comandi `C` (dov'e'), `M290` (vai a 290°), `S` (stop), risposte
`+0290`, `+0290+0045` o `AZ=290` chiuse da CR, a 9600 baud. Non si rileva da solo (il PRO.SIS.TEL
resta l'unico che il rilevamento automatico prova): si sceglie a mano, e la nota sotto la scelta
lo ricorda. Il G-450 gira fino a 450°: per usare tutto l'overlap si mette il massimo dell'azimut a 450
nelle impostazioni della finestra del rotore. Tutto il resto del gateway — l'app, rotctld, la pagina
web, i finecorsa, lo stop di sicurezza, il rotore simulato — funziona uguale. Un goto oltre 450 si
ferma a 450 (DecoRotor lo rifiutava).

**La scheda audio si sceglie, e vale quella.** Il decoder CW (ingresso) e il DVK (uscita verso la
radio e microfono) potevano ascoltare o parlare su una scheda diversa da quella che l'elenco
mostrava. Le cause: se il nome salvato non si ritrovava — Windows numera le schede uguali, «2- USB
Audio CODEC», e il numero cambia con le porte USB — si usava in silenzio il **predefinito di
sistema**, mentre la tendina continuava a mostrare la prima riga; e l'elenco del decoder si
rileggeva, e la tendina si rifaceva, a ogni pezzetto di audio. Ora:
- la scelta si salva con l'**identificativo** della scheda (quello stabile di Windows) e il suo nome;
  si ritrova per identificativo, poi per nome se e' uno solo (le impostazioni vecchie, col solo
  nome, si aggiornano da sole la prima volta);
- «Predefinito di sistema» e' una voce scritta, e solo quella usa il predefinito;
- se la scheda scelta **non c'e'**, la tendina dice «Non disponibile: <nome>», il decoder non parte
  e il DVK non manda niente (e non alza il PTT): niente ripiego su un'altra scheda, che per un
  messaggio vocale vuol dire gli altoparlanti con la radio in trasmissione;
- due schede con lo stesso nome si distinguono nell'elenco («nome», «nome (2)») e, scelte, per
  identificativo; nel Registro attivita' si legge su quale sta ascoltando il decoder;
- l'elenco si aggiorna solo quando cambiano le schede (collegando o togliendo una USB), non a ogni
  audio.

**Le finestre vere di Decodium dentro il log.** Due pannelli nuovi della lavagna, «Full Spectrum»
e «Signal RX» (da Pannelli, in alto), che mostrano le finestre di Decodium come le disegna lui,
con i suoi colori, le sue colonne e le sue righe — non una lista ricostruita:
- di partenza ritagliano la zona di Full Spectrum e di Signal RX della finestra principale di
  Decodium; con ▾ si sceglie un'altra finestra (anche una staccata col tasto «Pop», con la sua
  misura giusta) o, con ▭, si traccia col mouse la zona da vedere;
- come gli altri pannelli: si spostano, si ridimensionano, si attaccano ai bordi, si staccano,
  anche in Contest Mode;
- «⇄» manda i tuoi clic a Decodium (un doppio clic su una riga le risponde): spento di serie;
- «⤡» (per le finestre staccate) porta la finestra di Decodium alla misura del pannello, cosi'
  il testo resta nitido.
Come funziona: Windows tiene una copia di ogni finestra (quella delle anteprime della barra) e
ogni mezzo secondo se ne prende l'immagine, una sola per finestra anche se la guardano due
pannelli, su un altro filo. La finestra di Decodium non si tocca: non si incorpora, non si
cambia, e se DecoDXLog si blocca Decodium non se ne accorge. La finestra deve esserci e **non
ridotta a icona** (coperta da altre finestre o su un altro schermo va bene; fuori da tutti gli
schermi no: Windows non la disegna). Se e' ridotta a icona, il pannello lo dice e offre
«Riporta Decodium alla misura normale, dietro le altre finestre».

**Il pannello Decodium riparte dalla storia.** Prima si riempiva solo con le decodifiche arrivate
dopo l'apertura di DecoDXLog: a banda calma restava vuoto, mentre il Full Spectrum di Decodium
aveva gia' le sue righe. Ora, al primo stato ricevuto, si rileggono dal registro di Decodium
(`db.sqlite`, aperto in sola lettura) le decodifiche dell'ultima ora sulla stessa banda e
modo, senza doppioni; «⟲» le rilegge quando vuoi.

**Una cosa da sapere sui comandi a Decodium.** Rispondere a una riga, Ferma TX, Auto TX spento,
Ripeti, testo libero, locatore ed evidenziazioni (monitor e pannello) li esegue Decodium solo se
vengono dalla sua destinazione UDP **principale** (Settings → Reporting → UDP Server) e con
«Accept UDP requests» acceso: alla seconda e alla terza destinazione le richieste le ignora, e
se DecoDXLog e' la seconda o la terza non succede niente. Con DecoDXLog come destinazione
principale basta, che gia' inoltra a JTAlert, GridTracker e simili («Inoltra a…»). Il monitor
lo dice, e il «⟲» ricarica la storia anche quando Decodium non risponde.

## 1.16.52 — 3 ottobre 2026

**Il pannello Decodium: Full Spectrum e Signal RX nel log.** Un pannello compatto sulla lavagna,
con le due liste di decodifica di Decodium 4, ricostruite dal protocollo UDP (ogni decodifica e
lo stato: corrispondente, frequenze di ascolto e trasmissione, trasmissione in corso):
- **Full Spectrum**: tutto quello che Decodium sente in banda, con ora, dB, DT, frequenza e
  messaggio;
- **Signal RX**: il QSO in corso, con la stessa regola di Decodium — le trasmissioni proprie, i
  messaggi che nominano il tuo nominativo e quelli del corrispondente; se il corrispondente
  cambia (doppio clic su un CQ vecchio in Decodium), entrano anche le sue righe di prima.
- **I colori sono quelli del log**: una barretta e il colore del testo dicono se la stazione
  e' un nuovo DXCC, una banda nuova, un nuovo nominativo, gia' lavorata… con gli stessi colori
  e la stessa regola degli spot del cluster; larghi, si leggono anche l'etichetta e il paese.
  Chi ti chiama ha il fondo rosso, il corrispondente il fondo dorato, le tue trasmissioni ambra.
- **Nella lavagna come gli altri pannelli**: si sposta per la testata, si ridimensiona dai bordi
  e dagli angoli, si attacca ai bordi vicini (calamita), si stacca in una finestra e si chiude;
  anche in Contest Mode. Largo, i due elenchi stanno uno accanto all'altro, stretto uno sopra
  l'altro; la maniglia in mezzo si tira e la proporzione segue il pannello. Le colonne che non
  ci stanno spariscono. In testata: le piu' recenti in alto o in basso come in Decodium, e «⟲»
  per chiedere a Decodium di rimandare le decodifiche che ha a schermo (utile se il pannello
  si apre dopo).
- **Clic**: un clic sceglie la riga; il doppio clic porta il nominativo nella scheda e nel
  riquadro del QSO (non fa partire niente in Decodium); col tasto destro «Rispondi in
  Decodium» (come il doppio clic in Decodium: puo' far partire la chiamata), «Mostra nella
  scheda e prepara il QSO» e «Copia la riga».
- Di serie e' chiuso: si apre da Pannelli, in alto.

Sotto il cofano: lo stato UDP porta ora anche Rx DF, Tx DF, periodo T/R e l'ultimo messaggio
trasmesso, e le decodifiche il flag «poco affidabile» (i client piu' vecchi, che si fermano
prima, si leggono lo stesso).

## 1.16.51 — 3 ottobre 2026

**Il monitor del traffico con Decodium.** Dal menu del marchio, «Monitor del traffico
Decodium…»: una finestra con tutto quello che passa fra DecoDXLog e Decodium 4, nei due versi,
riga per riga con ora (al millesimo), canale, verso, tipo, programma e contenuto.
- **UDP** (il protocollo di WSJT-X): battiti, stato, decodifiche, QSO, ADIF, e anche quello che
  DecoDXLog inoltra ai programmi accanto (GridTracker, JTAlert…) con le loro risposte rimandate
  a Decodium. Si leggono anche i tipi che Decodium ha in piu' (AnnotationInfo, SetupTx,
  EnqueueDecode).
- **DecoLink**: ogni riga JSON che entra ed esce (saluto, elenco del log a blocchi, domande e
  risposte sui nominativi, award, ping).
- **DecoPort**: gli annunci della radio in rete (frequenza, modo, PTT, nome della radio, stato,
  strumenti: tensione, corrente, potenza…), ascoltati sulla 5560 solo mentre la finestra e'
  aperta, insieme agli altri programmi che li ascoltano.
- Filtri per canale e per testo, «nascondi i battiti», pausa (la vista si ferma, le righe si
  raccolgono lo stesso), copia come testo; clic su una riga per vederla intera (i byte, o il
  JSON in chiaro). Tiene le ultime 5000 righe.

**E si parla a Decodium da li'.** Dallo stesso socket UDP su cui Decodium scrive, col suo nome di
programma (e' quello di cui Decodium si fida): rispondere a una riga decodificata (come il
doppio clic in Decodium, anche col doppio clic nel monitor), Ferma TX, Auto TX spento, ripeti
le decodifiche, pulisci le finestre, testo libero (impostato o anche trasmesso), locatore,
evidenziare o togliere l'evidenziazione a un nominativo coi suoi colori. Su DecoLink: una riga
JSON a mano, rimandare l'elenco del log o lo stato degli award. DecoPort resta in solo ascolto:
la sessione e' firmata con la chiave di Decodium e un suo comando mette la radio in trasmissione.

## 1.16.50 — 3 ottobre 2026

**Il rapporto giusto per il modo.** Nel Nuovo QSO (il pannello e la finestra) i rapporti
partivano sempre da 59, anche in CW e in FT8. Adesso seguono il modo: 59 in fonia, 599 in CW,
RTTY e PSK, -10 (dB) nei modi di WSJT (FT8, FT4, FT2, JT65, Q65…). Cambiando modo cambiano
da soli, ma solo finche' nel campo c'e' ancora il valore di partenza: un rapporto scritto a
mano non si tocca.

**Il riepilogo dell'importazione QSL si apre sempre.** Il pulsante della scheda QSL restava
spento finche' non c'era uno scarico riuscito, e cliccandolo non succedeva niente. Adesso si
apre sempre, e anche uno scarico non riuscito (password sbagliata, servizio che non risponde)
ci finisce in rosso con il motivo; se lo si era chiesto a mano, la finestra si apre da sola.

## 1.16.48 — 3 ottobre 2026

**Il riepilogo delle conferme scaricate.** Dopo uno scarico da LoTW, eQSL o QRZ chiesto a mano
si apre «Riepilogo importazione QSL» (e dalla scheda QSL, «Riepilogo importazione»):
- per ogni servizio (e per ogni account di profilo) l'ultimo scarico: nuove, gia' segnate,
  senza corrispondenza, errori; un clic sul riquadro filtra quel servizio;
- l'elenco con data, nominativo, banda, modo, il proprio nominativo, il servizio, l'esito e, per
  le nuove, il paese con «★ nuovo DXCC» quando e' la prima conferma di quell'entita' (LoTW o
  cartolina); per quelle senza corrispondenza il motivo («il nominativo non e' nel log», o
  «nessun QSO entro 30 minuti su questa banda e modo»);
- i filtri per servizio, per esito (nuove, senza corrispondenza, errori, tutto), «solo nuovi
  DXCC» e nominativo o paese; doppio clic apre il QSO; «Copia come testo» per incollarlo;
- **la cartolina eQSL** della riga scelta: chiesta a eQSL (GeteQSL.cfm) solo quando la si
  guarda, una ogni dieci secondi come chiede eQSL, e tenuta nella cartella dei dati, cosi' la
  seconda volta arriva subito. LoTW e QRZ non hanno l'immagine della cartolina.
Il riepilogo resta nel log e si riapre anche dopo il riavvio.

**Allineata alla 1.16.48 di elisir80.**
- **CAT avanzato**: per i modelli Hamlib che li dichiarano, Impostazioni → Radio offre bit di
  dati, bit di stop, parita', handshake, stato fisso delle linee DTR/RTS e indirizzo CI-V; i
  valori si conservano e passano a `rigctld`, e quello che la radio non supporta non si vede.
- **Aggiornamenti per piattaforma**: ogni pacchetto (setup Windows, DMG macOS, AppImage Linux)
  ha il suo elenco firmato; le release vecchie con l'elenco unico si leggono ancora.
- **La chiave del fork**: le release di elisir80/DecodxLog firmate con la chiave dedicata del
  fork si installano da sole. Ogni chiave vale solo per il suo repository.
- **Le due sorgenti restano alla pari**: vince la versione piu' nuova che risulta firmata, a
  pari versione il fork. La 1.16.48 di elisir80 metteva il fork sempre per primo e iu8lmc come
  riserva; qui no, per scelta di IU8LMC.
- Tradotte le voci nuove dei parametri seriali.

## 1.16.47 — 2 ottobre 2026

**Rotore: un'antenna per banda, con il suo scostamento.** Sullo stesso palo non tutte le
antenne guardano dove guarda il rotore: un dipolo rotativo dei 40 montato a croce sulla
direttiva dei 20, una direttiva dei 2 metri girata. In Impostazioni → Rotore → «Antenne per
banda» si dice, banda per banda, quale antenna si usa e di quanti gradi guarda di lato (da
-180 a +180). Quando la radio e' su quella banda:
- il quadrante, i gradi e la mappa mostrano dove guarda quell'antenna, e sotto i gradi si
  legge l'antenna in uso e dove sta davvero il rotore («40m · Dipolo rotativo (+90°) · rotore
  10°»);
- puntare a una rotta (a mano, il DX dello spot, il locatore) gira il rotore in modo che sia
  quell'antenna a guardare li';
- al cambio di banda il registro attivita' dice quale antenna e' in uso.
Le bande non elencate restano come prima: l'antenna guarda col rotore. Le memorie, il parcheggio
e i finecorsa restano posizioni del rotore.

## 1.16.46 — 2 ottobre 2026

**I colori delle conferme si vedono.** In Impostazioni → «Colori delle righe del log»,
«Confermato su LoTW» acceso non colorava quasi niente: ogni riga prendeva la prima categoria
che le spettava anche se il suo colore era spento, e quasi ogni QSO e' un «nuovo nominativo
sulla banda». Adesso le categorie spente si saltano e la riga prende la prima accesa.

Ci sono due colori nuovi per le conferme, con la precedenza a LoTW:
- **Confermato su LoTW** — con o senza cartolina;
- **Confermato da cartolina (senza LoTW)**;
- **Confermato solo su eQSL / QRZ** — che per il DXCC non valgono.

Lo stesso nelle righe colorate di «Azioni → Colora le righe secondo la conferma QSL»: verde
LoTW, azzurro solo cartolina, giallo solo eQSL/QRZ, arancio cartolina partita e non tornata.

**CW via CAT con le Yaesu (FTDX10, FTDX101, FT-991, FT-891…).** La frequenza si leggeva, il CW
non partiva. Hamlib, per le Yaesu, scrive il testo nella memoria del manipolatore della radio
(comando KM) e lo fa suonare (KY): la radio rifiuta il messaggio intero per una sola lettera
minuscola, e trasmette solo in CW. E DecoDXLog, al primo rifiuto, spegneva i tasti delle macro
fino al riavvio. Adesso:
- il testo parte sempre in maiuscolo e con i soli segni del CW (lettere, cifre, / ? . , = + -),
  con qualunque manipolatore;
- un messaggio rifiutato dice perche' (la radio in CW? il break-in acceso? la porta giusta?) e i
  tasti restano accesi; si spengono solo se la radio dice proprio che non manipola;
- se la radio non e' in CW lo si dice prima di mandare («la radio e' in USB…»);
- le Yaesu tengono al massimo 50 caratteri per messaggio: una macro piu' lunga lo dice;
- «Ferma», con una radio che non sa fermare il CW da CAT, lo dice una volta sola invece di dare
  un errore a ogni pressione.

## 1.16.45 — 1 ottobre 2026

**HamAward / HamConnect via UDP.** I QSO registrati a mano in DecoDXLog possono ora essere
inviati anche nel formato N1MM XML richiesto da HamConnect: basta indicare nella riga
«Inoltra ad altri programmi» `127.0.0.1:12060`. Per HamAward la banda usa il bordo basso
corretto (20 m = `14`, 80 m = `3.5`) e SSB viene trasmesso come USB. Il nominativo della
stazione viene letto dal profilo attivo, cosi' HamAward puo' riconoscere l'attivatore.

**Uscita pulita.** Alla chiusura DecoDXLog ferma prima socket UDP, timer, servizi e lavori
in coda. Anche il keyer CW non aspetta piu' indefinitamente il suo thread quando una
porta seriale o il suo driver non risponde: la finestra chiusa deve quindi riportare subito
il terminale al prompt.

## 1.16.44 — 1 ottobre 2026

**Ricerca per campo, come nelle «Ricerche» di altri log.** Nel pannello Log, «Filtri → +
aggiungi → Altro campo» cerca i QSO per zona CQ (WAZ), zona ITU, continente (WAC), prefisso
WPX, QTH, stato o provincia, IOTA, POTA, SOTA, WWFF, altri riferimenti (SIG), QSL manager,
propagazione, satellite, contest, mese della cartolina inviata o ricevuta, mese della
conferma LoTW. Si sceglie il campo, poi il valore da un elenco con quanti QSO ha ciascuno
(contato su un altro filo, e si restringe scrivendo); i campi si sommano agli altri filtri
(banda, modo, entita' DXCC, QSL, date…) e si salvano con loro. Il prefisso WPX e' quello
scritto nel QSO o, se manca, il nominativo fino all'ultima cifra, anche da portatile (/P,
/M…).

**Le righe colorate per conferma.** «Azioni → Colora le righe secondo la conferma QSL»: verde
confermato da LoTW o dalla cartolina, giallo solo da eQSL o QRZ (che per il DXCC non
valgono), arancio cartolina partita e non ancora tornata. La legenda sta accanto ai filtri.

**Gli stessi filtri in tutti i log.** «Azioni → Cerca questi filtri in tutti i log…» passa i
filtri di adesso su ogni log dell'elenco (quello di tutti i giorni, i contest…), in sola
lettura e su un altro filo: una lista sola dal piu' recente, con il log di ogni QSO e quanti
ne ha trovati ciascuno. Il profilo stazione non conta, perche' ogni log numera i suoi.

**I 60 metri tornano nei conti.** Dalla 1.16.30 i QSO sui 60 metri erano fuori dal DXCC,
dal DXCC Challenge, dal WAS e dal DXCC anno per anno delle statistiche, come vuole l'ARRL
(«contacts on this band do not qualify for the ARRL award program»). Adesso di serie
contano di nuovo: il Challenge torna a undici bande, 60 compresi. Chi vuole il conto
ufficiale dell'ARRL spegne «60 m» nella finestra Diplomi, sulla riga «Valide per» del DXCC,
del Challenge o del WAS; la scheda
del DXCC e del WAS dice ogni volta come si sta contando.

- La ricerca libera guarda anche nel QTH.
- Le tendine troppo strette per la loro scelta la mostrano dall'inizio («Tutte le b…») e non
  dalla fine («…e le bande»), che non si capiva.

## 1.16.43 — 30 settembre 2026

**MicroHAM ARCO, senza programmi in mezzo.** In Impostazioni → Rotore c'e' ora
"MicroHAM ARCO / Yaesu GS-232": DecoDXLog legge la posizione (`C2`), punta (`W`) e
ferma (`S`) direttamente il controller. Si puo' usare la sua LAN (indirizzo ARCO e porta
4001) oppure solo il suo cavo USB/RS-232: niente Hamlib e niente `rotctld` da installare.
La porta USB/seriale e il baud rate restano salvati nelle impostazioni.

- La posizione dell'ARCO si legge solo a risposta intera: sulla seriale arriva a pezzi
  («AZ=1», poi «23 EL=000») e letta a meta' l'indice saltava a 1 grado prima di tornare a
  123. Oltre alla forma «AZ=… EL=…» si capisce anche quella del GS-232A («+0123+0045»).
- Le voci nuove del rotore sono tradotte in tutte le lingue.

## 1.16.42 — 30 settembre 2026

**Porte seriali che sono davvero porte.** CAT, rotore, SO2R e manipolatore CW ora usano
l'elenco di Qt delle porte seriali reali. Su macOS non compaiono piu' i pseudo-terminali
`/dev/ttys*`; restano le porte USB, Bluetooth e quelle create dai driver della radio.

**Nomi leggibili anche nelle tendine.** I menu della porta CAT e PTT si allargano quanto
serve per visualizzare per intero i percorsi delle porte seriali.

**CRX Logbook: la chiave rifiutata si dice per quello che e'.** CRX adesso, quando non
accetta la chiave API, risponde con un errore 500 e nel corpo `20-api-key-auth-error`.
DecoDXLog prendeva il 500 per «il servizio non risponde» e riprovava all'infinito, senza
mai dire che il problema era la chiave. Adesso lo dice, con il messaggio di CRX e dove si
rifa' la chiave (crx.cloud → utente → My account → my-api) e dove si incolla (Impostazioni
→ Servizi QSL). Lo stesso vale per «Carica l'elenco» dei logbook. La chiave si manda senza
spazi o a capo ai bordi, se ci sono finiti incollandola.

## 1.16.41 — 30 settembre 2026

**Ogni tasto macro manda la sua macro.** Nel pannello CW (e nella finestra del contest) tutti
i tasti mandavano la prima macro, il CQ, e il tasto destro apriva sempre la prima da
modificare: il numero della macro del tasto si chiamava come quello che il Repeater mette
nel delegato, e quello del tasto restava 0. Adesso ha un nome suo; {CALL}, {RST}, {NR} ed
{EXCH} si riempiono come prima. I tasti F1–F12 della tastiera andavano gia' giusti.

**Le macro cambiate restano cambiate.**
- Le macro tolte non tornano al riavvio: il passaggio da otto a dodici (per chi veniva
  dalle versioni con otto) si faceva a ogni avvio, e chi ne teneva otto se ne ritrovava
  dodici. Adesso si fa una volta sola.
- Il Cloud non riporta indietro quello che si e' cambiato qui e non e' ancora partito:
  arrivando le impostazioni da un altro computer (o da un altro log), vince la modifica
  fatta qui, che sale al sync dopo. Il resto arriva come sempre.
- «Macro di partenza» chiede conferma: al primo clic «Sicuro? Clicca di nuovo».

**Split col CAT di Decodium: detto com'e'.** Nella 1.16.40 il consiglio era mettere in
Decodium l'Operazione split su «Impianto» o «Simula»: non basta. Decodium lo split lo
gestisce da se' per l'FT8 e a ogni cambio di frequenza si rifa' la frequenza di
trasmissione, sovrascrivendo quella di DecoDXLog; con «Simula» poi la radio in split non
ci va proprio, e il CW di DecoDXLog esce sulla frequenza di ricezione. Adesso DecoDXLog,
tre secondi dopo lo split, controlla anche dove la radio trasmette e lo dice: per lo split
da DecoDXLog il CAT della radio lo deve tenere DecoDXLog (e Decodium si collega alla CAT
condivisa di DecoDXLog).

## 1.16.40 — 29 settembre 2026

**Le macro CW si scrivono davvero.** Nella finestra delle macro (pannello CW → «Macro…» o
tasto destro su un tasto) il campo prendeva il cursore ma i tasti si perdevano: la finestra
e' un popup a se', e Qt gli consegna la tastiera solo se il popup chiede il fuoco. Adesso lo
chiede, e quello che si scrive arriva al campo e si salva. Lo stesso guaio c'era nella
modifica di piu' QSO insieme, nella scelta delle colonne e nel colore scritto a mano:
corretto anche li'.

**Lo split dice perche' non va.** Col CAT che passa da Decodium (porta 4533) il comando di
split («S») il ponte di Decodium lo accetta e non lo esegue: lo split lo fa solo con la
frequenza di trasmissione, e solo se in Decodium Operazione split e' «Impianto» o
«Simula» — con «Nessuna» lo ignora, e la pillola SPLIT tornava spenta da sola, senza una
parola. Adesso, tre secondi dopo il comando, DecoDXLog chiede alla radio se lo split c'e'
e, se no, lo dice con la ragione e dove cambiarla. Con Decodium anche «Split spento»
funziona: si manda la frequenza di trasmissione a zero, che e' quello che il ponte capisce.

- La ricerca in alto non va piu' a capo e non si allunga: al massimo 220 punti, e prende
  solo lo spazio che resta sulla riga. Se non basta per il campo resta la lente (accesa se
  c'e' un filtro), che apre il campo sotto la barra; Ctrl+F fa lo stesso.

## 1.16.39 — 29 settembre 2026

**Le macro CW cambiate restano cambiate.** Nella lista delle macro ogni salvataggio rifaceva
tutte le righe: il campo dove si stava scrivendo spariva, e quello che si scriveva dopo aver
cambiato la prima casella non arrivava ne' alla radio ne' al file. Adesso le righe restano
quelle, ogni macro si salva uscendo dal campo e comunque quando la finestra (o le
Impostazioni) si chiude, e il file delle impostazioni si scrive subito sul disco. Le macro
arrivate dal Cloud da un altro computer si rileggono subito.

- La ricerca in alto sta sempre sulla riga degli altri blocchi: prende lo spazio che avanza,
  fra 140 e 300 punti, senza andare a capo.
- **Il biglietto d'avvio**, come in Decodium 4: all'apertura il logo, la versione, con che
  cosa e' scritto (C++20, Qt 6 · QML, GPL-3.0), chi lo fa — Martino Merola IU8LMC, autore e
  sviluppatore; Salvatore Raccampo 9H1SR, sviluppatore; Filippo Ricci G0YCE, supporto e
  social media manager — e il pulsante «☕ Offrimi un caffè» (buymeacoffee.com/iu8lmc).
  Si chiude da solo dopo dieci secondi, o subito con «Avvia ▶», Esc o Invio; il programma
  intanto si carica. Se all'avvio si chiede quale log aprire, la domanda arriva dopo.
- Informazioni: gli stessi crediti, «Costruito con» C++20 · Qt · QML, e il caffe'.
- README: le targhette in cima (versione, licenza, linguaggio C++20, Qt 6 · QML, autore,
  sviluppatore, supporto e social media, offrimi un caffe') e i crediti con i nomi.

## 1.16.38 — 29 settembre 2026

**Macro CW: quante ne vuoi, scritte dove vuoi.** Le macro non sono piu' dodici fisse: «+
Aggiungi una macro» ne mette una in fondo, «−» accanto a una riga la toglie (ne resta
almeno una), fino a 24. Le prime dodici stanno sui tasti F1–F12, le altre si mandano col
clic. Si scrivono in Impostazioni → Radio (CAT), sotto il manipolatore — non serve piu'
aprire la finestra del contest — oltre che dal pannello CW («Macro…» o tasto destro su un
tasto). Una finestra sola per tutte, anche dal contest.

- La ricerca in alto ha una misura sua, corta: non si allunga piu' fino a riempire la riga.
- Nel piè di pagina non ci sono piu' tema, densita', carattere e versione (la versione resta
  sotto il marchio, in alto a sinistra).
- Impostazioni → Radio (CAT): la riga SO2R va su due righe; tutta in fila era piu' larga
  della finestra e tagliava a destra l'intera pagina.

## 1.16.37 — 29 settembre 2026

**Macro CW: la scritta del tasto e' tutta tua.** Prima il tasto mostrava sempre «F1»
seguito dalla scritta, e «F1» non si cambiava. Adesso la scritta e' tutta della macro: si
puo' lasciare «F1 CQ», scrivere solo «CQ» o qualunque altra cosa (le macro salvate prima
si ritrovano con «F1», «F2»… davanti, come si vedevano). I tasti funzione della tastiera
restano F1–F12.

- Si modificano anche dal pannello CW: «Macro…» nell'intestazione le apre tutte, il tasto
  destro su un tasto apre solo quella, anche con la radio spenta.
- I tasti hanno tutti la stessa misura: una scritta lunga si accorcia con i puntini (per
  intero la si legge passandoci sopra) invece di allargare la griglia.
- Il testo del decoder CW scorre dentro la sua misura: non allunga piu' il pannello man
  mano che arriva.

**Cluster: Invio sullo spot scelto.** Scelto uno spot con un clic, Invio lo manda a
Decodium e alla radio come il doppio clic; le frecce su e giu' passano allo spot prima e a
quello dopo, e la scheda del nominativo segue. Lo spot scelto resta evidenziato.

**Radio TCI: niente caduta alla chiusura.** Con una radio collegata via TCI (ExpertSDR,
SunSDR), chiudendo il collegamento — o il programma — il socket poteva avvisare di essersi
staccato quando i suoi timer non c'erano gia' piu'. Su Linux la prova cadeva cosi'
(SIGSEGV); adesso il socket si stacca prima.

## 1.16.36 — 29 settembre 2026

**Split, VFO, RIT e XIT non tornano indietro.** Con rigctld, subito dopo aver acceso lo
split (o cambiato VFO, RIT, XIT) la barra in alto poteva mostrarlo spento per un giro: la
risposta a una domanda partita prima del comando arrivava dopo e rimetteva lo stato vecchio.
Adesso le risposte piu' vecchie dell'ultimo comando non contano.

**VOACAP su Windows, C++ altrove** (con elisir80). Su macOS e Linux DecoDXLog si costruisce
solo in C++ e la previsione sul percorso usa il modello interno; su Windows VOACAP resta nel
pacchetto come nella 1.16.35.

- Nella scheda Propagazione le tendine del rumore e delle antenne non tagliano piu' la
  scritta.
- Log vuoto: la frase al centro della tabella sta al suo posto e non da' piu' avvisi QML
  (elisir80).
- Rilascio per macOS: runner Intel aggiornato e librerie di Homebrew indicate a mano
  (elisir80).
- Le prove degli aggiornamenti firmati e della radio passano anche su Linux.

## 1.16.35 — 28 settembre 2026

**VOACAP nel programma.** La previsione sul percorso (scheda Propagazione) usa adesso VOACAP,
il motore di propagazione dell'ITS: per ogni ora e banda la probabilita' di fare il QSO
(affidabilita') e l'SNR, calcolati con le mappe CCIR, l'assorbimento, il rumore e il guadagno
delle antenne. Sotto la griglia si sceglie il modo (FT8, FT4, FT2, CW, RTTY, SSB: decide l'SNR
che serve), la potenza, le antenne e il rumore del posto. VOACAP e' nel pacchetto
(`voacap\`, voacapl di J. A. Watson: dominio pubblico e CC0); il 160 e il 6 m, fuori dalla sua
gamma, restano al modello semplice, che torna anche se VOACAP non c'e' o non risponde.

**Un log per piu' operatori.** Una stazione di club, un contest multi-operatore: un log sul
Cloud, piu' operatori, ognuno con il suo account — nessuno presta una password. Chi tiene il
log crea un invito (Impostazioni → Sync e Cloud → Log condiviso, o dalla pagina Stazione del
Cloud), chi riceve il codice entra, e sceglie quale suo file di log va con quello condiviso.
Operatore: manda e prende i QSO; osservatore: guarda soltanto. Viaggiano solo i QSO: profili,
impostazioni e password restano di chi tiene il log. **Serve il server aggiornato**
(`server/deploy/update.sh`): finche' non lo e', il programma lo dice.

**Venire da un altro programma.** Importa log (Ctrl+I) legge, oltre all'ADIF, i fogli di
calcolo (CSV, con le colonne riconosciute in italiano, in inglese o col nome ADIF; separatore,
date e kHz capiti da soli) e il database di N1MM Logger+ (`.s3db`) direttamente. Le conferme
che il logbook di QRZ scrive nei campi suoi diventano conferme vere.

**Un campo su molti QSO, e i doppioni.**

- Dal menu della riga (sui QSO selezionati) o da Azioni (su quelli mostrati): lo stesso valore
  in un campo — il locatore di casa, un riferimento POTA, le QSL spedite, il profilo di
  stazione. Ogni QSO tiene il valore di prima nel suo storico; con molti QSO lavora da parte.
- Azioni → Trova i QSO doppi: stesso nominativo, banda, genere di modo e profilo entro i
  minuti scelti. Di ogni gruppo se ne tiene uno (il piu' confermato, o quello che scegli), che
  prende dagli altri campi, conferme ed etichette; gli altri si cancellano e restano nello
  storico.

**Account eQSL e QRZ per profilo.** Nei profili stazione ogni profilo puo' avere i suoi
account eQSL e QRZ Logbook: l'invio usa quelli del profilo del QSO, lo scarico delle conferme
li prova tutti.

**Collegamenti con altri programmi.**

- Ripetitore UDP: quello che arriva da Decodium va anche ad altri programmi (GridTracker,
  JTAlert…), e le loro risposte tornano a Decodium.
- N1MM Logger+: i QSO arrivano dal suo pacchetto UDP (ContactInfo), anche corretti o
  cancellati.
- Log ADIF tenuti d'occhio: fldigi, WSJT-X, JTDX o un altro programma che scrive un ADIF; i
  QSO nuovi entrano da soli, come quelli ripresi da Decodium.
- Interfaccia HTTP locale (`docs/API.md`), solo su 127.0.0.1 e con una chiave: gia' lavorato,
  ultimi QSO, registrare un QSO.
- Wavelog tra i servizi QSL, con la scelta della stazione.

**Diplomi.** Ogni diploma puo' avere le sue conferme valide (LoTW, eQSL, cartolina, QRZ…).
Con la chiave API di Club Log, ogni settimana arriva il suo `cty.xml`: le entita' con le
date, cosi' un QSO del 2005 con PJ2 e' Antille Olandesi, le operazioni non accettate
dall'ARRL non contano per il DXCC e le entita' cancellate si vedono ma non contano. Un
controllo confronta il log con le date e corregge solo il DXCC vuoto o messo dal cty.csv.

**Radio: split, VFO B, RIT, XIT.** Con rigctld, TCI e flrig, nella barra in alto: split
acceso o spento, VFO A/B, RIT e XIT, e la frequenza di trasmissione in split.

**Contest.** Le regole si adattano all'edizione (bande, ore di attivita', pause, termine per
il log), il tempo in aria e le pause si contano come vuole il regolamento (WPX: 36 ore su 48,
pause di un'ora), il file call history (formato N1MM) suggerisce lo scambio, e in rete un PC
distribuisce i numeri progressivi a tutti.

**Log grandi.** Su un log da un milione di QSO: ordinare per colonna e cercare non fermano
piu' la finestra (si preparano da parte, prima 2 secondi fermi), un QSO scritto a mano con
un'ora vecchia entra in 0,35 secondi invece di 4,3, la tabella si ricarica in 0,18 secondi
invece di 0,5, e la scheda del nominativo per le entita' piu' comuni conta da parte invece
di fermarsi mezzo secondo.

## 1.16.34 — 28 settembre 2026

**I QSO rimasti in Decodium si recuperano da soli.** Decodium scrive ogni QSO anche nel suo
`decodium_log.adi`. Se DecoDXLog era chiuso, o non riceveva (porta UDP diversa, rete giu'),
il QSO restava solo li'. Adesso ogni 5 minuti DecoDXLog legge i QSO registrati dall'ultimo
controllo e salva quelli che non ha.

- Il file e' quello che Decodium sta usando (`Decodium3.ini`, `[Logbooks] ActivePath`), o
  uno scelto a mano in Impostazioni → Collegamento a Decodium. Si legge solo la coda: il
  pezzo che serve, anche con un file di anni.
- Non tornano i QSO corretti o cancellati qui (si guarda anche lo storico delle modifiche),
  ne' quelli finiti in un altro log dell'elenco, come il log di una gara. Quelli degli
  ultimi tre minuti si lasciano all'UDP, che sta ancora arrivando.
- La prima volta si guarda indietro una settimana. Due pulsanti controllano gli ultimi 30
  giorni o tutto il file; oltre venti QSO si salvano tutti insieme, con una riga sola nel
  registro.
- I QSO recuperati hanno la sorgente «rec» nella tabella, prendono entita' DXCC, profilo
  stazione, callbook e invio QSL automatico come quelli via UDP. Un'attivazione o una gara
  aperta li tratta come fa con l'UDP, e quelli scartati come doppioni si contano nel
  registro: un'attivazione dimenticata aperta non li fa sparire in silenzio.

**Conferme eQSL e QRZ anche da sole.** In Impostazioni → Servizi QSL c'e' una sezione nuova,
«Conferme eQSL e QRZ Logbook», con lo scarico automatico: spento, ogni 6 ore, ogni 12 (la
scelta iniziale, come per LoTW) o una volta al giorno.

- Parte solo per i servizi di cui ci sono le credenziali: chi non usa eQSL o QRZ non vede
  niente. Un servizio alla volta, il primo un minuto dopo l'avvio e poi al controllo ogni
  dieci minuti.
- Un tentativo fallito (rete giu', password cambiata) si riprova al giro dopo, non a ogni
  controllo; e non sposta il «da quando»: al primo scarico riuscito arriva tutto quello
  che nel frattempo mancava.
- Nella stessa sezione: ultimo scarico di ogni servizio, i pulsanti per scaricare adesso e
  quelli per riscaricare tutto da capo.

## 1.16.33 — 28 settembre 2026

**Niente piu' driver video che non servono.** L'audio (avviso del cluster, voice keyer,
decoder CW) passa da Qt Multimedia, e il suo motore FFmpeg all'avvio provava tutte le
accelerazioni video: CUDA, Vulkan, Direct3D 11, DXVA2. Ognuna apriva il suo driver: sulle
schede NVIDIA anche il driver Vulkan (nvoglv64.dll), e con lui gli overlay che si agganciano
a Vulkan, come quello di OBS. DecoDXLog non decodifica video: adesso non le prova piu'.
L'avvio e' tre quarti di secondo piu' rapido e senza finestra il programma usa 56 MB in
meno. Chi le vuole puo' ancora impostare `QT_FFMPEG_DECODING_HW_DEVICE_TYPES`.

## 1.16.32 — 28 settembre 2026

**Conferme da eQSL e da QRZ Logbook.** Nella scheda «Invio QSL», accanto a LoTW, i pulsanti
«Conferme eQSL» e «Conferme QRZ».

- eQSL: si scaricano le eQSL arrivate nella casella (DownloadInBox), con utente e password
  gia' messi in Impostazioni → Servizi QSL. Le segnalazioni d'ascolto (SWL) restano fuori.
- QRZ Logbook: con la chiave API del logbook si leggono i QSO che l'altra stazione ha
  confermato, a pagine da 250 finche' ce ne sono.
- Solo quello che e' arrivato dopo l'ultimo scarico (con un giorno di margine); la prima
  volta tutto. Le conferme si abbinano ai QSO come quelle di LoTW (nominativo, banda, gruppo
  di modi, entro mezz'ora) e segnano EQSL_QSL_RCVD o QRZCOM_QSO_DOWNLOAD_STATUS con la data;
  la cartolina resta com'era. Nel registro attivita': quante nuove, quante gia' segnate,
  quali non sono nel log, e i DXCC confermati per la prima volta.
- La password di eQSL viaggia nell'indirizzo, come vuole eQSL: nei messaggi d'errore non
  compare, e nemmeno la chiave di QRZ.

**Meno memoria su Windows.** La grafica in automatico usa Direct3D 11 invece di Direct3D 12:
sullo stesso log da 24.000 QSO 480 MB invece di 845. `--d3d12` resta per chi la vuole.

**Date impossibili nell'importazione.** Un QSO con una data prima del 1930 (tipicamente il
30/12/1899, lo «zero» delle date di Excel e di alcuni programmi quando la data manca) non
entra piu': l'importazione lo scarta dicendo quale. Quelli gia' nel log restano, da
correggere a mano.

**Log vuoto.** La frase «Il log e' vuoto…» finiva mezza nascosta sotto le intestazioni della
tabella; adesso si legge.

## 1.16.31 — 28 settembre 2026

**Ripristinare un backup.** Impostazioni → Backup → «Ripristina un backup…».

- Si sceglie una copia della cartella dei backup (o un file qualsiasi). Prima di toccare
  niente si guarda cosa c'e' dentro: se SQLite la trova sana, quanti QSO, dal primo
  all'ultimo, quanti in meno o in piu' del log di adesso.
- Il ripristino riavvia il programma e rimette la copia prima di aprire il log, quando il
  processo di prima e' uscito. Il log com'era viene salvato prima nella cartella dei backup
  come `decodxlog-before-restore-<ora>.sqlite` (la pulizia delle copie notturne non lo
  tocca): per tornare indietro si ripristina quello. I `-wal` e `-shm` del log di prima si
  tolgono, perche' applicati alla copia la rovinerebbero.
- Una copia rovinata, un file che non e' un log, o il log stesso non si rimettono, e il log
  resta com'e'.

**Log grandi: provato a 100.000 e a un milione di QSO** (strumento `decodxlog_benchlog`,
log finti con l'importazione vera).

- La tabella tiene in memoria solo gli id e legge i valori a pagine: un milione di QSO si
  apre in mezzo secondo con 25 MB, prima erano 21 secondi e 1,3 GB. Filtri e ordine li fa
  SQLite; la ricerca libera legge il log in fila (un secondo su un milione, prima sette). I
  colori delle righe si contano su un altro filo.
- Su un altro filo anche l'importazione ADIF (a blocchi di mille, con l'avanzamento
  nell'intestazione del log, e i QSO dalla radio passano fra un blocco e l'altro),
  l'esportazione, il backup, le statistiche, i diplomi all'avvio, i conti della pagina QSL,
  l'elenco del SuperCheck e l'elenco dei lavorati per Decodium (DecoLink).
- Esportare 100.000 QSO: da 21 secondi a meno di 3. Le query che leggono tutto il log vanno
  in fila (NOT INDEXED) invece di saltare per il file seguendo un indice; nuovi indici per
  sottomodo, QSO non cancellati in ordine di tempo, etichette e profili, e le statistiche
  di SQLite si aggiornano da sole all'apertura.
- Avvio su un milione di QSO: la finestra risponde in 3 secondi (prima 85). Un QSO da
  Decodium su un log da un milione fermava la finestra fino a 28 secondi; adesso sei QSO di
  fila le costano una sola pausa di 2 secondi. Su 100.000 QSO tutto quello che si fa a mano
  resta sotto il mezzo secondo.

**Doppioni FT8, CW e SSB.** Un QSO senza sottomodo non trovava mai il suo doppione: WSJT-X
che rimanda lo stesso QSO, o lo stesso ADIF importato due volte, finivano nel log due volte.
Adesso si riconoscono come quelli FT2.

**Prove dal pacchetto UDP al QSO nel log.** Una prova nuova avvia DecoDXLog vero, senza
finestra, gli manda i datagrammi di Decodium (QSOLogged e LoggedADIF), di JTDX (solo
QSOLogged) e di WSJT-X, doppioni, datagrammi rotti e una raffica di venti QSO, e guarda nel
database: uno per QSO, con la fonte giusta e il DXCC dal cty.csv.

- Tradotto in 15 lingue.

## 1.16.30 — 28 settembre 2026

**Aggiornamenti firmati.** DecoDXLog installa da solo solo quello che chi pubblica ha firmato
(docs/firma-aggiornamenti.md).

- Ogni release porta l'elenco dei suoi file con SHA-256 e dimensione (`decodxlog-release.json`)
  e la sua firma Ed25519 (`.sig`). Il programma conosce le chiavi pubbliche di chi pubblica, una
  lista per repository, e prima di «Aggiorna ora» controlla firma, repository e versione.
- Il pacchetto scaricato resta solo se il suo SHA-256 e' quello firmato: altrimenti viene buttato
  prima di scriverlo, anche sopra l'AppImage che gira su Linux. Prima si controllava solo la
  dimensione.
- Una release senza firma, o firmata con una chiave che non si conosce, si vede lo stesso nella
  finestra, con un avviso: si scarica a mano dalla pagina. Una firma che non torna si segnala in
  rosso anche nel registro attivita'.
- Fra elisir80/DecodxLog e iu8lmc/DecoDXLog vince la versione piu' nuova che risulta firmata.
- La firma e' di Monocypher 4.0.2 (libs/monocypher), provata con i vettori dell'RFC 8032. Lo
  strumento `decodxlog_sign` crea le chiavi nel portachiavi di sistema e firma le release
  (`scripts/sign-release.sh`); la chiave segreta non sta nel repository.

**Contest e diplomi contati come dicono i regolamenti.** Le correzioni della revisione esterna,
ognuna ricontrollata sul regolamento ufficiale.

- **CQ WW RTTY con le sue regole** (cqwwrtty.com): stesso paese 1 punto, stesso continente 2,
  altro continente 3, senza l'eccezione del Nord America che vale in CW e SSB. Ai moltiplicatori
  zona e paese si aggiunge il QTH W/VE: i 48 stati continentali, il DC e le 14 aree canadesi,
  per banda (Alaska e Hawaii solo come paese). Lo scambio accetta "05 MA" e lo controlla; il
  suggerimento mette zona e stato da una gara gia' fatta o dallo STATE del log. Nella finestra
  dei moltiplicatori c'e' la colonna "QTH W/VE".
- **CQ WPX RTTY con le sue regole** (cqwpxrtty.com): 1, 2 e 3 punti, il doppio su 40 e 80 metri
  anche nel proprio paese, niente eccezione per il Nord America.
- **Bande della gara**: i CQ in RTTY si fanno dagli 80 ai 10 metri, quelli in CW e SSB e lo IARU
  dai 160 ai 10. Un QSO su un'altra banda (i WARC, o i 160 in RTTY) non porta punti ne'
  moltiplicatori, e la finestra dei moltiplicatori mostra solo le bande della gara.
- **Scadenze e portali giusti**: tutti i CQ (WW e WPX, in CW, SSB e RTTY) vogliono il log entro
  **48 ore**, non cinque giorni; i due RTTY vanno su cqwwrtty.com/logcheck e
  cqwpxrtty.com/logcheck, non sui portali di CW e SSB.
- **La zona che conta e' quella ricevuta**: nel CQ WW e nello IARU il moltiplicatore segue la zona
  mandata dal corrispondente; quella del cty.csv si usa solo quando lo scambio non c'e' (gli
  spot). Prima valeva quella del cty.csv, che per molte stazioni americane e canadesi non e'
  la zona in cui stanno.
- **DXCC come lo conta l'ARRL**: i QSO sui 60 metri non valgono per il DXCC, per il DXCC Challenge
  (che torna a dieci bande) e per il WAS, e per il DXCC l'eQSL non e' una conferma anche con
  "eQSL" acceso (vale ancora per gli altri diplomi). Nella scheda del diploma lo si legge.
- Tradotto in 15 lingue.

## 1.16.29 — 28 settembre 2026

**Allineata alla 1.16.28 di elisir80: il Nuovo QSO piu' affidabile, con la CAT condivisa.**

- Porta dentro le correzioni della 1.16.28 di elisir80 (qui sotto): nel Nuovo QSO le tendine
  Modo e Prop non cercano piu' di scrivere una proprieta' in sola lettura, la data UTC non
  torna al giorno corrente quando si rientra nel nominativo, e una data impossibile si vede
  in rosso e non entra nel log ("Inserisci una data UTC valida", tradotto in 15 lingue).
- Con tutto quello della 1.16.27: la CAT condivisa come in Decodium 4.

## 1.16.28 — 27 settembre 2026

**Nuovo QSO piu' affidabile.** Le tendine Modo e Prop non cercano piu' di
scrivere una proprieta' Qt in sola lettura. La data UTC non torna al giorno
corrente quando si rientra nel nominativo, una data impossibile viene mostrata
in rosso e non puo' essere salvata nel log.

**Pacchetti coerenti con la release.** I runner macOS e Linux costruiscono
esattamente il tag richiesto e nominano DMG e AppImage con quel numero di
versione; l'Intel macOS usa il runner `macos-15-intel`.

## 1.16.27 — 27 settembre 2026

**La CAT condivisa, come in Decodium 4.**

- Quando la radio la tiene DecoDXLog (cavo seriale con il suo rigctld, TCI, flrig o
  OmniRig), gli altri programmi di questo computer la possono usare lo stesso: DecoDXLog
  la rivende su 127.0.0.1 con il protocollo di rigctld, e Decodium, WSJT-X o un altro log
  si collegano come "Hamlib NET rigctl" (porta 4533 di serie). Impostazioni → Radio (CAT)
  → CAT condivisa.
- Lo stesso dialogo di Decodium 4, verificato con il vero client Hamlib (`rigctl -m 2`):
  frequenza, modo, VFO in lettura; cambiare frequenza e modo solo con "Consenti il
  controllo", trasmettere solo con "Consenti la trasmissione". Se la porta e' gia' presa
  (Decodium che condivide sulla stessa), lo dice.
- Il contrario, quando la radio la tiene Decodium: "Usa una CAT condivisa" con
  127.0.0.1:4533 e Collegati, e DecoDXLog comanda la radio attraverso Decodium.

## 1.16.26 — 27 settembre 2026

**La band map non ferma piu' la finestra quando il cluster manda spot a raffica.**

- Con l'RBN acceso e una banda piena di spot, la band map rifaceva tutte le sue etichette
  (centinaia) a ogni aggiornamento: ogni secondo la finestra si fermava per un sesto di
  secondo, e spostando o ridimensionando i pannelli si sentiva a scatti. Adesso crea solo
  le etichette attorno a quello che si vede, si aggiorna ogni secondo e mezzo e, quando e'
  chiusa o nascosta, non si aggiorna affatto. Misurato con 60 spot al secondo sulla stessa
  banda: da 150 ms di fermo al secondo a nessun fermo avvertibile.
- All'avvio l'elenco degli utenti LoTW (centinaia di migliaia di righe) e l'elenco di chi e'
  gia' stato lavorato si leggono su un altro filo: niente piu' mezzo secondo di finestra
  ferma appena aperto il programma.
- La nota delle impostazioni sulle macro dice dove si scrivono le dodici macro.

## 1.16.25 — 27 settembre 2026

**Molto piu' svelto con un log grande: niente piu' finestra ferma fra un pannello e l'altro e dopo ogni QSO.**

Misurato con un log vero di 24.000 QSO:
- Dopo ogni QSO la finestra restava ferma circa un secondo e mezzo; adesso un decimo di
  secondo. Il conto delle categorie del log (nuovo DXCC, nuova zona...) non rilegge piu'
  tutto il log per un QSO nuovo; i conteggi della barra in basso (QSO, coda, QSL) si fanno
  una volta sola, su un altro filo, invece che a ogni lettura; l'elenco di chi e' gia'
  stato lavorato per il cluster si rifa' anch'esso su un altro filo.
- Passare dalla gara alla finestra di tutti i giorni costava quasi un secondo di finestra
  ferma, perche' tutti i pannelli venivano distrutti e rifatti: adesso restano pronti, e il
  passaggio e' immediato (la prima volta che si entra in gara si preparano i suoi).
- I diplomi si contano in un quindicesimo del tempo (da 3,3 s a 0,2 s): un'espressione
  regolare veniva ricostruita per ogni QSO italiano. Anche quelli che servono a Decodium
  (DecoLink) si contano in secondo piano e gli arrivano quando sono pronti, invece di
  fermare la finestra alla prima domanda dopo ogni QSO.
- L'avvio e' due volte piu' rapido (il caricamento dell'interfaccia da 7,5 a 3,7 s).
- Il registro attivita' aggiunge le righe una alla volta invece di rifare tutta la lista a
  ogni messaggio; le regole d'avviso con i nominativi non ricostruiscono piu' le loro
  espressioni a ogni spot.

## 1.16.24 — 27 settembre 2026

**Quello che mancava rispetto ai log blasonati: band map, avvisi, propagazione, rete, SO2R, WinKeyer, DVK.**

Per il DX di tutti i giorni:
- **Band map**: un pannello nuovo (Pannelli → Band map) con la banda in verticale, gli spot
  posati sulla loro frequenza e colorati per stato (nuovo DXCC, nuova banda...), la riga
  rossa della radio. Un clic su uno spot porta li' la radio e prepara il QSO, un clic sulla
  scala sposta la radio, Ctrl+rotella cambia lo zoom. Segue la banda della radio o una scelta.
- **Avvisi DX con regole**: oltre a voce e Decodium, una regola puo' mostrare un riquadro
  (un clic sintonizza) e suonare. Stati nuovi nei filtri: NEW ZONE, NEW IOTA, IOTA non
  confermata, NEW REF (POTA/SOTA/WWFF mai lavorata).
- **Previsione di propagazione sul percorso**: in Propagazione, verso il DX della scheda o un
  locatore scritto a mano, le 24 ore per le bande dal 160 al 6 m (buona, discreta, marginale,
  chiusa), con MUF e LUF ora per ora. Un modello F2 semplificato, non VOACAP.
- **HRDLog.net** fra i servizi di invio (nominativo e codice di upload nel portachiavi) e la
  **chat ON4KST** (menu → ON4KST chat) con stanze, messaggi privati e chi c'e'.
- **CAT anche via flrig e OmniRig**, oltre a Hamlib e TCI.
- **Statistiche**: pagina "Diplomi e classifiche" con la curva dei diplomi anno per anno
  (DXCC lavorati e confermati, zone CQ, locatori), entita' e nominativi piu' lavorati, banda
  per modo.
- **Diplomi italiani**: WAIR (le venti regioni, con quelle che mancano), IIA, DIFI e DAI dai
  campi SIG/SIG_INFO o dal commento.

Per i contest:
- **Super Check Partial e N+1** nell'inserimento: MASTER.SCP (si scarica con un clic) piu' i
  nominativi del log; un clic mette il nominativo nel campo.
- **Rete multi-operatore sulla LAN** (pannello Rete di stazione): i QSO di ogni PC arrivano
  negli altri log, doppi e moltiplicatori sono di tutti, messaggi fra operatori, spot interni,
  sincronia per chi arriva dopo.
- **SO2R**: seconda radio (rigctld, TCI, flrig, OmniRig), fuoco con Ctrl+←/→ e Pausa, stereo
  con `, scatola SO2R via OTRSP.
- **WinKeyer K1EL** come manipolatore, accanto al piedino DTR/RTS.
- **Keyer vocale (DVK)**: otto messaggi registrati dal microfono o da WAV, PTT della radio,
  CQ a ripetizione; in fonia i tasti funzione suonano i messaggi.
- **ESM (Enter Sends Message)** in Run e S&P, dodici tasti funzione e l'editor delle macro
  nell'inserimento del contest.
- **Moltiplicatori per banda** (pannello Moltiplicatori), con zone e province che mancano, e
  sotto il nominativo il controllo: dove il suo moltiplicatore e' gia' preso e dove manca.

## 1.16.23 — 27 settembre 2026

**L'orologio mondiale, e il log si ordina cliccando le intestazioni delle colonne.**

- Nella barra in basso, a destra, l'orologio mondiale: una mini-mappa con la notte e la
  grayline, l'ora UTC coi secondi, il locatore del QTH con l'ora locale, l'alba e il tramonto,
  e due citta' DX a scelta. Si apre con un clic, con Invio o con lo Spazio.
- Aperto in grande: la mappa del mondo (Natural Earth, con i confini) con il giorno, i tre
  crepuscoli (civile, nautico, astronomico) e la notte in fasce, la grayline, il Sole a picco
  e le citta' con la loro ora. A destra i fusi orari: per ogni citta' l'ora coi secondi, lo
  scarto da UTC, il locatore, la data (con "+1 g" / "−1 g" quando e' gia' domani o ancora
  ieri), se e' giorno, grayline o notte, l'alba e il tramonto. Sotto la mappa i dettagli
  della citta' scelta: alba, tramonto, mezzogiorno solare, durata del giorno, grayline del
  mattino e della sera (in ora locale e UTC), altezza del Sole, ora locale.
- Le citta' si aggiungono dalla tendina (28 capitali e citta' DX), si tolgono, e si sceglie
  quali due mostrare nella barra; tutto resta alla prossima apertura. Il QTH della stazione
  c'e' sempre, dal locatore del profilo. Ora legale e poli (sole di mezzanotte, notte polare)
  compresi. Niente rete: e' tutto calcolato sul computer, e l'orologio si ferma quando la
  finestra e' ridotta a icona. Si chiude con la ✕, con Esc o con un clic fuori.

- Un clic sull'intestazione ordina il log per quella colonna: ora, nominativo, banda (nell'ordine
  delle frequenze, non dell'alfabeto), frequenza, modo, nome, paese, date, qualsiasi colonna
  scelta. Un altro clic sulla stessa colonna la gira al contrario. La freccia ▲▼ dice come e'
  ordinato. I numeri si ordinano come numeri, le date come date, le righe vuote vanno in fondo.
- L'ordine scelto resta: con i filtri, con i QSO nuovi e alla prossima apertura. Trascinare
  l'intestazione continua a spostare la colonna, tirarne il bordo ad allargarla.

## 1.16.22 — 27 settembre 2026

**La foto grande del callbook non lampeggia piu'.**

- Col mouse sopra la foto della scheda nominativo, la foto grande (640x480) compare di fianco
  alla miniatura, dal lato dove c'e' posto, invece che sotto il mouse: prima copriva la
  miniatura, la faceva sparire e ricomparire di continuo. Si chiude un attimo dopo che il
  mouse se ne va, e si puo' passare dalla miniatura alla foto grande senza perderla.
- "Punta il DX" prende la rotta del nominativo della scheda anche quando il profilo della
  stazione non ha il locatore: la conta dal QTH del gateway del rotore, come gia' per gli spot.

## 1.16.21 — 27 settembre 2026

- La mappa riutilizza i locatori calcolati invece di rileggere il database a ogni
  accesso QML. La cache si invalida quando cambia il log o si apre un altro database.
  Corretto il percorso che nella traccia KDE eseguiva 1.510 scansioni, per 287,9 secondi.
- Diagnostica di avvio facoltativa con `DECODXLOG_TRACE_STARTUP=1`: tempi di
  database, pannelli, diplomi e dispositivi audio, senza registrare i QSO.
- Verificata localmente la mappa compatibile su una copia del log di 6.547 QSO:
  una sola chiamata a `workedGrids`. La verifica sul computer KDE resta necessaria.

## 1.16.20 — 26 settembre 2026

**Aggiornamenti giusti per ogni computer, senza avvisi per pacchetti di altri sistemi.**

- Il controllo aggiornamenti cerca prima le release pubblicate su `elisir80/DecodxLog` e usa
  `iu8lmc/DecoDXLog` solo come riserva. Guarda l'elenco delle release, non soltanto l'ultima:
  se per esempio esce prima Windows, macOS e Linux non ricevono un avviso falso.
- Si sceglie esclusivamente il pacchetto installabile della macchina che sta usando il log:
  setup `.exe` su Windows, DMG Apple Silicon o Intel su macOS, AppImage x86_64 o aarch64 su
  Linux. ZIP portabili, sorgenti e binari dell'architettura sbagliata non vengono proposti.
- Il download viene scritto in modo atomico e confrontato con la dimensione dichiarata da
  GitHub. Su Linux l'AppImage in uso viene sostituita in sicurezza e riavviata quando la sua
  cartella e' scrivibile; altrimenti quella nuova resta in Download. Su macOS si apre il DMG.
- La finestra di aggiornamento dice anche da quale repository arriva il pacchetto scelto.
- La mappa compatibile Linux non appare piu' come un reticolo vuoto: usa le terre emerse
  vettoriali di Natural Earth insieme a locator, spot, stazione e rotore, senza istanziare
  `Canvas` o una texture FBO che potrebbe bloccare KWin/Mesa.
- Il packaging macOS firma anche le librerie Homebrew con un suffisso di versione (per esempio
  `libbrotlicommon.1.dylib`), cosi' il DMG Intel non fallisce durante la verifica della firma.

## 1.16.19 — 26 settembre 2026

**Le righe del log colorate come i decode di Decodium 4, e la foto del callbook in grande.**

- Ogni QSO del log prende la prima cosa nuova che ha portato quando e' stato fatto: nuova
  entita' DXCC (o sulla banda), nuovo continente, nuova zona CQ, nuova zona ITU, nuovo locatore,
  nuovo nominativo (in assoluto o sulla banda), confermato su LoTW, oppure gia' lavorato (B4).
  Come in Decodium 4, ogni categoria ha il colore del testo e, se si vuole, il fondo della riga:
  si scelgono in Impostazioni → Tema e densita' → Colori delle righe del log, dalla tavolozza o
  scrivendo il codice (#RRGGBB, o #AARRGGBB per un fondo trasparente). "Colori di Decodium"
  rimette quelli di serie. Accesi di serie entita', continenti e zone; locatore, nominativo,
  LoTW e B4 si accendono se li si vuole.
- Nella scheda nominativo, col mouse sopra la foto del callbook compare la foto grande, 640x480.

## 1.16.18 — 26 settembre 2026

**Il pannello del rotore: quadrante grande al centro, i gradi e "Punta il DX".**

- Nella finestra principale il pannello Rotore ha solo il quadrante, grande e centrato, i gradi
  (e dove sta andando) e il pulsante "Punta il DX". Passi, STOP, park e il resto stanno nella
  finestra del rotore (Apri ▾, Ctrl+R).
- Scegliendo uno spot nel cluster (clic o doppio clic) il rotore ne prende subito la rotta, e il
  pulsante la dice: "Punta il DX · VK9XX 98°". Vale anche per il nominativo nella scheda. Se il
  QTH della stazione non c'e', la rotta si conta da quello del gateway del rotore.

## 1.16.17 — 26 settembre 2026

**Lo scarico da LoTW anche per un periodo: dal … al ….**

- Accanto a "Scarica le conferme LoTW" (scheda Invio QSL) c'e' "LoTW dal… al…", e in
  Impostazioni → Servizi QSL una riga con le due date: si scaricano le conferme dei QSO fatti
  in quel periodo, estremi compresi. Una delle due date si puo' lasciare vuota (dal primo QSO,
  o fino a oggi); si scrivono come nel resto del programma (in italiano gg/mm/aaaa).
- Lo scarico per periodo non sposta il segno dell'ultimo scarico: "Sincronizza adesso"
  riparte da dove era e non perde le conferme arrivate nel frattempo per gli altri QSO.

## 1.16.16 — 26 settembre 2026

**Il gateway del rotore e' dentro DecoDXLog: DecoRotor non serve piu'.**

- In Impostazioni → Rotore, "Parla con" ha di serie "il control box, direttamente": DecoDXLog
  apre da se' la seriale del control box PRO.SIS.TEL (control box D, azimut e/o elevazione, o
  Combi-Track, anche riconosciuto da solo) e fa tutto quello che faceva DecoRotor: interroga
  la posizione cinque volte al secondo, applica finecorsa, riposo, tolleranza, stop se il
  rotore non si muove e stop se sparisce l'ultimo client durante un movimento.
- Per il telefono e gli altri programmi non cambia niente: WebSocket 8765 per l'app (stesso
  protocollo, memorie e configurazione a caldo comprese), pagina web e API REST sulla 8080,
  riquadri della mappa satellitare in cache, rotctld di Hamlib sulla 4532 per N1MM+, Log4OM,
  PstRotator e gli altri. Le stazioni sulla mappa dell'app arrivano da Decodium (decode e QSO
  in corso) e dal cluster, attraverso DecoDXLog.
- "Prendile da DecoRotor" legge il config.json di DecoRotor: porta, modello, porte, finecorsa
  e memorie passano in DecoDXLog. C'e' anche il control box simulato, per provare senza rotore.
- DecoRotor va chiuso: la seriale e le porte possono avere un solo padrone; se sono occupate,
  DecoDXLog dice quali. Restano possibili anche DecoRotor a parte e un rotctld qualsiasi.

## 1.16.15 — 25 settembre 2026

**Il Nuovo QSO muove la radio.**

- Come l'inserimento della gara: scegliendo la banda la radio
  va li' (dove la si era lasciata in quel modo, o all'inizio del segmento del modo, piano IARU
  Regione 1), scegliendo il modo cambia modo; la frequenza si scrive anche nel campo. Vale nel
  pannello e nella finestra del nuovo QSO, con la radio (Hamlib o TCI) o con Decodium.
- Al contrario, girando la manopola il pannello Nuovo QSO segue frequenza, banda e modo della
  radio, e non solo quelli di Decodium.

## 1.16.14 — 25 settembre 2026

Release di manutenzione con il codebase aggiornato e i pacchetti multipiattaforma.

## 1.16.13 — 25 settembre 2026

**La radio anche via TCI, come in Decodium.**

- In Impostazioni → Radio (CAT), "Come" ha una terza strada: TCI, il protocollo delle SDR
  Expert Electronics (SunSDR, ColibriNANO con ExpertSDR) e dei programmi che lo parlano.
  Basta l'indirizzo del server (di serie 127.0.0.1:40001) e il ricevitore (RX1 o RX2).
- Frequenza e modo arrivano appena cambiano, senza interrogare la radio; DecoDXLog la
  sintonizza (cluster, inserimento veloce, banda e modo), usa il suo PTT e manda le macro CW
  con il manipolatore della SDR (cw_macros). Decodium puo' restare collegato insieme.

## 1.16.12 — 25 settembre 2026

**JTTY, il modo nuovo di Joe Taylor K1JT (WSJT-X 3.2.0-rc1).**

- JTTY e' il modo per gli scambi veloci da contest e le conversazioni in tastiera, simile al
  RTTY ma molto piu' robusto (4 toni GFSK, circa 127 Hz). Si sceglie nell'inserimento, nella
  finestra del nuovo QSO (MFSK → JTTY), nell'inserimento della gara e nelle statistiche.
- Nel log va come gli altri modi nuovi di WSJT-X: MODE MFSK, SUBMODE JTTY, e si legge JTTY.
  Chi lo manda o lo importa scritto JTTY lo ritrova cosi'. La radio va in PKTUSB, nel segmento
  dei digitali a banda stretta; gli spot con "JTTY" nel commento sono JTTY.

## 1.16.11 — 25 settembre 2026

**La finestra principale e' una lavagna magnetica, come quella della gara.**

- Fuori dalla gara i pannelli (nuovo QSO, log, scheda nominativo, CW, rotore, FT2 Award,
  schede in basso, mappa) si comportano esattamente come in modalita' contest: si prendono
  per la testata e si mettono dove si vuole, si ridimensionano da bordi e angoli, vicino ai
  bordi della finestra o di un altro pannello si attaccano da soli, un clic porta un
  pannello davanti. ⤢ lo stacca in una finestra sua, ✕ lo chiude, Pannelli lo riapre.
- La disposizione di tutti i giorni e quella della gara sono separate e si ricordano da
  sole, in proporzione alla finestra. "Blocca la disposizione" (tasto destro su una
  testata) ferma spostamenti e misure; "Ripristina la disposizione" rimette quella di
  partenza.
- Un pannello che non ci sta nel suo riquadro resta dentro, invece di coprire i vicini.

## 1.16.10 — 25 settembre 2026

**CRX Logbook resta allineato al log: correzioni e cancellazioni vanno anche li'.**

- Come suggerito da CRX: ogni QSO porta il suo numero DecoDXLog nel campo personalizzato 38,
  e DecoDXLog tiene la mappa numero locale ↔ numero CRX ↔ logbook CRX. Un QSO corretto qui
  torna in coda e parte come modifica dello stesso QSO su CRX; uno cancellato qui viene
  cancellato anche su CRX. Senza rete restano in coda e partono all'invio successivo
  (automatico o dal pulsante nella scheda QSL), che conta anche le cancellazioni.
- Cambiando logbook CRX i QSO gia' mandati all'altro partono come nuovi, non come modifiche.
- Le correzioni di QSO gia' su CRX partono anche se sono piu' vecchi della data "Invia i QSO
  dal".
- La data "Invia i QSO dal" di CRX si scrive nella forma della lingua, come le altre.

## 1.16.9 — 25 settembre 2026

**Le date nella forma della lingua scelta: in italiano giorno/mese/anno.**

- Con l'interfaccia in italiano le date si leggono **25/09/2026** (nel log **25/09/26 12:18**):
  nel log e nelle sue colonne (data, cartoline, LoTW, campi ADIF), nella scheda del QSO,
  nell'inserimento, nella scheda nominativo, nei diplomi, nelle statistiche, nel filtro per
  date, nelle ultime sincronizzazioni. Tedesco, russo, danese, lettone e rumeno con i punti
  (25.09.2026), francese, spagnolo e catalano con le barre, olandese coi trattini; inglese,
  ungherese, giapponese e cinese restano anno-mese-giorno. Nei campi si scrive nella stessa
  forma (anche 5/9/26), o in ISO. Dentro, database e ADIF, non cambia niente.
- Nel cluster della finestra principale le colonne aggiunte a mano (banda, spotter, km ·
  azimut, fonte) adesso si vedono: la scheda stretta le toglieva sempre.

## 1.16.8 — 25 settembre 2026

**Il decoder CW fa vedere quello che sente, come ggmorse, e le colonne del cluster si
allargano e si stringono.**

- Sotto i comandi del decoder c'e' il **grafico di ggmorse**: in arancio il segnale filtrato
  sul tono negli ultimi tre secondi, con la soglia tratteggiata; sopra, in verde, i punti e
  le linee come li sta leggendo. Scorre da destra a sinistra. Sotto, tono, velocita' e costo
  (F · V · C): quando il costo e' basso sta leggendo, quando sale e' solo rumore e le barre
  si sbiadiscono.
- **Le colonne del cluster si allargano e si stringono**, anche in modalita' contest, tirando
  il bordo destro dell'intestazione come nel log; un doppio clic sul bordo riporta la misura
  di sempre, e **Larghezze predefinite** nella finestra delle colonne le rimette tutte. Le
  misure si ricordano, in gara a parte. Se il pannello e' stretto la tabella scorre di lato
  invece di tagliare le ultime colonne. Il chip **colonne** c'e' anche in gara.

## 1.16.7 — 25 settembre 2026

**L'inserimento veloce porta la radio sulla banda e sul modo scelti, e il cluster ha le colonne
a scelta.**

- **Banda e modo nell'inserimento veloce** (e nella finestra Contest) adesso muovono la
  radio: cambiando banda si va sulla frequenza lasciata li' in quel modo, o all'inizio del
  segmento giusto (piano IARU Regione 1: 7.030 CW, 7.150 SSB, 14.074 FT8…); cambiando modo la
  radio cambia modo e va nel segmento del modo, LSB sotto i 10 MHz e USB sopra. La memoria e'
  per banda e modo, come il band stack delle radio. Al contrario, girando la manopola o
  cambiando modo sulla radio, l'inserimento segue.
- **Le colonne del cluster si scelgono e si spostano**, come quelle del log: trascinando
  l'intestazione, o dal chip **colonne** (a sinistra quelle che si vedono con ▲▼ e ✕, a destra
  le altre: entita', km/azimut, fonte, continente, DXCC, locatore, dB, referenza, commento).
  In gara la disposizione e' una a parte, di serie piu' stretta.

## 1.16.6 — 24 settembre 2026

**Connessioni di rete piu' robuste.** Le richieste automatiche per aggiornamenti e servizi
usano HTTP/1.1 quando necessario, e la posta non legge piu' socket TLS gia' chiusi durante
la riconnessione o la chiusura dell'applicazione.

## 1.16.5 — 24 settembre 2026

**Le colonne del log si scelgono fra tutti i campi ADIF e si mettono in ordine**, come nei log
di stazione di sempre (Logger32 e gli altri), nel log normale e in modalita' contest.

- **si spostano trascinando l'intestazione** sopra un'altra: un segno verde dice dove cade;
- la finestra **Colonne** ha a sinistra quelle che si vedono, nell'ordine, con **▲▼** e **✕**,
  e a destra tutte le altre: un clic e si aggiungono in fondo;
- oltre alle colonne di prima, una cinquantina di **campi ADIF**: data, ora inizio e fine,
  prefisso, submodo, banda e frequenza RX, continente, SOTA/POTA/WWFF, programma e referenza,
  note, propagazione, satellite, potenze, stazione, operatore, il mio locatore, contest,
  numeri e scambi inviati e ricevuti, sezione ARRL, Ten-Ten, QSL via e messaggio, indirizzo,
  email, distanza, eta', radio, SFI/K/A, e gli stati QSL di cartolina, LoTW, eQSL, Club Log e
  QRZ;
- in fondo si scrive il nome di **un campo ADIF qualsiasi**, anche quelli di altri programmi
  (per esempio APP_LOGGER32_QSO_NUMBER);
- la disposizione e' **una sola, in gara e fuori**, e le larghezze restano attaccate alla
  colonna quando cambia posto. Chi aggiorna ritrova le colonne di prima, meno quelle che
  aveva nascosto.

## 1.16.4 — 24 settembre 2026

**Il commento dell'ADIF ha la sua colonna nel log.**

- nel log c'e' la colonna **Commento** (il COMMENT dell'ADIF), subito dopo il nome, e la
  **ricerca** guarda anche li'. Chi arriva da Logger32 o da altri programmi cercava il
  commento nella colonna Etichette, che e' un'altra cosa: le etichette sono di DecoDXLog;
- l'**oggetto delle QSL per email** con accenti o faccine si scrive in pezzi da 75 caratteri
  al massimo, come vuole la RFC 2047, senza spezzare una faccina a meta': prima era un pezzo
  solo lungo quanto tutto l'oggetto, che i filtri antispam guardano male.

## 1.16.3 — 24 settembre 2026

**Connettore per CRX Logbook (crx.cloud).** Un servizio in piu' in Impostazioni → Servizi QSL
e nella scheda QSL in basso, accanto a LoTW, QRZ Logbook, Club Log ed eQSL.

- la **chiave API** dell'account (comincia con HAM-) si mette una volta sola, nel portachiavi
  di sistema come le altre credenziali;
- **Carica i miei logbook** mostra i logbook dell'account e si sceglie quello in cui scrivere
  (con uno solo si sceglie da se');
- ogni QSO parte con nominativo, banda, frequenza, modo, rapporti, nome, commento, **data e
  ora**; un QSO corretto dopo l'invio **si aggiorna**, non si duplica;
- si parte **dal giorno in cui si sceglie il logbook**: il log di prima parte solo spostando
  indietro la data, non per sbaglio al primo collegamento;
- invio a mano o automatico dopo ogni QSO, e contatori, come per gli altri servizi.

E' la prima versione, fatta sulla documentazione pubblica dell'API: provata contro un CRX
finto, in attesa della conferma dello sviluppatore di CRX su alcuni dettagli (l'ora del QSO
all'invio, il significato dei due rapporti).

## 1.16.2 — 24 settembre 2026

**Nel log colonne e filtri funzionano ovunque.**

- **Colonne e filtri salvati**: li ricordava solo il log nella sua casella di serie, al centro
  della finestra principale. Sulla lavagna del contest, in un log staccato o spostato in
  un'altra casella, scegliere una colonna o salvare un filtro non faceva niente. Adesso li
  tiene il log stesso, dovunque sia; le scelte di prima restano.
- **Le colonne si tirano dall'intestazione**: fra un'intestazione e l'altra c'e' una riga
  verticale che si vede, e trascinandola la colonna si allarga o si stringe. La misura resta.
- **Barra in alto senza fascia vuota**: cloud e ricerca stanno subito dopo i comandi, e la
  ricerca prende lo spazio che avanza.

## 1.16.1 — 24 settembre 2026

**Stazione dentro Impostazioni, e Pannelli che in gara comanda davvero.**

- **La stazione sta in Impostazioni**: il riquadro Stazione nella barra in alto non c'e' piu'.
  Il pulsante dice con quale profilo si scrive ("Impostazioni · IU8LMC ▾"), e il suo menu apre
  le impostazioni, cambia profilo con un clic (spuntato quello attivo) o apre i profili.
- **Via il pulsante Cluster**: il cluster si apre dal menu del marchio (≡ → DX Cluster…) e con
  Ctrl+K.
- **Pannelli in modalita' contest**: comandava la disposizione di tutti i giorni, che in gara
  e' spenta, e i clic non aprivano e non chiudevano niente. Adesso in gara elenca i pannelli
  della lavagna: un clic li apre o li chiude, ⤢ li stacca, ↩ li riporta, e il conto dei
  chiusi e' quello della lavagna.
- **La CW si comporta come le altre**: staccata dalla lavagna non mostra piu' il pulsante
  "Davanti", che in gara non faceva niente.

## 1.16.0 — 24 settembre 2026

**La modalita' contest diventa una lavagna magnetica.** In gara la finestra principale e' una
lavagna, e i pannelli ci stanno sopra liberi:

- si prendono per la **testata** e si spostano **in qualsiasi punto**, anche sovrapposti; un
  clic sulla testata porta un pannello davanti agli altri;
- si **ridimensionano** da tutti i bordi e da tutti gli angoli;
- vicino al bordo della lavagna o a un altro pannello **si attaccano da soli**, come
  calamite, a filo o con un piccolo spazio: si allineano senza fatica;
- **⤢ stacca** un pannello in una finestra sua (anche su un altro monitor), che resta sopra
  la principale e si riduce con lei; **↩** lo riporta sulla lavagna; **✕** lo chiude, e dal
  menu Contest Mode si riapre;
- posizioni e misure si ricordano **in proporzione**: allargando o stringendo la finestra la
  disposizione la segue. **Ripristina la disposizione** torna alle tre colonne di partenza.

**La ✕ delle finestre torna a chiudere.** Dalla 1.15.3, togliendo la barra di Windows, la
testata trascinabile annullava il clic dei suoi pulsanti appena premuti: la ✕ dei pannelli
staccati, delle finestre di dialogo e delle finestre grandi non rispondeva. Adesso la testata
si trascina lasciando i clic ai pulsanti. Provato con clic veri del mouse.

**Il cluster in gara senza il giallo, e in un colore solo.** Le righe dei moltiplicatori non
hanno piu' il fondo giallo, che in gara copriva tutto l'elenco: il moltiplicatore lo dicono il
filo a sinistra e la pasticca, nel colore d'accento del tema. La pasticca si legge:
**"Z5 · DXCC"** invece di "zona 5 · paese 291". In gara le pasticche di stato (NEW CALL,
WORKED…) non ci sono piu'.

## 1.15.9 — 24 settembre 2026

**I pannelli della gara si spostano.** Nella modalita' contest agganciata la maniglia ⠿ dei
pannelli non faceva niente, e i pannelli sembravano bloccati.

- si prende un pannello per la **maniglia ⠿** e lo si lascia sopra un altro: la casella di
  arrivo si accende con **"qui"** e i due **si scambiano di posto**, come nella finestra di
  tutti i giorni. Le posizioni restano da una gara all'altra; **Ripristina la disposizione**
  le rimette come all'inizio;
- i **bordi fra i pannelli** hanno un segno sempre visibile e si accendono passandoci sopra:
  si trascinano per allargare e stringere. Prima erano trasparenti e non si trovavano.

## 1.15.8 — 24 settembre 2026

**La modalita' contest agganciata, sistemata per chi arriva dalle versioni prima.**

- **Chi aveva chiuso il programma in gara con la 1.15.6 o prima** si ritrovava la finestra
  dell'inserimento ancora staccata sopra i pannelli agganciati, e i pannelli chiusi con la ✕
  del vecchio banco (log, scheda nominativo, mappa…) restavano chiusi anche nella finestra di
  tutti i giorni. Adesso all'avvio in gara le finestre staccate rientrano, e all'uscita
  tornano i pannelli chiusi e staccati di prima della gara.
- **Chiudere l'ultimo pannello della gara non li riapre piu' tutti**: restano chiusi, con la
  scritta che dice come riaprirli dal menu Contest Mode. La CW si apre da sola solo entrando
  in una gara in telegrafia; chiusa, resta chiusa.

## 1.15.7 — 24 settembre 2026

**La modalita' contest agganciata nella finestra principale.** Le finestre separate del banco
davano guai: la ✕ che non chiudeva, le spunte del menu che non rispondevano, finestre che
finivano sotto o sopra le altre. Adesso non ci sono piu' finestre separate:

- i pannelli della gara stanno **agganciati nel corpo del programma**, in tre colonne: il
  cluster a sinistra; in mezzo l'inserimento veloce, il log, la scheda nominativo e la CW; a
  destra punteggio, ritmo e mappa;
- i **bordi fra i pannelli si trascinano** e le misure restano da una gara all'altra; la
  disposizione **segue la finestra** quando la si allarga o la si stringe;
- ogni pannello ha la sua **✕**, e dal menu **Contest Mode** le spunte chiudono e riaprono;
  **Ripristina la disposizione** rimette pannelli e misure come all'inizio;
- all'uscita la finestra di tutti i giorni torna esattamente com'era; in gara i suoi pannelli
  si scaricano e non costano niente;
- l'**inserimento veloce parte nel modo della gara** (CQ-WW-SSB in fonia, con il 59) e non si
  stringe mai sotto quanto serve a vedere lo scambio e Registra, anche su uno schermo piccolo.

**Trovata e corretta la caduta alla chiusura.** Il biglietto della 1.15.4 l'ha presa al primo
colpo: chiudendo DecoDXLog con Decodium collegato, DecoLink salutava il client mentre si
spegneva e scriveva nel registro attivita' che era gia' stato distrutto. E' la chiusura
improvvisa che il registro di Windows segnava dalla 1.7 in poi, sempre nello stesso punto.
Adesso DecoLink si ferma prima, con tutto ancora in piedi.

## 1.15.6 — 24 settembre 2026

**Lo scambio si scrive da solo mentre si batte il nominativo.** Nell'inserimento veloce del
contest (e nella finestra Contest) il campo dello scambio si riempie con quello che la
stazione probabilmente mandera':

- **CQ WW**: la zona CQ, da un QSO gia' fatto con lei o dal paese;
- **IARU HF**: la zona ITU, o la sigla della societa' (DARC, ARI…) se era una stazione HQ in
  una IARU precedente;
- **ARI DX e 40/80**: la provincia delle stazioni italiane, dall'ultima gara o dalla provincia
  salvata nel QSO;
- **Sezioni ARI**: la sezione mandata l'ultima volta;
- **CQ WPX e i contest a progressivo**: niente, il numero ricevuto non si puo' sapere prima.
  Il proprio progressivo resta, come sempre, in "Nr i".

Il nome del campo dice da dove viene ("Zona CQ · paese", "Provincia · log") e il valore
suggerito e' colorato. Con la barra spaziatrice o con un clic su uno spot del cluster si
arriva allo scambio gia' selezionato: **Invio** se va bene, si scrive sopra se no. Quello
scritto a mano non si tocca mai.

## 1.15.5 — 23 settembre 2026

**Contest Mode: tutto in un menu nella barra in alto.** La pulsantiera "Banco del contest" era
una finestra in piu' da spostare, e lo sfondo della modalita' contest ripeteva gli stessi
pulsanti con una scritta in mezzo.

- in alto c'e' una voce sola, **Contest Mode**: mostra la gara e i QSO, ed e' verde quando si
  e' in modalita' contest. Il menu ha la situazione della gara (QSO, punti, moltiplicatori),
  **Contest e attivazioni…**, **entra / esci** dalla modalita' contest, le otto **finestre
  della gara** con la spunta su quelle aperte, le tre **disposizioni**, **Davanti anche agli
  altri programmi**, **Cabrillo…** e **Manda il log…**;
- lo sfondo in modalita' contest resta una **griglia vuota**, tutta per le finestre;
- la finestra delle sessioni ha il pulsante **Contest Mode** al posto di "Banco del contest";
- il titolo di "nessuna sessione" non esce piu' come tre caratteri senza senso.

## 1.15.4 — 23 settembre 2026

**Il cluster che corre non ferma piu' il banco, e lo spot scelto va nell'inserimento.**

- **Clic su uno spot del cluster**: nominativo, banda e modo vanno nell'**inserimento veloce
  del contest** (LSB e USB diventano SSB), la finestra passa davanti e il cursore va sullo
  **scambio**: si chiama, si scrive quello che manda, Invio. Il doppio clic fa lo stesso e
  sintonizza anche la radio, come prima. Vale anche per la finestra Contest singola.
- **Niente piu' scatti con gli spot a raffica**: a ogni spot nuovo la mappa ricostruiva
  l'elenco di tutti gli spot e ridisegnava il mondo intero, 35 ms a spot — in gara, con l'RBN,
  vuol dire il programma fermo per buona parte del tempo. Adesso la mappa ha due livelli: il
  fondo (coste, notte, reticolo, locatori) si ridisegna solo quando cambia, gli spot si
  aggiornano al massimo ogni due secondi. Uno spot costa **1 ms**.
- **Scheda nominativo**: tolto un calcolo che ripartiva piu' volte a ogni nominativo scelto.
- **Se il programma cade, lascia un biglietto.** Nel registro di Windows c'erano chiusure
  improvvise di DecoDXLog, sempre nello stesso punto (mentre si aggiunge un elemento a un
  elenco), ma il registro tiene solo l'ultimo passo e non dice chi ce l'ha portato. Adesso,
  se succede, in `%LOCALAPPDATA%\Decodium\DecoDXLog\crash` resta un file `crash-….txt`
  con la strada intera e cosa stava facendo il programma: basta mandarlo per trovare la
  causa.

## 1.15.3 — 23 settembre 2026

**Le finestre senza la barra di Windows: il titolo sta nella testata.** Sopra ogni finestra
staccata e ogni finestra di dialogo c'erano due barre — quella di Windows e la testata del
pannello — con lo stesso titolo. Spazio perso, soprattutto in gara con tante finestre aperte.

- **finestre staccate e del banco contest**: resta solo la testata del pannello. Si
  **spostano dalla testata** (portandole al bordo dello schermo Windows le aggancia), il
  **doppio clic** le ingrandisce o le rimette com'erano, dai **bordi e dagli angoli** si
  ridimensionano; il margine attorno al pannello scende da 8 a 3 pixel, e un filo colorato
  segna la finestra attiva;
- **finestre di dialogo** (Impostazioni, Log, Contest, Informazioni…): lo stesso, con la
  testata e la ✕;
- **finestre grandi** (cluster, contest, log, QSL cartacee, statistiche, rotore): una testata
  bassa con il titolo, senza "DecoDXLog —" davanti, e i comandi riduci, ingrandisci, chiudi;
- **scritte piu' corte**: nel cluster "1718/3000 spot" al posto della frase intera;
  l'inserimento contest senza sessione dice "Inserimento contest · nessuna sessione".

## 1.15.2 — 23 settembre 2026

**Le finestre di Windows e le tendine non finiscono piu' sotto il banco.** In modalita' contest
le finestre della gara erano "sempre in primo piano" per tutto il sistema: coprivano le
finestre di dialogo di DecoDXLog (Impostazioni, Log, Contest…), quelle di Windows (apri,
salva, avvisi) e le tendine.

- le finestre della gara stanno **sopra la finestra principale perche' sono sue** — si
  riducono a icona con lei — ma non piu' sopra a tutto: le finestre di dialogo di DecoDXLog e
  di Windows si aprono davanti;
- nella pulsantiera del banco c'e' **Davanti anche agli altri programmi**, spento di
  partenza, per chi vuole le finestre della gara sopra Decodium o altri programmi;
- **le tendine** si aprono in una finestra loro: stanno sopra tutto ed escono dal bordo delle
  finestre piccole invece di restare tagliate; lo stesso per il menu **Pannelli**.

## 1.15.1 — 23 settembre 2026

**La barra in alto va a capo sugli schermi piccoli.** Su uno schermo stretto i riquadri della
barra uscivano dalla finestra: cloud e ricerca sparivano oltre il bordo.

- i riquadri della barra **scendono sulla riga sotto** quando non c'e' posto, e la barra si
  alza da sola; su uno schermo largo resta tutto su una riga, con cloud e ricerca a destra;
- i **pulsanti dei comandi** vanno a capo anche dentro il loro riquadro, che non esce mai
  dalla finestra;
- il pulsante del cloud e' diventato l'icona **⟳** (la spiegazione compare passandoci sopra),
  e la scelta della stazione e' un po' piu' stretta;
- la finestra principale **non chiede piu' 1100 pixel** quando lo schermo e' piu' stretto.

## 1.15.0 — 23 settembre 2026

**La modalita' contest vera: la finestra principale diventa la base del banco.** Aprendo il
banco la finestra principale restava quella di tutti i giorni — il pannello del nuovo QSO, il
log, le schede di diplomi, statistiche e propagazione — e le finestre della gara stavano sparse
per lo schermo.

Adesso entrare in modalita' contest cambia la finestra principale:

- al posto della disposizione di tutti i giorni c'e' la **base della gara**: il nome del
  contest, QSO, punti, moltiplicatori e punteggio, e tre pulsanti — **Riapri il banco**,
  **Disponi le finestre**, **Esci dalla modalita' contest**;
- la finestra principale **si massimizza** e le finestre della gara si dispongono **sopra di
  lei, raggruppate**, invece che su tutto lo schermo;
- le finestre della gara sono **legate alla principale**: stanno sempre davanti, si riducono
  con lei e non riempiono la barra delle applicazioni. Restano libere: si spostano e si
  ridimensionano dove si vuole, e li' restano.

Si esce dalla pulsantiera, dalla base, o chiudendo la sessione, e la finestra principale torna
**esattamente com'era**: pannelli, finestre staccate, misura. Il programma chiuso in gara si
riapre in gara.

## 1.14.1 — 23 settembre 2026

**Un QSO in gara non ferma piu' il programma.** Registrare un QSO teneva il programma fermo
per secondi — quattro, su una stazione vera — proprio nel momento in cui la stazione dall'altra
parte aspetta. Adesso un QSO si registra in circa **70 millisecondi**.

Misurato su un log da stazione vera (15.400 QSO, una gara in corso da 400), prima e dopo:
**2,4 secondi per QSO prima, 70 millisecondi dopo**, e il blocco piu' lungo fra un QSO e
l'altro sotto i 115 ms.

Non era una cosa sola. Il diploma FT2 si ricalcolava 22 volte per ogni QSO, una per ogni
riquadro che lo mostra; le statistiche per banda 28 volte; il punteggio del contest 5 volte,
ogni volta rileggendo i QSO della gara uno per uno; e il riepilogo dei diplomi anche con la
finestra dei diplomi chiusa. Adesso:

- il punteggio della gara si calcola **una volta per QSO**, con una sola lettura del log, e lo
  leggono tutte le finestre — cluster, punteggio, inserimento, pulsantiera — dallo stesso conto;
- diplomi e statistiche di tutto il log si aggiornano **un attimo dopo**, quando si smette di
  scrivere, e **in un thread a parte**: la finestra non si ferma mai;
- quello che non si vede non si calcola piu'.

## 1.14.0 — 23 settembre 2026

**Il pieno controllo del banco: pulsantiera, tre disposizioni, finestre che restano davanti.**

Mancavano tre cose per lavorarci davvero in gara.

**Le finestre non spariscono piu'.** Sotto quella grande, o ridotte a icona: una finestra
sparita mentre passa la stazione e' un QSO perso. In contest stanno davanti e il pulsante per
ridurle non c'e' proprio, cosi' non ci si casca. Si spegne dall'interruttore della pulsantiera,
per chi preferisce.

**La pulsantiera.** E' una finestra piccola come le altre, e da li' si comanda tutto: si
accendono e si spengono le otto finestre una per una, si sceglie la disposizione, e si arriva
al Cabrillo e all'invio del log. In fondo si legge a che punto sta la gara, cosi' anche con
tutto il resto chiuso si sa come va.

**Le disposizioni sono tre**, e si cambiano con un pulsante: **Colonne** (cluster, lavoro,
conti — quella che regge uno schermo solo), **Al centro** (tutto attorno all'inserimento) e
**Due schermi**, che se di schermo ce n'e' uno solo ricade sulle colonne invece di mandare
meta' banco nel nulla. La scelta si ricorda, e le finestre spostate a mano restano dove sono.

**Il cluster in gara era ancora troppo pieno.** Adesso la fila dei filtri si apre solo quando
serve, e c'e' «solo molt», che nasconde tutto quello che non porta un moltiplicatore: in una
colonna stretta e' la differenza fra guardare e cercare.

**E il log si manda.** Ogni contest con la scheda sa dove va — CQ WW e WPX al loro logcheck,
IARU al submission ARRL, i tre ARI a contest.ari.it — ed entro quanti giorni. Il programma
scrive il Cabrillo con il punteggio contato e apre la pagina. A caricarlo e' l'operatore: un
log spedito per sbaglio non si richiama indietro.

## 1.13.0 — 23 settembre 2026

**Il banco del contest e' fatto di finestre vere, ognuna dove si vuole.** La finestra unica
metteva tutto insieme e non si poteva spostare niente: in gara serve il contrario, ogni cosa
in una finestra sua, grande quanto si vuole e dove si vuole — anche su un altro monitor.

Il banco apre otto finestre, che sono le stesse dei pannelli staccati, quindi **misura e
posizione si ricordano**: la volta dopo si riaprono dove le si e' lasciate.

| Finestra | Cosa c'e' dentro |
| --- | --- |
| Inserimento contest | nominativo grande, RST, progressivo, scambio col nome del contest, Invio registra |
| Cluster in colonna | verticale, filtri e spot, con i moltiplicatori che mancano in evidenza |
| Log | il registro della stazione |
| Scheda nominativo | quello che il log e il callbook sanno di chi si sta lavorando |
| Come va | QSO, nominativi, durata, ultimi 10 minuti, QSO/h, ultima ora, ultimi collegati |
| Punteggio | punti, moltiplicatori, totale, e la lista banda per banda |
| Mappa | i QSO con locatore |
| CW | solo quando il contest e' in telegrafia |

**La prima volta si dispongono da sole** — cluster in colonna a sinistra, inserimento in mezzo
in alto, log sotto, punteggio e ritmo a destra, mappa e CW in basso — e da li' in poi comanda
chi le sposta: le posizioni scelte non si toccano piu'.

Tre pannelli sono nuovi (inserimento, punteggio, ritmo) e il **cluster e' diventato un pannello
come gli altri**, quindi si stacca in finestra e in colonna stretta tiene solo le colonne che
servono.

Quando la finestra dell'inserimento e' stretta **i campi vanno a capo** invece di schiacciare il
nominativo fino a farlo sparire: in una finestra che si ridimensiona a piacere «stretta»
succede, e succedeva. I quattro pannelli del contest vivono solo in finestra: la ✕ li chiude
invece di riagganciarli, perche' nella disposizione della finestra principale non hanno un
posto e riagganciarli voleva dire perderli.

Nel dialogo Contest ci sono due pulsanti: **Banco del contest** (le finestre) e **Finestra
unica** (quella di prima, che tiene Cabrillo ed export — dal pannello del punteggio ci si arriva
con «Esporta…»).

## 1.12.0 — 22 settembre 2026

**Il banco del contest si apre da solo.** Aprire una sessione di contest voleva dire aprire a
mano la finestra del contest, poi quella del cluster, poi quella CW, e spostarle tutte. Adesso
partono insieme: l'**inserimento veloce** con progressivo, punteggio e statistiche; il
**cluster**; e la **finestra CW** quando il contest e' in telegrafia — lo si capisce dal modo
della sessione o dall'identificativo che finisce in -CW. La finestra dove si scrive resta
davanti.

**Il cluster durante una gara e' un'altra cosa.** Niente schede di fonti, avvisi, voce e
console: solo i filtri e gli spot. E gli spot che portano **un moltiplicatore che ancora non si
ha** si vedono da lontano: riga colorata, barra piu' spessa e la targhetta che dice quale —
«zona 5», «paese 291», «prov RM».

Un secondo americano sulla stessa banda smette di essere segnato, perche' quella zona e quel
paese ci sono gia'; sui 40 metri torna a essere un moltiplicatore, che e' il motivo per cui si
cambia banda. Quello che uno spot vale lo dice la scheda del contest — punti, moltiplicatore
nuovo, duplicato — calcolato con il cty.csv invece che con lo scambio, che prima di lavorare
qualcuno non si sa. Per i contest senza scheda non si segna niente: meglio niente che un numero
inventato.

## 1.11.0 — 22 settembre 2026

**La sezione si chiama Contest, i contest sono tutti quelli che esistono, e sei di loro sanno
le proprie regole.**

Il pulsante diceva «Attivazione» e il contest era un campo dove scrivere a memoria un
CONTEST_ID: se lo si sbagliava non lo diceva nessuno, lo scopriva chi riceveva il log.
Adesso l'elenco c'e': i **242 contest dell'enumerazione Contest_ID di ADIF 3.1.5**, generati
dalla specifica e non scritti a mano, piu' gli otto contest della ARI che la tabella ADIF non
conosce. Si cercano per nome o per identificativo, e accanto al campo si legge il nome di
quello scelto.

**Sei contest hanno la scheda delle regole**, e ogni numero viene dal regolamento, citato nel
codice accanto al punto da cui esce:

| Contest | Scambio | Punti | Moltiplicatori |
| --- | --- | --- | --- |
| CQ WW (SSB, CW, RTTY) | zona CQ | 0 stesso paese, 1 stesso continente (2 fra nordamericani), 3 altro continente | zone e paesi, per banda |
| CQ WPX (SSB, CW, RTTY) | progressivo | 3 e 1 sulle bande alte, il doppio su 40, 80 e 160; il proprio paese 1 | prefissi, una volta sola |
| IARU HF | zona ITU o sigla HQ | 1 stessa zona o HQ, 3 stesso continente, 5 tutto diverso | zone e HQ, per banda |
| ARI DX | provincia o progressivo | 10 a una stazione italiana, 3 altro continente, 1 stesso continente | province e paesi, per banda |
| ARI Contest delle Sezioni | codice ASC | per banda: 40m 1, 80m e 20m 2, 160m e 15m 3, 10m 4 | ASC, per banda e per modo |
| ARI Contest 40/80 | provincia | per modo: CW 3, RTTY 2, SSB 1 | province, per banda e per modo |

Nella finestra contest ci sono **punti, moltiplicatori e punteggio** che salgono a ogni QSO, e
sotto la riga **banda per banda**: quanti QSO, quanti punti e quanti moltiplicatori su ognuna,
perche' nei contest i moltiplicatori si contano per banda e sapere dove mancano e' quello che
dice dove andare.

**Il campo dello scambio prende il nome del contest** — Zona CQ, Provincia, Sezione ARI — e
avvisa quando quello che si e' scritto non ha la forma che il regolamento vuole: una zona CQ
non e' 55, un codice ASC non e' «L1». Il QSO si registra lo stesso, perche' la stazione e' gia'
passata, ma lo si corregge adesso invece che a gara finita.

Un contest senza scheda non ha punteggio, e il programma lo dice invece di dare un numero che
non vuol dire niente: gli altri 244 restano buoni per il log e per il Cabrillo.

**Piu' log per la stessa stazione: uno per tutti i giorni, uno per ogni contest.** I duplicati,
il punteggio e il Cabrillo si contano su un log solo, e a gara finita i QSO della gara non
devono mescolarsi con quelli di sempre. Il pulsante **Log** apre l'elenco: si crea un log nuovo
dandogli un nome, si aggiunge uno che sta gia' sul disco, si apre quello che serve. Chi vuole
se lo fa chiedere all'avvio; di serie no, perche' chi ha un log solo non deve rispondere a
niente.

Aprire un altro log riavvia il programma su quel file: e' l'unica cosa che non lascia in giro
mezzo programma legato al log di prima — radio, cluster, sessione e finestre staccate ripartono
insieme. Un log nuovo si crea davvero subito, con le sue tabelle: se non si apre e' meglio
scoprirlo prima della gara. Dimenticare un log non cancella niente dal disco.

## 1.10.0 — 22 settembre 2026

**Due diplomi italiani fra quelli che DecoDXLog calcola: il WAIP e il DCI.**

Il **WAIP** conta le 110 province italiane. La sigla arriva dal campo STATE dei QSO con
l'Italia e con la Sardegna — due entita' DXCC ma un paese solo, e il diploma conta le
province di tutte e due — e si legge come la scrivono i log: «NA», «I-NA», «NA Napoli».
Carbonia-Iglesias, che dal 2016 non esiste piu', finisce in Sud Sardegna invece di restare
senza provincia; Ogliastra, Olbia-Tempio e Medio Campidano restano voci a se', perche' il
WAIP continua a contarle. Il traguardo e' 75, il piu' alto dei due del regolamento (60 per
chi non trasmette dall'Italia), cosi' la barra non da' per preso un diploma che ancora non
lo e'.

Il **DCI**, i castelli d'Italia, segue il regolamento della Sezione A.R.I. di Mondovi'
aggiornato al 15 settembre 2025: **30 castelli in almeno 5 regioni diverse, e uno della
provincia di Cuneo** (20 per le stazioni fuori d'Italia). Sotto il titolo si legge a che
punto si e' — quanti castelli, in quante regioni, e se Cuneo c'e' — perche' le regioni sono
l'unica cosa che guardando il numero non si poteva sapere.

Contano i QSO **dal 1 gennaio 2001, dai 160 ai 2 metri, in SSB, CW o digitale**: un castello
lavorato in FM non vale, e dirlo qui costa meno che vederselo scartare da chi rilascia il
diploma. C'e' anche il **DCPC**, i castelli della sola provincia di Cuneo: dieci, e si chiede
dopo aver preso il DCI.

**Il riferimento del castello si scrive, non solo si importa.** SIG e SIG_INFO — il programma
di un QSO e il suo riferimento — sono entrati nel log e nella scheda del QSO, e il nuovo QSO
ha un campo DCI che li riempie da solo: si scrive «na 015» e diventa «NA015», come vuole il
regolamento. Nei log scritti a mano il riferimento finisce quasi sempre nel commento, e
anche li' viene trovato — ma con la parola DCI davanti, se no un «TNX 001» qualunque
diventerebbe un castello.

## 1.9.0 — 22 settembre 2026

**La QSL per email puo' partire dal Cloud, e cosi' nessuna password di posta resta sul
computer.** Fino alla 1.8.0 per mandare una cartolina bisognava dare a DecoDXLog l'indirizzo
e la password di una casella. Adesso c'e' una seconda via, ed e' quella di serie: la
cartolina va al Cloud di DecoDXLog con il token che si ha gia', e la imbuca il server. La
password sta su una macchina sola, non su ogni computer che scarica il programma.

L'email parte come «IU8LMC via DecoDXLog», e il Rispondi-a e' l'indirizzo dell'operatore:
chi riceve la cartolina risponde a chi gliel'ha mandata, non al servizio. In fondo al
messaggio si legge da chi viene — una QSL anonima non serve a niente.

**C'e' un tetto di cartoline al giorno**, per nominativo e in tutto. Non e' un capriccio:
una casella di tutti che manda a sconosciuti in giro per il mondo e' esattamente quello che
i filtri antispam guardano, e se una persona sola potesse mandarne diecimila il dominio
finirebbe in lista nera per tutti insieme. Quando il tetto si tocca si dice, e si riprende
il giorno dopo.

**Si sceglie in Impostazioni → Servizi QSL**, alla voce «Chi imbuca la cartolina»: «Il
Cloud, a nome mio» oppure «La mia casella». Con il Cloud i campi del server di posta e la
password spariscono, perche' non servono — e non si chiede al portachiavi qualcosa che non
si usa. Chi preferisce la propria casella non perde niente: la via di prima e' tutta li'.

Il Cloud accetta solo cartoline vere (un PNG, non un file qualunque), rifiuta gli indirizzi
con dentro un a capo — quelli servono solo a infilare intestazioni di nascosto — e non fa
da ponte per un account non ancora approvato.

## 1.8.0 — 22 settembre 2026

**La QSL si manda per email, e l'indirizzo lo sa il callbook.** Oltre al PDF e ai PNG c'e'
un terzo pulsante: «Manda per email». Per ogni QSO scelto DecoDXLog chiede l'indirizzo a
QRZ.com o HamQTH — quello che gia' usa per nome, QTH e locatore — disegna la cartolina di
quel collegamento e la manda come allegato PNG.

Le stazioni di cui il callbook non ha l'email si saltano, una riga per ognuna nel registro:
meglio saperlo che credere di aver mandato cinquanta cartoline e averne mandate trenta.
Una QSL partita si segna come mandata nella coda, con la via elettronica, cosi' non si
rimanda due volte la stessa.

**Il client SMTP e' scritto qui dentro.** Qt non ne ha uno, e per una funzione sola non
vale la pena tirarsi dietro una libreria: il protocollo e' cinque comandi in fila. Una
email per volta, in coda — mandarne cinquanta non apre cinquanta connessioni — e quello che
va storto si legge, invece di sparire.

**La posta esce solo cifrata.** Sulla 465 dal primo byte, sulla 587 con STARTTLS; un server
che non offre ne' l'una ne' l'altra viene rifiutato, e un certificato che non convince pure.
Di li' passa la password della casella.

Si prepara in **Impostazioni → Servizi QSL**: server, porta, il proprio nome, l'oggetto e
il testo — con {CALL} {NAME} {DATE} {TIME} {BAND} {MODE} {RST} e {MYCALL} {MYNAME} — e
l'indirizzo con la password, che va nel portachiavi di sistema come tutte le altre. Con
Gmail ci vuole una password per le app, non quella con cui si entra.

Niente parte senza chiederlo: il pulsante apre una finestra che dice da quale casella, a
quanti QSO, e aspetta un «Manda».

## 1.7.4 — 22 settembre 2026

Quattro cose segnalate in stazione, tutte vere.

**Lo spot del cluster adesso prepara il QSO.** Doppio clic su una stazione e il riquadro
del QSO nuovo si riempie: nominativo, banda, modo, frequenza. Prima restava vuoto, e
siccome il callbook riempie nome, QTH e locatore solo quando il nominativo nel riquadro e'
quello cercato, di QRZ non arrivava niente. Adesso arriva.

**Il display in cima segue la radio.** Mostrava la frequenza e il modo che diceva
Decodium, e quelli vincevano sempre: con Decodium aperto su FT8, cliccare uno spot in CW
portava la radio in CW mentre in cima restava scritto FT8 a 14.074. Adesso quando la radio
e' collegata comanda lei, che e' lo stato vero. Il nome di Decodium si tiene solo quando
dice la stessa cosa in modo piu' preciso — «FT8» invece di «PKTUSB».

**La scheda del nominativo non si lascia piu' schiacciare.** Nella colonna di destra gli
altri pannelli, con le loro misure, la riducevano a una riga: si vedeva il nominativo e
basta. Adesso tiene almeno duecento punti, e sotto c'e' tutto il resto.

**E si vede quando un pannello ha altro sotto.** La barra di scorrimento di Qt sbiadisce
appena si smette di toccarla: un pannello basso sembrava che avesse mangiato la parte di
sotto — nell'invio QSL sparivano eQSL e i tre pulsanti — e non c'era modo di accorgersi
che bastava scorrere. Adesso la barra resta li' finche' c'e' qualcosa da scorrere, nella
scheda del nominativo, nel QSO nuovo, nel CW, nelle sei schede in basso e nelle
Impostazioni.

## 1.7.3 — 22 settembre 2026

**Il doppio clic su uno spot del cluster adesso muove la radio.** Prima parlava solo a
Decodium: il log si spostava di banda, ma il VFO restava dov'era. E se Decodium non era
aperto — cioe' sempre, per chi lavora in CW o in fonia — il doppio clic non faceva
proprio niente, con un avviso che diceva che Decodium non c'era.

Adesso la radio ci va: frequenza e modo dello spot, per via del CAT. Decodium lo riceve
lo stesso quando c'e', e la riga nel registro dice dove e' finito il comando — «radio e
Decodium», «radio», «Decodium», o che non c'era nessuno dei due.

**Per i modi digitali il VFO va sulla sotto-banda, non sulla frequenza dello spot.** Uno
spot FT8 a 18101.5 kHz porta la radio su 18100.0 in PKTUSB: il DX sta a 1500 Hz
nell'audio, e una radio messa su 18101.5 non lo sentirebbe.

**E la fonia sceglie la banda laterale giusta.** Sotto i 10 MHz si parla in LSB, sopra in
USB: lo sanno tutti in aria, ma il programma no — uno spot in fonia sui 40 metri portava
la radio in USB. Chi scrive USB o LSB di suo non viene toccato.

Provato con un rigctld finto che scrive quello che riceve: CW a 14025.1 → `+F 14025100`
`+M CW`; FT8 a 18101.5 → `+F 18100000` `+M PKTUSB`; SSB a 14205 → `+M USB`; SSB a 7125 →
`+M LSB`.

## 1.7.2 — 22 settembre 2026 (pacchetti rifatti)

**I pacchetti della 1.7.1 e della 1.7.2 non partivano.** Li costruivano i flussi di
GitHub Actions con MSVC e il Qt ufficiale, e quello che ne usciva si avviava e moriva
subito: `0xC0000409`, stack buffer overrun. Provato tre volte di fila, sempre uguale. Lo
stesso identico codice compilato con MSYS2/MinGW parte e funziona — quindi non era il
programma, era come veniva messo insieme il pacchetto.

**E si faceva strada da solo.** L'aggiornamento automatico cerca fra gli allegati quello
che finisce per `-setup.exe`, e il pacchetto rotto si chiamava proprio cosi': chi diceva
di si' si ritrovava un DecoDXLog che non si apriva piu'.

I tre flussi non partono piu' a ogni tag — restano, si lanciano a mano, e chi li lancia
prova quello che ne esce. Il pacchetto per Windows torna a farlo `scripts/installer.sh`
con MSYS2, come fino alla 1.7.0. Gli allegati della 1.7.2 sono stati rifatti e provati:
scompattato, installato, aperto. Quelli della 1.7.1 sono stati tolti — i binari stanno
nella 1.7.2, che ha tutto.

Il codice della 1.7.2 non e' cambiato: i satelliti a mano, il portachiavi, i pannelli e il
rotore sono quelli di prima.

## 1.7.2 — 21 settembre 2026

Quattro cose, tutte di elisir80.

**I satelliti si scrivono a mano.** Nella scheda del QSO e nel pannello rapido ci sono
`SAT_NAME` e `SAT_MODE`, per chi lavora via satellite e non ha un programma che glieli
manda. Lo schema del database passa alla versione 4; un log gia' fatto ci arriva da solo
alla prima apertura, e la colonna nuova si aggiunge senza toccare i QSO.

**Il portachiavi non chiede la password all'avvio.** Prima DecoDXLog andava a leggere i
segreti appena partito, e su Linux quello vuol dire la finestra del portachiavi in faccia
prima ancora di vedere il log. Adesso li chiede quando servono davvero, e quello che ha
gia' letto se lo tiene per il resto della sessione invece di ridomandarlo ogni volta.

**«Riporta i pannelli com'erano» li riporta davvero com'erano**: non solo quali sono
aperti, ma anche le misure delle colonne, l'altezza della fascia di sotto e il blocco della
disposizione.

**Il rotore staccato si comporta come gli altri.** Chiudere la sua finestra lo riattacca
invece di lasciarlo sparito, e il pannello resta nella disposizione anche quando un rotore
non c'e' — prima la casella spariva e gli altri si spostavano sotto le mani.

## 1.7.1 — 21 settembre 2026

**Le versioni per Linux e macOS si costruiscono da sole.** Tre flussi di GitHub Actions —
`release-linux.yml`, `release-macos.yml`, `release-windows.yml` — preparano l'AppImage, il
pacchetto per il Mac e quello per Windows a ogni tag, con il Qt ufficiale invece di quello
che capita sulla macchina. Il lavoro e' di elisir80, che tiene le build fuori da Windows.

Dentro il programma non cambia niente di quello che si vede: un `#include` che mancava e
che su Windows passava lo stesso (`QJsonDocument` in `DecoLogController.cpp`), e il numero
di versione scritto anche nel pacchetto del Mac, che altrimenti nel Finder resta a zero.

## 1.7.0 — 21 settembre 2026

**«Gli otto di serie»: i campi che una QSL ha sempre, gia' nei loro riquadri.** Un
pulsante, e sulla cartolina compaiono nominativo, giorno, mese, anno, UTC, MHz, modo e RST,
ognuno in mezzo alla sua casella. Prima bisognava aggiungerli uno per uno dal menu e poi
trascinarli, otto volte.

Le posizioni non sono a occhio. Sono misurate sulla cartolina vera: si trovano le righe e
le colonne della tabella guardando dove i pixel sono scuri, si prende il centro di ogni
casella vuota, e da li' escono le frazioni. La fascia del nominativo comincia dove finisce
la scritta «Confirming QSO/SWL to:», che pure si misura invece di indovinarla.

Il pulsante non sdoppia niente: un campo gia' posato si sposta al suo posto invece di
comparire due volte, e quello che si e' messo a mano e non e' fra gli otto — un testo
libero, il nome, il locatore — resta dov'e'. La prova automatica lo verifica, e verifica
anche che tutti e otto cadano dentro i riquadri e non a un pelo fuori.

Su una cartolina fatta in un altro modo finiscono nel posto sbagliato: allora si
trascinano, che e' il mestiere di questo pannello. Il fumetto del pulsante lo dice.

## 1.6.1 — 21 settembre 2026

**Nel menu «Aggiungi un campo» il testo non si leggeva.** Le voci — Nominativo, Data,
Giorno, e tutte le altre — uscivano grigio scuro su fondo scuro, come se fossero spente.

Era una voce di menu di Qt lasciata com'e': quella, sul tema scuro, tiene i colori del
sistema. Tutti gli altri menu del programma usano StyledMenuItem, che i colori li prende
dal tema; quello della cartolina se n'era dimenticato — ed era l'unico in tutto l'albero.
Adesso e' come gli altri: carattere a spaziatura fissa, testo chiaro, la voce sotto il
puntatore si accende.

## 1.6.0 — 21 settembre 2026

**La cartolina QSL torna, vestita come il resto del programma.** Nella 1.5.1 era stata
tolta per un malinteso: il problema non era la funzione, era che il pannello non somigliava
a niente altro dentro DecoDXLog — una striscia di controlli nudi in mezzo a una finestra
fatta di pannelli di vetro.

Adesso sono tre pannelli come tutti gli altri, con la testata, il pallino e le etichette
sopra i controlli:

- **La cartolina**, che si prende tutto lo spazio che avanza. Il modello si carica dalla
  sua testata, dove stanno anche il nome del file e la misura in pixel; senza modello il
  riquadro non finge niente, dice cosa manca e offre il pulsante per caricarlo.
- **Il campo**, che cambia titolo secondo quello che si e' scelto — «Campo · Nominativo» —
  e tiene corpo, aggancio, colore e grassetto ognuno sotto la sua etichetta. Il colore in
  uso si riconosce dal bordo.
- **Stampare**, con le cartoline per foglio e i due pulsanti.

La cartolina vera sta in mezzo al suo pannello con un filo di bordo attorno: porta i propri
colori, come una foto appoggiata sul tavolo, e non si mischia col tema scuro.

Di sotto non e' cambiato niente: gli stessi ventuno campi, le stesse posizioni in frazione
del modello, lo stesso PDF e gli stessi PNG.

## 1.5.1 — 21 settembre 2026

**Via il laboratorio delle QSL cartacee.** La 1.5.0 aveva aggiunto un pannello per
disegnare la propria cartolina QSL — caricare l'immagine, posarci sopra i campi, stamparla.
E' durata poche ore, per un malinteso: il guaio era il vestito, non la funzione, e nella
1.6.0 e' tornata. La riga resta qui perche' la 1.5.1 e' uscita davvero, e chi l'ha
installata deve poter capire cosa gli era successo.

## 1.5.0 — 21 settembre 2026

**La cartolina QSL si stampa da qui.** Fino a ieri delle QSL di carta DecoDXLog sapeva
fare solo le etichette adesive. Adesso c'e' la cartolina vera: si carica l'immagine della
propria QSL — la scansione, o il file che ha dato la tipografia — e ci si posano sopra i
campi, trascinandoli col mouse dove stanno i riquadri vuoti.

I campi sono ventuno: nominativo, data intera o spezzata in giorno, mese (in cifre o
JAN/FEB/MAR) e anno, ora UTC, MHz, banda, modo, RST, nome, QTH, paese, locatore, il via, i
propri dati dal profilo di stazione, e il testo libero per scriverci quello che si vuole.
Di ognuno si sceglie il corpo, il grassetto, il colore e se si aggancia a sinistra, in
mezzo o a destra del punto dove sta.

**Quello che si vede e' quello che si stampa.** L'anteprima non e' un disegno a parte: e'
la stessa cartolina, con dentro un QSO vero preso dalla coda. Le posizioni non sono in
pixel ma in frazione del modello, e i corpi in millesimi della sua altezza: cambiare la
cartolina con una scansione piu' grande non sposta piu' niente.

In uscita, due strade: **un PDF** con una, due o quattro cartoline per foglio A4 (dentro
la casella la cartolina tiene le sue proporzioni — una QSL stirata non la manda nessuno),
oppure **un PNG per ogni QSO**, alla misura del modello, per chi le manda a stampare
altrove. Si stampano quelle scelte nella coda, o tutta la coda.

La cartolina si prepara una volta e resta: com'e' fatta sta nelle impostazioni, quindi
segue il resto nella copia di sicurezza.

Cosa non fa, per ora: una faccia sola (il davanti), una riga per campo, e niente testo
ruotato.

La si apre da **Invio QSL → Cartolina QSL**, o dal pulsante in cima alla finestra delle
QSL di carta.

## 1.4.0 — 21 settembre 2026

**Il decoder CW adesso e' ggmorse.** Quello di Georgi Gerganov
(github.com/ggerganov/ggmorse, licenza MIT), copiato dentro `libs/ggmorse`: otto file e
nessuna dipendenza. Il decoder di prima era roba nostra — un filtro stretto sul tono e i
tempi misurati a occhio — e leggeva, ma solo se il segnale era pulito e la velocita' non
cambiava.

Cosa cambia, misurato:

- **la velocita' e' giusta**, non a spanne: a 15, 20, 25 e 35 parole al minuto legge 15,
  20, 25 e 35. Prima si accettava un terzo di errore;
- **il tono lo trova entro pochi hertz** (550 Hz letti 547), e cercandolo fra 200 e 1200,
  non piu' fra 400 e 1000;
- **legge anche sotto il rumore**: con rumore di ampiezza doppia rispetto al segnale il
  testo esce ancora tutto.

**E sulla banda vuota non scrive niente.** ggmorse, lasciato a se', su mezzo minuto di
solo fruscio tira fuori una cinquantina di lettere inventate — il rumore a tratti somiglia
al Morse. DecoDXLog guarda quanto i tempi misurati somigliano davvero al Morse (il costo
che ggmorse calcola da solo: sotto 0.01 con un segnale, intorno a 1 col solo rumore) e
lascia passare solo quello che sta sotto 0.2. Su trenta secondi di rumore, a qualunque
livello, non esce piu' una lettera.

Una cosa e' diversa da prima: **tre lettere sole, da fermo, non bastano**. ggmorse misura i
tempi su una finestra di tre secondi, e finche' non ha agganciato la velocita' le prime
lettere possono restare indietro. In aria non cambia niente — nessuno manda tre caratteri
e chiude — ma vale la pena dirlo.

**La finestra del CW sganciata sta davanti alle altre.** Mentre si manipola si guarda
quello che esce dal decoder e si scrive nel log: la finestra del CW non deve finire dietro
a quella grande. Nella sua testata c'e' il pulsante «Davanti» per toglierlo, e resta tolto.

**Le Impostazioni scorrono.** Una pagina lunga — Servizi QSL, Radio — spingeva «Annulla» e
«Applica» fuori dalla finestra, e la meta' di sotto non si raggiungeva in nessun modo.
Adesso la pagina ha la sua barra di scorrimento, i due pulsanti restano incollati in
fondo, e su uno schermo largo il contenuto si ferma a 920 punti invece di allungarsi da un
bordo all'altro.

## 1.3.0 — 21 settembre 2026

**Nella scheda del nominativo c'e' la griglia banda per modo.** Quella che chi caccia il
DX si tiene appesa al muro: una riga per CW, digitale e fonia, una colonna per banda —
160, 80, 40, 30, 20, 17, 15, 12, 10, 6, 2 e 70 centimetri, piu' le bande fuori elenco che
il log ha davvero usato. La casella vuota vuol dire mai lavorato; quella arancione,
lavorato e basta; quella verde, confermato.

**Dentro la casella verde ci sono le lettere di chi ha confermato**: `L` LoTW, `e` eQSL,
`C` Club Log, `Q` QRZ, `K` la cartolina in mano. Passandoci sopra il puntatore, il fumetto
dice la banda, quanti QSO ci sono dentro e da dove arriva la conferma.

**Le griglie sono due.** La prima e' la stazione che si sta chiamando; la seconda e' la
sua entita' DXCC, cioe' tutto quello che si e' lavorato in quel Paese, da qualunque
nominativo. La prima dice se quel nominativo lo si conosce gia'; la seconda dice se quel
Paese serve ancora, e su quale banda. Se il nominativo non e' mai stato lavorato resta
solo la seconda.

Il criterio con cui un modo finisce in CW, fonia o digitale e' lo stesso dei diplomi:
quello che non e' manipolatore ne' voce sta fra i digitali — FT8, FT4, FT2, RTTY, PSK e
tutti gli altri.

Tutto arriva da una sola interrogazione al database, raggruppata li' dentro: la scheda si
riempie anche con un log grosso.

## 1.2.0 — 21 settembre 2026

**DecoDXLog si accorge da solo quando esce una versione nuova.** Una volta al giorno
chiede a GitHub se c'e' qualcosa di piu' recente — in silenzio, senza aprire finestre.
Solo quando c'e' davvero si fa vedere, con una finestra che dice cosa e' cambiato e tre
strade: aggiorna adesso, vai a leggere la pagina, o metti da parte questa versione e non
se ne parla piu'.

Il controllo si spegne dalle impostazioni (Generale → Aggiornamenti), dove c'e' anche il
pulsante per cercare subito e la data dell'ultimo controllo.

**«Aggiorna ora» fa tutto: scarica l'installatore, lo apre e si toglie di mezzo.**
DecoDXLog si chiude da solo, perche' un installatore non puo' sostituire i file di un
programma in esecuzione. Il log e le impostazioni restano dove sono — stanno in
`%APPDATA%\Decodium`, che l'installatore non tocca ne' installando ne' disinstallando.

**Ma non si aggiorna mai da solo senza chiedere.** Il controllo e' automatico, lo scarico
no: durante un contest nessuno vuole un programma che si cambia sotto i piedi. E una
versione messa da parte non si ripropone.

**C'e' l'installatore.** `DecoDXLog-<versione>-setup.exe`: si installa per l'utente, in
`%LOCALAPPDATA%\Programs\DecoDXLog`, **senza chiedere i permessi di amministratore** — e
proprio per questo l'aggiornamento automatico puo' lanciarlo da solo. Mette il
collegamento nel menu Start (quello sul desktop e' facoltativo), riconosce una versione
gia' installata e la aggiorna invece di affiancarle una seconda copia, e se DecoDXLog e'
aperto lo dice e si offre di chiuderlo. Il disinstallatore c'e', e porta via solo il
programma. L'installatore parla undici lingue di quelle di casa: inglese, italiano,
tedesco, francese, spagnolo, catalano, olandese, danese, ungherese, giapponese, russo.

Lo zip resta: chi preferisce scompattare e lanciare fa come ha sempre fatto.

**Le cose che si potevano sbagliare, provate una per una.** Il confronto delle versioni
non e' un confronto di lettere: `1.10.0` viene **dopo** `1.9.0`, non prima. Una bozza o un
pre-rilascio non si propongono. Una risposta che non si capisce non e' una versione nuova.
Sette prove tengono ferme queste cose (`tests/tst_updates.cpp`).

E la catena intera e' stata provata davvero, con un finto GitHub in locale: il controllo
trova la versione, la finestra si apre da sola, «Aggiorna ora» scarica il file, lo lancia
e DecoDXLog si chiude. L'installatore vero e' stato installato, avviato e disinstallato:
1775 file dentro, niente lasciato indietro, collegamento nel menu Start creato e tolto.

## 1.1.0 — 21 settembre 2026

**La frequenza in cima e' diventata una manopola.** Le cifre non sono piu' solo da
guardare:

* **la rotellina del mouse muove la cifra sotto il puntatore** — come la manopola di una
  radio, dove ogni tacca vale quanto la cifra che stai guardando. Una sottolineatura dice
  quale cifra sta per muoversi, cosi' la rotellina non e' una sorpresa: sopra il `4` di
  `14.074000` si va di 1 MHz, sopra l'ultima cifra di 1 Hz;
* **un clic apre la casella e la frequenza si scrive**. In MHz (`14.074`) o in kHz
  (`14074`), come viene comodo: un numero piu' grande di mille non puo' essere altro che
  kHz. Invio conferma, Esc lascia le cose com'erano.

**Il modo si sceglie da un elenco.** La pillola accanto alla frequenza si clicca e si apre
la lista: CW, CW-R, USB, LSB, AM, FM in cima, e sotto tutti i digitali — FT8, FT4, FT2,
JS8, JT65, JT9, Q65, MSK144, WSPR, RTTY, RTTY-R, PSK31, PSK63, OLIVIA, MFSK, CONTESTI,
HELL, SSTV, PACKET.

**Dove va a finire quello che scegli.** Alla radio, se il CAT e' collegato, e a Decodium,
se DecoLink ha qualcuno dall'altra parte — cosi' i due non si raccontano due frequenze
diverse. Se non c'e' nessuno dei due, il registro lo dice invece di far finta di aver
fatto qualcosa.

La radio, pero', i modi non li chiama come li chiamiamo noi: per Hamlib tutti i digitali
sono una banda laterale con i dati dentro. La corrispondenza sta in un posto solo
(`src/core/Modes.cpp`) e ha le sue prove:

| quello che scegli | quello che riceve la radio |
|---|---|
| CW · CW-R | `CW` · `CWR` |
| USB · LSB · AM · FM | `USB` · `LSB` · `AM` · `FM` |
| RTTY · RTTY-R | `RTTY` · `RTTYR` |
| FT8, FT4, JS8, PSK31, Q65… | `PKTUSB` |
| PACKET | `PKTFM` |
| un modo che non si conosce | `USB`, che non fa danni |

Provato con un finto rigctld: scegliendo 7.026 in CW arriva `+F 7026000` e `+M CW`,
scegliendo 14.074 in FT8 arriva `+F 14074000` e `+M PKTUSB`.

**E la frequenza si vede anche senza Decodium.** Se Decodium non c'e' ma il CAT e'
collegato, in cima compare quella della radio, con il suo modo: prima restava
`--.------` anche con la radio accesa.

**Il pannello del rotore, finalmente in inglese.** Le sue frasi avevano il sorgente in
italiano (arriva da DecoRotor): in inglese quel pannello parlava italiano, ed era la cosa
segnalata dalla 0.9.6 in poi. Adesso i sorgenti sono inglesi come tutto il resto, e le
quindici traduzioni si sono portate dietro quello che avevano gia' — `lupdate` dice «0
new», cioe' non si e' perso niente per strada. Anche la rosa dei venti era italiana nel
sorgente: `SO` e `O` in inglese non vogliono dire sud-ovest e ovest, e adesso sono `SW` e
`W`.

**E il pannello si e' sistemato anche nella colonna stretta.** I quattro passi (−10, −1,
+1, +10) andavano a capo a meta' e la scritta di stato si mangiava il resto. Adesso i
comandi stanno in fondo, larghi quanto il pannello, in due file ordinate, e il quadrante
si stringe con la colonna invece di schiacciare tutto il resto.

## 1.0.0 — 21 settembre 2026

**Quindici lingue su quindici.** Russo, giapponese e le due forme del cinese chiudono il
giro: 1212 frasi per lingua, nessun angolo lasciato in inglese.

> Italiano · English · Deutsch · Français · Español · Nederlands · Dansk · Català ·
> Magyar · Română · Latviešu · **Русский** · **日本語** · **简体中文** · **繁體中文**

**I plurali, finalmente contati da chi li sa contare.** I quindici file `.ts` erano nati
copiando l'italiano e dicevano tutti `language="it_IT"`: lrelease dava a tutti *due* forme
plurali, quelle dell'italiano. Adesso ogni file dice la sua lingua, e le forme sono quelle
giuste. In che ordine stanno non si indovina: si e' costruito un `.qm` con forme numerate
e si e' guardato quale esce per n = 0, 1, 2, 3, 5, 11, 21, 101.

| lingua | forme | come vengono usate |
|---|---:|---|
| ungherese | 1 | dopo un numero il nome resta singolare |
| italiano, tedesco, francese, spagnolo, catalano, olandese, danese | 2 | 1 · tutto il resto |
| rumeno | 3 | 1 · 0 e 2-19 · da 20 in su, che vuole «de» |
| lettone | 3 | 1 · 2-19 · 0 |
| russo | 3 | 1 · 2-4 · 0 e 5-20 |
| giapponese, cinese | 1 | il numero non cambia il nome |

Trenta frasi con `%n` per lingua, riscritte una per una con il numero di forme che serve:
prima il russo contava come l'italiano, e «5 QSO» prendeva la forma di «2 QSO».

**I caratteri vengono dopo la lingua.** Consolas e Segoe UI non hanno gli ideogrammi: in
giapponese o in cinese mezza finestra diventava una fila di quadratini. Adesso, quando la
lingua li vuole, si parte da un carattere che li ha — MS Gothic, NSimSun, MingLiU, e per
il testo normale Yu Gothic UI, Microsoft YaHei, Microsoft JhengHei. Sono caratteri a
larghezza fissa anche per le lettere latine, quindi la tabella del log resta in colonna.

E se sul computer non c'e' nessun carattere con gli ideogrammi — succede su un Windows
europeo, dove i font giapponesi e cinesi si scaricano a parte — il registro di attivita'
lo dice a chiare lettere, nella lingua scelta, invece di lasciar credere che sia rotto il
programma:

> Questo computer non ha un carattere con gli ideogrammi: la scrittura viene fuori a
> quadratini. Su Windows arrivano con la lingua: Impostazioni → Data/ora e lingua →
> Lingua → Aggiungi una lingua.

**Due frasi che nessuno aveva mai tradotto.** `lupdate` non girava da un po': «radio
(CAT)» — quella che compare nel Cloud quando la frequenza arriva dal CAT invece che da
Decodium — non era mai entrata nei file. Adesso c'e', in tutte e quindici.

**La rosa dei venti, per finire.** In russo la bussola dice `С СВ В ЮВ Ю ЮЗ З СЗ`, in
giapponese e in cinese `北 北東 東 南東 南 南西 西 北西`. La `S` si cambia solo dentro il
contesto `RotorPointing`: la stessa `S` e' anche la colonna del log (RST inviato), e li'
resta `S`, che e' la sigla internazionale.

## 0.9.8 — 21 settembre 2026

**Ungherese, rumeno e lettone complete.** Undici lingue su quindici sono finite: 1210
frasi ciascuna.

> Italiano · English · Deutsch · Français · Español · Nederlands · Dansk · Català ·
> **Magyar** · **Română** · **Latviešu**

Restano russo, giapponese e le due forme del cinese — quattro lingue, l'ultimo giro. Lo
stato si guarda sempre con:

```sh
python scripts/translations.py stato
```

**Le rose dei venti di tre alfabeti diversi.** L'ungherese non scrive le lettere della
bussola come nessun'altra lingua di casa: dove l'italiano ha `E` (est) l'ungherese ha `K`
(kelet) e il lettone `A` (austrumi); dove l'italiano ha `O` (ovest) l'ungherese ha `Ny`
(nyugat) e il lettone `R` (rietumi); il nord ungherese e' `É`, quello lettone `Z`. Il
rumeno invece segue l'italiano, con `V` per ovest e `SV` per sud-ovest. Sono poche
lettere, ma una bussola con le lettere di un'altra lingua non si legge.

**Due cose trovate guardando le finestre, non i file.** In lettone «Station» e «Filters»
restavano in inglese in cima alla finestra: erano fra le voci riprese da Decodium, dove la
traduzione c'era ma era la parola inglese identica — quindi risultavano «fatte» e nessun
conteggio se ne accorgeva. Riscritte, con altre quindici della stessa specie (`Avots`,
`Brīdinājumi`, `Filtri`, `Valoda`, `Fails`, `Stacija`…). E la lettera `S` della bussola era
rimasta `S` anche in olandese, ungherese e lettone: adesso e' `Z`, `D` e `D`. Si cambia
solo dentro il contesto `RotorPointing`, perche' la stessa `S` e' anche la colonna del log
(RST inviato) e li' deve restare `S`.

**I plurali, contati per davvero.** Prima di scrivere le trenta frasi con `%n`, si e'
chiesto a `lrelease` quante forme vuole ogni lingua: con tre forme rispondeva «Removed
plural forms as the target language has less forms», con due no. Quindi due forme per
tutte e tre, e nessuna forma buttata via in silenzio.

## 0.9.7 — 21 settembre 2026

**Olandese, danese e catalano complete.** Altre tre lingue al 100%: 1210 frasi ciascuna,
come per tedesco, francese e spagnolo. Otto lingue su quindici sono finite:

> Italiano · English · Deutsch · Français · Español · **Nederlands** · **Dansk** ·
> **Català**

Restano ungherese, rumeno, lettone, russo, giapponese e le due forme del cinese: per
adesso hanno l'ossatura. Lo stato si guarda sempre con:

```sh
python scripts/translations.py stato
```

**Anche il pannello del rotore parla le tre lingue nuove.** Le sue frasi hanno il sorgente
in italiano (arriva da DecoRotor), quindi si sono tradotte dall'italiano: in olandese,
danese e catalano la finestra DecoRotor e' tutta nella lingua scelta — la bussola, il
control box, le memorie, la diagnostica e le impostazioni. Resta da fare la cosa giusta,
cioe' portare quei sorgenti in inglese come il resto del programma.

**La rosa dei venti si traduce davvero.** Le lettere della bussola non sono uguali
dappertutto: dove l'italiano scrive `E` (est) l'olandese scrive `O` e il danese `Ø`, e
dove l'italiano scrive `O` (ovest) l'olandese scrive `W` e il danese `V`. Il catalano usa
le stesse lettere dell'italiano. Sono otto lettere, ma una bussola con le lettere di
un'altra lingua non si legge.

**Il pacchetto non poteva piu' uscire a meta'.** Preparando questa versione, `deploy.sh`
ha prodotto un archivio di 46 MB invece dei soliti 110: mancavano `libstdc++-6.dll`,
`libwinpthread-1.dll` e le altre librerie del compilatore, e la cartella non si sarebbe
aperta su nessun computer. Il motivo: `ldd` scrive la libreria come la vede la shell —
dentro MSYS2 `/mingw64/bin/...`, da fuori `/c/msys64/mingw64/bin/...` — e lo script
guardava una scrittura sola. Adesso le riconosce tutte e due, e prima di fare l'archivio
controlla che le librerie del compilatore ci siano: se mancano si ferma, invece di
spedire una cartella che non parte.

## 0.9.6 — 21 settembre 2026

**Tedesco, francese e spagnolo complete.** Tre lingue al 100%: 1210 frasi ciascuna, non
solo i pulsanti — i messaggi del registro, le spiegazioni nelle impostazioni, i nomi delle
colonne, gli avvisi del cluster, i testi del contest e del rotore. Chi sceglie Deutsch,
Français o Español trova il programma nella sua lingua dappertutto.

Restano da fare, nell'ordine: olandese, danese, catalano, ungherese, rumeno, lettone,
russo, giapponese e le due forme del cinese — per adesso hanno l'ossatura. Si va avanti
tre lingue per volta, e lo stato si guarda sempre con:

```sh
python scripts/translations.py stato
```

**Una cosa trovata traducendo:** il pannello del rotore ha le frasi scritte **in
italiano** nei sorgenti, non in inglese (arriva da DecoRotor). Tradotte lo stesso in tutte
e tre le lingue, ma andrebbero portate in inglese come il resto del programma: in inglese,
oggi, quel pannello parla italiano.

## 0.9.5 — 20 settembre 2026

**Quindici lingue, come Decodium 4.** DecoDXLog parlava italiano e inglese; adesso nella
tendina della lingua ci sono tutte e quindici le lingue di casa Decodium, ognuna scritta
come la scrive chi la parla:

> Italiano · English · Deutsch · Français · Español · Català · Nederlands · Dansk ·
> Magyar · Română · Latviešu · Русский · 日本語 · 简体中文 · 繁體中文

Il cinese si distingue come si deve: **tradizionale** a Taiwan, Hong Kong e Macao,
**semplificato** altrove — e la differenza non si vede dalle prime due lettere, quindi si
guarda il Paese. Se la lingua chiesta non c'e', si prova quella senza variante (zh_TW → zh)
e poi si resta in inglese: mai un'interfaccia a meta' per colpa di un file che manca.

**Le parole si portano dietro quelle di Decodium.** Dove il testo inglese e' identico, la
traduzione e' quella che Decodium 4 usa gia': chi passa da un programma all'altro trova le
stesse parole, non due modi di dire la stessa cosa.

**Lo stato delle traduzioni si guarda con un comando**, perche' un lavoro cosi' lungo si
governa solo se si vede:

```sh
python scripts/translations.py stato
```

Per adesso italiano e inglese sono complete; le altre tredici hanno **l'ossatura** —
barra, pannelli, schede, colonne del log, menu — e crescono a ogni versione. Dove una
frase non e' ancora tradotta compare quella inglese: il programma resta leggibile, non
resta un buco.

## 0.9.4 — 20 settembre 2026

**La frequenza della radio si vede anche dal browser.** Al Cloud la frequenza partiva solo
quando arrivava lo stato di Decodium o WSJT-X via UDP. Chi opera in SSB o in CW, senza un
programma che manda quello stato, dal log online risultava senza frequenza: la radio era li'
accesa e il Cloud non lo sapeva. Adesso, se l'UDP non dice niente, si guarda **il CAT** —
frequenza, banda e modo della radio — e il VFO che si muove e' una notizia quanto un QSO.
Provato con una radio finta: nel Cloud arrivano 14.074.000 Hz, banda 20m, «radio (CAT)».

**La pagina Stazione e' la finestra Impostazioni.** Dal browser era un elenco di chiavi e
tendine; adesso e' la stessa finestra del programma: le pagine a sinistra nello stesso
ordine e con gli stessi nomi — Generale, Tema e densita', Collegamento a Decodium, Sync e
Cloud, Servizi QSL, Callbook, Radio (CAT), Rotore, Copie di sicurezza — e dentro le stesse
sezioni con le stesse etichette.

Quello che ha senso cambiare da lontano (tema, densita', lingua, sync automatico, e le
altre) si cambia e torna al programma alla sincronizzazione dopo; il resto si legge, perche'
una porta seriale scritta da un telefono non vuol dire niente su un altro computer. In
fondo alla colonna resta **Tutte le voci**, con l'elenco completo di quello che il
programma sincronizza: nessuna impostazione sparisce dalla vista.

## 0.9.3 — 20 settembre 2026

**La finestra principale si ricorda anche dove sta.** Ricordava quanto era grande ma non
dove: chi la tiene sul secondo monitor se la ritrovava ogni volta dove decideva Windows. Le
finestre staccate la posizione se la ricordavano da sempre; questa, che e' la principale,
no — mancavano proprio le due righe. Adesso ci sono, e se lo schermo di prima non c'e' piu'
la finestra torna al centro di questo invece di aprirsi nel nulla.

**Il menu del marchio non e' piu' solo un disegno.** Le tre righette in alto a sinistra
aprono un menu vero — e si clicca su tutto il riquadro, nome compreso:

- **Informazioni su DecoDXLog**
- Impostazioni, Profili stazione, Pannelli
- Importa ADIF, Esporta ADIF, Apri la cartella del log
- Esci

**La finestra «Informazioni»** dice cos'e' questo programma e chi l'ha fatto: versione,
sviluppatore (Martino Merola — IU8LMC), email, dove sta il codice, quanti QSO ci sono nel
log di questa copia, con quale Qt e quando e' stato compilato, e la licenza — software
libero, GPL-3. C'e' anche un pulsante che copia tutti questi dati negli appunti, per
quando si segnala qualcosa.

## 0.9.2 — 20 settembre 2026

**La spia dei blocchi.** Quando la finestra smette di rispondere, adesso lo scrive lei nel
**Registro attivita'**: *«la finestra e' rimasta ferma per 3,2 s»*, con quante volte e'
successo dall'avvio. E le operazioni che leggono tutto il log — caricare la tabella,
ricaricarla, rifare l'elenco dei gia' lavorati per il cluster — si cronometrano da sole e
dicono quanto ci hanno messo, se superano il mezzo secondo. Un blocco raccontato a voce non
si trova; un blocco col suo nome e la sua durata nel registro si corregge.

**La sincronizzazione non blocca piu' la finestra.** Preparare i QSO da mandare al Cloud —
ognuno e' una lettura dal log piu' la costruzione del record — si faceva tutto in un colpo:
con la coda piena la finestra restava ferma per secondi. Adesso si prepara a fette,
tornando in mezzo a servire l'interfaccia. Ci si mette lo stesso tempo, ma il programma
resta vivo.

Sulla lentezza sono stati misurati, e **esclusi**, anche: DecoLink (manda un megabyte in
venti millisecondi col log da 23.830 QSO), il ponte CAT di Decodium (risponde una riga per
domanda e non manda niente di sua iniziativa), il caricamento e le ricariche della tabella,
i conti dei diplomi e della mappa.

## 0.9.1 — 20 settembre 2026

**DecoLink si collegava e si staccava ogni cinque secondi, e la colpa era del nome.** Nel
saluto di DecoLink il campo `app` non e' il nome del programma: e' il nome con cui questo
lato si presenta nel protocollo, e Decodium **chiude la connessione** se ne legge uno che
non conosce. Rinominando il programma in DecoDXLog e' cambiato anche il saluto, cosi'
Decodium si collegava, leggeva un nome che non era «DecoLog», staccava, e riprovava dopo
cinque secondi. Per sempre.

Adesso `app` resta `DecoLog` — e' un pezzo di protocollo, come i campi ADIF
`APP_DECOLOG_*` — mentre il nome vero viaggia accanto, nel campo nuovo `product`. Un test
lo inchioda, cosi' non ricapita.

## 0.9.0 — 20 settembre 2026

**Il CW parte anche con Decodium aperto.** Chi opera con Decodium ha la porta della radio
gia' occupata: il CAT passa dal ponte di Decodium, e quel ponte il manipolatore non lo sa
fare — i tasti F1-F8 restavano li' senza fare niente. Adesso il CW lo manipola DecoDXLog da
solo, come si e' sempre fatto: alza e abbassa **DTR o RTS di una porta tutta sua**, quella
attaccata al circuito di manipolazione. Decodium si tiene il CAT, le macro vanno in aria.

Si sceglie in **Impostazioni → Radio (CAT) → Manipolazione su porta seriale**: la porta, il
piedino (DTR o RTS) e un pulsante **Manda VVV** per sentire se la radio va in aria davvero.
Lasciando la porta su «nessuna» si manipola dal CAT come prima.

I tempi non ballano: la manipolazione gira in un thread suo a priorita' alta, che dorme a
colpi corti e chiude l'attesa contando i microsecondi — i timer di Windows, a 40
millisecondi per punto, sbagliano di piu' di quanto dura il punto. PARIS a 20 parole al
minuto dura tre secondi esatti, che e' la definizione stessa della velocita' in CW.

**DecoLink non si arrende alla porta occupata.** Se all'avvio la porta 52237 e' presa —
quasi sempre un altro DecoDXLog ancora aperto — prima DecoLink si spegneva e basta, per
tutta la sessione. Con due copie aperte Decodium si collegava ora all'una ora all'altra, e
il collegamento sembrava andare e venire. Adesso si riprova ogni quindici secondi e appena
la porta si libera l'ascolto riparte da solo, dicendolo nel registro.

## 0.8.1 — 20 settembre 2026

**Il marchio rimasto in maiuscolo.** Cambiando nome al programma si cercava "DecoLog"
scritto misto, e ai marchi scritti tutto in maiuscolo la rinomina e' passata accanto senza
vederli. Entrando nel log dal browser il logo in alto diceva ancora **DECOLOG**; adesso
dice DECODXLOG, nella pagina di accesso e in quella del log. Corretti anche il nome del
file quando si esporta un'attivazione senza nominativo, e le macro di compilazione rimaste
indietro.

Restano col nome vecchio, apposta, le variabili d'ambiente del server — sono scritte nei
file di servizio gia' installati — e il campo ADIF `APP_DECOLOG_TAGS`, che sta dentro i log
gia' esportati.

## 0.8.0 — 20 settembre 2026

**Il programma si chiama DecoDXLog.** Nome nuovo e icona nuova — la nuvola del Cloud con
le righe del log, l'onda del segnale e il nome DecoDXLog — dappertutto: titolo della
finestra, eseguibile (**DecoDXLog.exe**), proprieta' del file, pacchetto, documenti,
traduzioni e deposito su GitHub.

**Il log e le impostazioni traslocano da soli.** Al primo avvio col nome nuovo, quello che
stava nelle cartelle di DecoLog viene portato nelle cartelle di DecoDXLog:

- `%APPDATA%\Decodium\DecoLog\` → `%APPDATA%\Decodium\DecoDXLog\` (il log, i backup,
  le carte QSL)
- `decolog.sqlite` → `decodxlog.sqlite`
- `DecoLog.ini` → `DecoDXLog.ini` (tutte le impostazioni: Cloud, radio, cluster, colonne,
  disposizione dei pannelli)
- la cache del cluster in `%LOCALAPPDATA%`

**Si copia, non si sposta**: la roba di prima resta dov'era. Se qualcosa non torna, il log
di sempre e' ancora al suo posto e si riapre col programma vecchio. L'unica accortezza e'
non lavorare un po' di qua e un po' di la': da qui in avanti vale quello nuovo.

Restano col nome vecchio le cose che nessuno vede e che non si possono cambiare senza
rompere qualcosa: i campi ADIF nostri (`APP_DECOLOG_*`, che stanno gia' dentro i log di
chi ha esportato), il servizio sul server (`decolog-cloud`) e il namespace del codice.

## 0.7.0 — 20 settembre 2026

Tre cose che mancavano, chieste da chi il log lo usa.

**Un file ADIF con dentro solo i QSO marcati.** Si marcano le righe che servono — una o
dieci — e col tasto destro **«Salva i 10 QSO scelti in un file ADIF…»**. Il nome di
partenza lo propone DecoDXLog (il nominativo se e' uno solo, altrimenti quanti sono, piu' la
data). Prima l'unica strada era esportare tutto il log e poi togliere a mano le
ventiduemila righe di troppo.

**La disposizione si blocca.** Tasto destro sulla testata di un pannello qualsiasi:
**Blocca la disposizione**. Le maniglie restano disegnate ma non si tirano piu', e i
pannelli non si spostano: quello che si e' sistemato resta com'e'. Dallo stesso menu si
stacca il pannello in una finestra sua, lo si chiude, si aprono i Pannelli o si rimette la
disposizione di partenza.

**I pannelli si scambiano di posto.** Si prende un pannello per la maniglia **⠿** in alto a
sinistra e lo si porta sopra un altro: quello sotto si accende — il magnete, con scritto
**qui** — e lasciando il pulsante i due si scambiano di casella. Come il layout DX-Pedition
di Decodium. Le otto caselle restano dove sono e tengono le loro misure: chi va nella
colonna di sinistra prende la larghezza della colonna di sinistra. La disposizione scelta
resta da una sessione all'altra, e a disposizione bloccata non si muove niente.

Sotto il cofano, i pannelli non sono piu' scritti uno per uno nel loro posto: ogni posto e'
una casella che ospita il pannello che le tocca (`PanelSlot.qml`).

## 0.6.0 — 20 settembre 2026

**Le finestre staccate stanno ferme.** Staccando un pannello mentre altri erano gia' in
finestra, le cose si scombinavano: il primo staccato spariva dall'elenco con la finestra
ancora aperta, restavano finestre orfane, e uscendo dal programma i pannelli tornavano
tutti dentro. Tre cose messe a posto:

- **Chi stacca due pannelli di fila non ne perde uno.** Le funzioni leggevano una lista
  calcolata a partire dalle impostazioni, che si aggiorna quando le pare: due chiamate
  ravvicinate leggevano la lista di prima e si cancellavano a vicenda. Adesso leggono
  sempre le impostazioni.
- **Le finestre non rinascono tutte a ogni cambiamento.** L'elenco era una lista semplice,
  e cambiandola Qt rifaceva da capo *tutte* le finestre: quelle aperte sparivano e
  tornavano altrove, svuotate. Adesso si aggiunge e si toglie una riga sola.
- **Uscire da DecoDXLog non riaggancia niente.** Chiudendosi, ogni finestra staccata diceva
  "riagganciami", e la volta dopo i pannelli erano tutti nella finestra principale.

**La X della finestra principale chiude DecoDXLog.** Con un pannello in finestra propria il
programma restava in piedi: Qt aspetta che si chiuda l'ultima finestra, e quella era
ancora li'.

**Le finestre non si chiudono piu' di sotto.** Statistiche, cluster, contest, QSL, log e
pannelli staccati si spegnevano da dentro il proprio evento di chiusura — distruggere una
finestra mentre si sta chiudendo e' il genere di cosa che fa cadere il programma invece di
chiudere una finestra.

**La finestra tornata dal secondo monitor.** Misura e posizione restano, ma il secondo
monitor a volte non c'e' piu': la finestra si riapriva a quelle coordinate, cioe' nel
nulla. Adesso, se non c'e' nessuno schermo dove stava, torna al centro di questo.
Aggiunte anche le misure minime che mancavano: nessuna finestra si puo' piu' schiacciare
fino a rompersi.

**La lingua scelta vale davvero.** All'avvio la lingua si leggeva dal registro invece che
dal file delle impostazioni, dove sta: su un Windows inglese, chi sceglieva l'italiano si
ritrovava l'inglese lo stesso.

**Per chi prova:** `--settings <cartella>` tiene le impostazioni li' dentro invece che fra
quelle vere.

## 0.5.9 — 20 settembre 2026

**La fascia del DX Cluster si tira come tutte le altre.** Sugli spot l'altezza minima
diventava 340 punti: la fascia si alzava da sola — giusto — ma poi **non si poteva piu'
abbassare** finche' si restava li', e l'altezza forzata veniva salvata come altezza di
tutte le altre schede. Adesso il minimo e' 130 come dappertutto, e **il cluster tiene
un'altezza sua**: chi guarda gli spot la vuole alta, chi guarda il registro la vuole
bassa, e cambiando scheda ognuna ritrova la propria senza rifarla ogni volta.

## 0.5.8 — 20 settembre 2026

**Il campanello del nodo non nasconde piu' gli spot.** DX Spider attacca in coda a ogni
spot uno o due BEL — il carattere che fa suonare il terminale:

    DX de KC7PFR:  14015.0  SJ2W  CQ CONTEST  0214Z<BEL><BEL>

Restavano appiccicati alla Z dell'orario, la riga non veniva riconosciuta come spot e
finiva nella console come testo qualunque. Il cluster "girava" — nella console si vedeva
passare tutto — e **la tabella restava vuota**. Adesso i caratteri di comando si tolgono
prima di leggere la riga, sia quando arriva dal nodo sia quando la legge il parser, e gli
spot tornano in tabella.

## 0.5.7 — 20 settembre 2026

**Un nodo che tace non e' un nodo collegato.** Certi nodi accettano il collegamento e poi
non dicono piu' niente: la porta e' aperta, il servizio spento. DecoDXLog aspettava quattro
secondi, mandava il nominativo al buio, ne aspettava altri quattro e si dichiarava
**online** — fonte verde, e nemmeno uno spot in tabella, senza una parola che spiegasse
perche'. Adesso, se dal nodo non e' mai arrivato niente, la fonte resta in attesa e dice
*«il nodo risponde ma non dice niente: forse e' spento — prova un'altra fonte»*, e ci
riprova da sola.

E' quello che succede in questi giorni con **dxc.ve7cc.net**, che e' fra le fonti
preimpostate: accetta il collegamento e sta zitto. Gli altri nodi della lista — IZ7AUH,
W3LPL, DXFun, NC7J — rispondono e mandano spot regolarmente.

## 0.5.6 — 20 settembre 2026

**«Svuota il Cloud» si trova anche da scollegati.** La Zona pericolosa in Impostazioni →
Sync e Cloud spariva del tutto finche' non si era entrati nel Cloud: chi andava a cercarla
non la trovava e pensava che nella sua copia non ci fosse. Adesso c'e' sempre, col tasto
spento e una riga che dice di entrare prima — una funzione che sparisce e' una funzione che
non c'e'.

**Quando il server e' piu' vecchio del programma, lo dice.** Una richiesta che il Cloud non
conosce tornava indietro come «Not Found», e chi leggeva pensava di avere DecoDXLog rotto.
Adesso il messaggio dice che e' il *server* a dover essere aggiornato, e quale richiesta non
ha capito. Lato server, `/v1/health` dichiara anche **contest** e **purge** fra le funzioni,
cosi' un aggiornamento a meta' si vede con un solo `curl`.

## 0.5.5 — 20 settembre 2026

**L'icona nuova.** DecoDXLog ha la sua faccia: la nuvola del Cloud con dentro le righe del
log, l'onda del segnale sotto e il nome. Si vede nella barra delle applicazioni, in
Esplora risorse, nelle proprieta' del file e sulle finestre. Il disegno e le taglie
stanno in `resources/icon/`; `make_icon.py` adesso le mette solo insieme, invece di
disegnare l'icona da solo — le taglie piccole sono ritoccate a mano e una riduzione
automatica le rovinerebbe.

## 0.5.4 — 20 settembre 2026

**Le chiamate in rete passano per HTTP/1.1.** Qualche servizio pubblico chiude gli stream
HTTP/2 senza finire il discorso, e la richiesta moriva a meta' senza un errore sensato.
Adesso tutte le chiamate di DecoDXLog — callbook, LoTW, eQSL, QRZ, Club Log (anche il carico
a blocco), Cloud, cluster, POTA, dati solari — chiedono HTTP/1.1: stesso TLS, stessi tempi
di attesa, stessa gestione degli errori, ma la risposta arriva.

## 0.5.3 — 19 settembre 2026

**La radio si cerca da sola.** In Impostazioni → Radio (CAT) c'e' **Cerca la radio**: prova
le porte una per una — prima quella impostata, poi quella che usa Decodium, poi tutte le
altre — a 38400, 19200, 9600, 115200, 4800 baud, finche' una risponde alla domanda della
frequenza. Quella che risponde se la tiene e si collega. Se non risponde nessuna, lo dice
chiaro: radio spenta, CAT non attivo, o un altro programma che tiene il cavo.

**"Radio non raggiungibile" quando invece era raggiungibile.** La casella della porta nella
pagina Radio era legata in tutti e due i versi: appena si apriva la pagina, si riscriveva
da sola sulla prima porta dell'elenco (COM4) e la porta buona (COM5) andava persa. Adesso
la casella scrive solo quando ci si scrive dentro davvero, e la porta scelta resta.

**Nel log ci sono anche citta', nazione, stato, contea, zone e IOTA.** Le colonne che
riempie il callbook adesso si vedono nella tabella del log: **Citta' / QTH**, **Nazione**,
**Stato**, **Contea**, **CQ**, **ITU**, **IOTA**. Si nascondono e si riordinano dal menu
**Colonne** come tutte le altre, e la larghezza resta quella che si lascia.

## 0.5.2 — 19 settembre 2026

**Le porte COM si leggono col loro nome.** L'elenco delle porte seriali usciva con le righe
giuste di numero ma **vuote**: i nomi stavano nel registro sotto voci con le barre rovesce
(\Device\Silabser0) e non si riuscivano a leggere. Adesso le porte si chiedono a Windows
come si deve, e nella tendina ci sono COM4, COM5 e compagnia.

**Il PTT sulla seconda porta.** Chi ha due porte — il CAT su una, il PTT sull'altra, come
le CP2105 a doppia porta — adesso lo puo' dire a DecoDXLog: Impostazioni → Radio (CAT),
**PTT** su RTS o DTR e la porta dove sta. DecoDXLog lo passa a rigctld, cosi' **la radio
trasmette mentre il CAT continua a leggere la frequenza**. C'e' anche **Prova il PTT**, che
lo preme per un attimo: se la radio va in trasmissione, e' a posto.

## 0.5.1 — 19 settembre 2026

**Le tendine si leggono di nuovo.** Tutti gli elenchi a discesa si aprivano **vuoti**: le
righe c'erano, ci si poteva anche cliccare sopra, ma il testo non compariva. Colpa di come
erano scritte le righe della tendina — con le proprieta' "required" di QML, che con un
elenco di stringhe semplici fanno saltare la creazione della riga. Adesso la riga la
disegniamo noi, e funziona con qualsiasi tipo di elenco: bande, modi, temi, ingressi audio,
modelli di radio, servizi QSL.

**La radio si legge anche quando il CAT risponde "alla vecchia".** Rigctld chiude ogni
risposta con RPRT; altri ponti CAT — quello di Decodium, per esempio — rispondono col
valore e basta. DecoDXLog si aspettava sempre il RPRT e restava li' ad aspettare: frequenza e
modo non arrivavano mai. Adesso capisce tutte e due le maniere, e la frequenza della radio
compare anche collegandosi a Decodium.

**E quando il CW non puo' partire, lo dice.** Non tutti i ponti CAT sanno manipolare: il
ponte di Decodium, per dire, risponde "non lo so fare". Prima i tasti F1-F8 restavano li'
senza fare niente; adesso il pannello scrive in chiaro che quel collegamento il CW non lo
manda e che per le macro serve rigctld attaccato alla radio, e i tasti si spengono. Corretto
anche il comando di stop del manipolatore, che partiva senza la barra rovescia e quindi
rigctld non lo capiva.

## 0.5.0 — 19 settembre 2026

**Il CW sta in piedi da solo.** Il pannello **CW** non e' piu' dentro al contest: si apre
come tutti gli altri (Pannelli → CW), si stacca in finestra, e ci sta dentro tutto — le
otto macro sui tasti F1-F8, la velocita' in parole al minuto, una riga per mandare in CW
quello che si scrive sul momento, e il **decoder**. Di partenza e' chiuso: chi non fa CW
non se lo ritrova fra i piedi.

**Il decoder CW e' dentro DecoDXLog.** Non serve una radio che decodifichi: basta l'audio che
esce dalla radio. DecoDXLog guarda quanta energia c'e' sul tono del CW rispetto a quello che
gli sta intorno — il rumore e' largo, il CW e' stretto — e da quei tempi tira fuori punti,
linee e lettere. La velocita' non si imposta: la impara dai punti che arrivano, e la scrive
insieme al tono che ha trovato. Provato su segnali veri generati a 15, 25 e 35 parole al
minuto, col tono cercato da solo fra 400 e 1000 Hz e col rumore in banda.

**La radio anche col cavo.** Prima serviva un rigctld gia' acceso; adesso in Impostazioni
→ Radio (CAT) si sceglie **Cavo seriale alla radio**, si prende il modello dall'elenco di
Hamlib (che DecoDXLog legge da `rigctld -l`), la porta COM e la velocita', e **rigctld lo
avvia DecoDXLog**. Per chi opera e' solo "COM5, questa radio".

**I campi che mancavano nel QSO.** Nella finestra del QSO nuovo ci sono adesso **nazione,
indirizzo/citta', stato, contea (JCC), DXCC, zona CQ, zona ITU, continente e QSL via**, e
il callbook li riempie da solo come faceva con nome, QTH e locatore. Nella scheda del QSO
si vedono anche indirizzo, e-mail e QSL via, che prima stavano solo fra i campi ADIF.

**Il punteggio del contest sul Cloud.** Nuova scheda **Contest** nel log online: QSO validi,
duplicati, punti, moltiplicatori e punteggio, banda per banda, sulle ultime ore che si
scelgono. Si conta come si conta in gara — stesso nominativo, stessa banda e stesso gruppo
di modi e' un duplicato; i moltiplicatori valgono una volta per banda — e il
moltiplicatore si sceglie fra entita' DXCC, prefissi WPX e zone CQ, coi punti per QSO che
si cambiano li'. Serve a guardare come sta andando la gara da un altro computer o dal
telefono, mentre in shack si macina.

## 0.4.0 — 19 settembre 2026

**Le macro CW, col manipolatore della radio.** DecoDXLog parla con **rigctld**, il demone di
Hamlib: da li' legge frequenza e modo, sposta la radio, e soprattutto le passa il testo da
mandare in CW. Nella finestra contest c'e' la fila delle otto macro sui tasti **F1-F8**,
con i buchi che si riempiono da soli — {CALL} chi stai lavorando, {MYCALL} il tuo
nominativo, {RST} il rapporto, {NR} il progressivo, {EXCH} quello che hai ricevuto —, la
manopola della velocita' in parole al minuto e **Esc** per fermare tutto. Le macro si
scrivono come si vuole e restano. Impostazioni → **Radio (CAT)** per dire dove sta
rigctld; il manipolatore e' quello della radio, quindi quello che senti nel monitor e'
quello che va in aria.

**Il certificato LoTW si trova, finalmente.** Su Windows TQSL tiene i suoi dati in
%APPDATA%\TrustedQSL — la cartella "Roaming" — e DecoDXLog cercava nell'altra: un TQSL a
posto sembrava non installato. Adesso guarda dove deve, e distingue il certificato **del
nominativo** (quello che arriva col file .tq6 di ARRL) dalle radici che TQSL si mette da
solo: se manca, lo dice chiaro e scrive anche in che cartella sta guardando.

**Svuotare il Cloud, scrivendo DELETE.** Impostazioni → Sync e Cloud, in fondo, c'e' la
zona pericolosa: si cancella tutto quello che il nominativo ha sul server — QSO, storico,
profili, impostazioni, credenziali sigillate — e per farlo bisogna **scrivere DELETE**,
come su GitHub. L'account resta e il log su questo computer non si tocca: alla prossima
sincronizzazione risale da capo.

**Niente piu' nero su nero.** Da Qt 6.8 i menu di QML possono diventare menu **nativi** di
Windows: quelli non sanno niente del tema e su sfondo scuro scrivevano nero su nero —
sottomenu, tendine e il menu del tasto destro dentro i campi di testo. Adesso i menu li
disegna DecoDXLog, sempre, coi suoi colori.

## 0.3.9 — 19 settembre 2026

**Le colonne del log si tirano.** Il bordo fra due intestazioni si trascina col mouse e la
colonna si allarga o si stringe; la misura resta, anche nel log staccato in finestra, e si
ricorda con il nome della colonna, non con la sua posizione. Dal menu «Colonne», «Larghezze
di partenza» rimette tutto com'era.

**Le conferme QSL su piu' QSO in una volta.** Scelte le righe nel log (clic sinistro per
aggiungerle, Esc per lasciarle andare), il tasto destro ha adesso «Manda i N QSO scelti
a…»: LoTW, eQSL, QRZ Logbook, Club Log — quelli pronti; gli altri si vedono con scritto
cosa gli manca. Un QSO gia' andato a quel servizio non riparte. Anche le QSL cartacee
vanno in coda per tutte le righe scelte, bureau o diretta.

**Il DX Cluster ha lo spazio che serve.** Quando si sceglie la scheda DX Cluster la fascia
in basso si alza da sola: una decina di spot, non cinque. Chi la vuole piu' alta la tira,
come sempre.

## 0.3.8 — 19 settembre 2026

**Tutte le finestre sono finestre vere.** Diplomi, Impostazioni, scheda del QSO, nuovo
QSO, profili stazione, attivazione e testata Cabrillo erano riquadri incollati in mezzo al
programma: non si spostavano di un millimetro. Adesso sono finestre del sistema, con la
loro barra del titolo: si trascinano dove si vuole — **su un secondo schermo compreso** —
si ingrandiscono, si riducono a icona, e si riaprono dove le avevi lasciate, perche'
misura e posizione di ognuna si ricordano. E non bloccano piu' il resto: mentre guardi i
diplomi puoi lavorare nel log.

**Anche le altre finestre si ricordano dove stavano.** Statistiche, cluster, rotore,
contest, QSL cartacee e il log staccato salvavano la misura ma non la posizione: con due
schermi tornavano sempre su quello principale. Adesso no.

## 0.3.7 — 19 settembre 2026

**I pannelli fanno quello che gli si dice.** Ogni pannello ha adesso due comandi nella sua
testata: la freccia lo stacca in una finestra sua — che si sposta su un altro monitor, si
ridimensiona e si ricorda dove stava — e la crocetta lo chiude. Chiuso vuol dire chiuso:
il posto che occupava se lo prendono gli altri, non resta un buco. Dalla barra in alto il
pulsante **Pannelli** apre l'elenco di tutti e sette, dice di ognuno se e' agganciato, in
finestra o chiuso, e li fa tornare con un clic; e se ci si e' persi, «Rimetti la
disposizione di partenza» rimette tutto com'era. Il pulsante conta i pannelli chiusi,
perche' un pannello sparito senza dirlo e' un pannello perso.

**Niente piu' roba tagliata.** Le statistiche — che con tutte le bande e tutti i modi non
ci stavano piu' — adesso scorrono, e cosi' anche i diplomi, l'invio QSL e il pannello FT2
Award. Quello che non ci sta si scorre, non sparisce.

**Tutto si tira.** La colonna di destra (scheda nominativo, rotore, FT2 Award) e la fascia
in basso (schede e mappa) sono diventate anche loro divisori trascinabili: ogni pannello
si allarga e si stringe come si vuole. Le misure si ricordano quando la disposizione e'
intera — se un pannello e' chiuso, gli altri si allargano per riempire, e quella non e'
una misura scelta da nessuno.

## 0.3.6 — 19 settembre 2026

**Se un callbook non sa, si chiede all'altro.** I due non conoscono le stesse stazioni:
HamQTH ha chi si e' iscritto li', QRZ ha quasi tutti. Adesso un nominativo che il primo
non conosce viene chiesto al secondo — e, cosa che conta di piu', se il primo risponde ma
non dice ne' il quadrato ne' dove sta la stazione, si chiede lo stesso all'altro e le due
risposte si mettono insieme: comanda la prima, la seconda riempie i buchi. Su venti QSO
veri che prima restavano senza locatore, undici adesso ce l'hanno. Si spegne dalle
impostazioni, e serve che il secondo servizio abbia utente e password.

**I lavori sul log intero.** Dal menu «Azioni» del log: «Completa tutti i QSO senza
locatore», che mette in coda e chiede una cosa per volta (mezzo secondo l'una, si ferma
quando si vuole); e «Ripulisci i QSO rovinati da un vecchio import», per i valori tagliati
a meta' dalla vecchia lettura ADIF che contava i byte come caratteri — quelli che nel log
si leggono come `Vilnius<GRIDSQ`. Quello che non si puo' piu' leggere si svuota, cosi' il
callbook lo riscrive per bene, e il testo di prima resta nello storico.

## 0.3.5 — 19 settembre 2026

**Il locatore dal callbook, anche quando il callbook non lo scrive.** QRZ e HamQTH non
sempre mettono il quadrato, ma quasi sempre dicono dove sta la stazione: adesso il
locatore si ricava dalla posizione. E se il QSO ne ha uno piu' grossolano — JN61 come lo
manda la FT8 — e il callbook ne sa uno piu' preciso dentro lo stesso quadrato (JN61VB),
si tiene quello preciso. Un locatore diverso non si tocca: quello l'ha sentito la radio.

**Le QSL dette a parole.** Passando il mouse sopra le lettere L Q C E della riga si legge
com'e' andata con quel servizio: «LoTW: confermata — ricevuta», «QRZ Logbook: inviata, si
aspetta la conferma», «Club Log: non inviata». Anche l'intestazione della colonna dice
quali sono i quattro servizi.

## 0.3.4 — 19 settembre 2026

**JCC e JCG — le citta' e i distretti giapponesi.** Il numero del JARL sta nel campo CNTY:
quattro cifre (sei per i quartieri delle citta' designate) sono una citta', cinque sono un
gun. Le prime due cifre sono la prefettura, e diventano il nome che si legge accanto al
numero. Si contano come gli altri diplomi, banda per banda, col traguardo dei cento. Il
numero lo mette il callbook quando lo sa, oppure si scrive a mano nella scheda del QSO.

**Il Cloud conta gli stessi diplomi.** Fino a ieri la pagina web si fermava a DXCC, WAZ,
WAS e compagnia: adesso ha anche WAC, WAAC, WAJA, AJD, JCC e JCG, con le stesse regole del
programma.

## 0.3.3 — 19 settembre 2026

**Selezione multipla nel log.** Il clic sinistro sceglie le righe una dopo l'altra, lo
shift prende tutto quello che sta in mezzo, Esc lascia andare. Il tasto destro sulla
selezione la cancella: chiede due volte, perché cancellarne trenta per sbaglio non e' come
cancellarne una — poi restano comunque nello storico, come sempre. In testata c'e' scritto
quante righe sono scelte.

**WAAC — Worked All Africa.** Le entita' DXCC africane, una per paese, con il nome che
gli da' il cty.csv. Il traguardo non e' un numero inventato: sono tutte le entita'
africane che il file delle entita' conosce — oggi 76 — e cambia da solo quando si
aggiorna il cty.csv. Si legge anche banda per banda, come gli altri.

## 0.3.2 — 19 settembre 2026

**Il QSO non resta nudo.** Decodium manda l'essenziale — nominativo, rapporto, banda,
modo — e il resto restava fuori dal log anche quando la scheda a destra lo mostrava:
nome, locatore, citta'. Adesso, appena il QSO e' scritto, DecoDXLog chiede al callbook
(QRZ.com o HamQTH) e quello che torna riempie **solo i campi vuoti**: nome, QTH,
locatore, indirizzo, stato, contea, entita', zone. Quello che ha scritto l'operatore non
si tocca — ha visto il collegamento, il callbook no.

Una ricerca per nominativo, e la risposta si tiene un giorno: cento QSO con lo stesso
corrispondente non diventano cento ricerche. Si spegne da Impostazioni → Callbook. Sui
QSO gia' nel log si fa a mano: dal menu di una riga, "Completa dal callbook", oppure
"Completa dal callbook i QSO mostrati" per tutte quelle che si stanno guardando.

**Diplomi nuovi.** **WAC** — i sei continenti, con l'Antartide che si vede ma non fa
numero — **WAJA** (le 47 prefetture giapponesi, lette da STATE comunque siano scritte:
"12", "JA12", "12 Chiba") e **AJD** (i dieci distretti giapponesi, dalla cifra del
nominativo). E il **DXCC Challenge**: gli stessi DXCC contati banda per banda, dai 160 ai
6 metri (undici bande, 60 compresi), con il traguardo dei mille slot.

Tutti si leggono anche **per banda e per modo**, come gli altri: il WAC su cinque bande e
il WAS banda per banda erano gia' possibili, adesso ci sono anche i diplomi che mancavano.

**Le statistiche non si fermano piu' ai 15 metri.** La scheda in basso mostrava le prime
otto bande e i primi otto modi, e chi lavora in 12, 10, 6, 2 metri o piu' in su non li
vedeva. Adesso ci sono tutte.

**Eliminare un QSO dal log.** Nel menu di una riga, accanto a "Apri / modifica", c'e'
"Elimina QSO": con la stessa domanda di conferma della scheda, e la stessa cancellazione
morbida — la riga resta nello storico e si recupera.

**Nella scheda del nominativo** si legge anche lo **stato**: la provincia, lo stato USA
col suo nome, la prefettura giapponese col suo. Il locatore c'era gia' e adesso arriva
piu' spesso, perche' il callbook lo riempie.



**DecoDXLog fuori da Windows.** Salvatore Raccampo 9H1SR ha portato il programma dove
Windows non c'e', e le sue correzioni sono qui: i caratteri si scelgono guardando quelli
davvero installati — Cascadia Mono o Consolas su Windows, SF Mono, Menlo o Monaco su
macOS, DejaVu Sans Mono o Liberation Mono su Linux, e in mancanza di tutto quello che il
sistema dichiara come carattere a spaziatura fissa. Lo stesso per il carattere
dell'interfaccia, che adesso ha un nome suo (`Theme.uiFamily`) invece di affidarsi a
quello dell'applicazione: Segoe UI, SF Pro Text, Noto Sans, secondo dove si e'.

Il quadrante del rotore non chiede piu' "Consolas" per nome — prende quello del tema — e
la finestra delle attivazioni non lascia piu' cadere un avviso quando il tipo di sessione
non c'e' ancora. `StationProfileModel.h` include il database invece di dichiararlo a
mezz'aria: i compilatori piu' severi lo volevano.

## 0.3.1 — 19 settembre 2026

**Tutto il log sul Cloud, non solo i QSO.** Chi si collega da un secondo computer non
deve rifare la stazione a mano: adesso viaggiano anche i **profili stazione**
(nominativo di stazione, operatore, locatore, radio, antenna, potenza, quello
predefinito) e **tutte le impostazioni** — tema, lingua, colonne e filtri salvati del
log, fonti e avvisi del cluster, premi seguiti, invii automatici (LoTW, QSL),
propagazione, rotore, dedup della UDP, backup, e anche porte, percorsi e indirizzi dei
programmi accanto. Una stazione che si ritrova uguale, non una che le somiglia.

**Anche le password dei servizi, ma chiuse.** QRZ, LoTW, Club Log, eQSL, HamQTH,
HamAlert: sul secondo computer non si riscrivono a mano. Viaggiano — sigillate qui,
con **AES-256-GCM** e una chiave che nasce dalla password del Cloud (PBKDF2-HMAC-SHA256,
200.000 giri), quella che il server conosce solo come impronta Argon2. Al server arriva
un blocco di byte che **senza quella password non si apre**: nemmeno per chi avesse il
database in mano. Nessuna crittografia scritta a mano: e' OpenSSL, quello che sta sotto a
HTTPS. Sull'altro dispositivo si entra con la stessa password e i servizi sono pronti.
La chiave non passa mai dal server: si rifa' dalla password e poi vive nel portachiavi
accanto al token; "Scollega" la butta. Si spegne dall'interruttore in Impostazioni →
Sync e Cloud, e senza OpenSSL DecoDXLog lo dice e non manda niente. Chi si era collegato
**prima** che la cassaforte esistesse ha la chiave mancante: nella stessa pagina compare
"Apri la cassaforte", si dice la password una volta e basta — non serve scollegarsi.

Restano fuori solo due cose, e nessuna e' una scelta di chi opera: il **promemoria di
cosa e' salvato nel portachiavi di quella macchina**, che altrove farebbe credere a
DecoDXLog di avere una password che non ha, e il **quaderno del sync** (nominativo
collegato, ora dell'ultimo giro). Il profilo attivo viaggia per **uuid** e non per numero
di riga, cosi' sul secondo computer si accende lo stesso profilo anche se li' ha un altro
numero.

Il meccanismo e' quello dei QSO, cosi' le regole non si sdoppiano: ogni profilo e' un
documento con la sua revisione, le impostazioni sono un documento solo (`station`) con
un'impronta SHA-256 che dice se e' cambiato davvero qualcosa; server e programma si
scambiano `docs` e `docResults` dentro la stessa spinta e lo stesso cursore dei QSO.
Vince l'ultima modifica, la versione che perde resta nello storico. Le impostazioni che
JSON non sa dire (un filtro salvato e' un QVariant di Qt) viaggiano impacchettate, senza
perdere niente. In arrivo, il tema si ridipinge subito: non si aspetta il riavvio.

**Il log dal browser e' la stessa finestra del programma.** Non una pagina web che
parla dello stesso log: barra superiore a blocchi, tre colonne di pannelli — scheda del
QSO a sinistra, log in mezzo, scheda del nominativo con FT2 Award e mappa a destra — le
sei schede in basso — le stesse del programma: Diplomi, Statistiche, Invio QSL,
Registro attivita', DX Cluster, Propagazione — e la barra di stato. Si sceglie un QSO nel log e le colonne seguono, come nel programma.

E **i colori sono quelli della stazione**: il tema arriva con le impostazioni
sincronizzate, valore per valore dal ThemeManager — Ocean Blue, Stellar Light o
Darkcodium con la sua variante d'accento e la densita' delle righe. Chi ha il log in
Darkcodium ambra lo ritrova in Darkcodium ambra anche sul telefono.

Cambia una cosa sola, ed e' voluta: da qui si guarda e si scarica, si scrive dal
programma.

**Dentro le schede c'e' il resto del log.** Ci sono le stesse schermate del
programma, rifatte dal log che sta sul server: **Statistiche** (QSO per anno, mese, ora
UTC, banda, modo, continente, e la mappa di calore banda per ora che dice quando una
banda e' aperta), **Diplomi** (DXCC, FT2, WAZ, WAS, WPX, locatori, IOTA, POTA, SOTA,
WWFF: lavorati e confermati, il conto per banda, i band slot, e l'elenco di quello che
manca), **QSL** (inviate e ricevute servizio per servizio, e le ultime conferme),
**Mappa** (i locatori lavorati sul mondo, coste comprese) e **Stazione** (profili e
impostazioni).

**Propagazione** legge la stessa fonte del programma (il XML di N0NBH) una volta
all'ora: SFI, macchie, indice A e K, aurora, MUF, le condizioni banda per banda di giorno
e di notte con i loro colori, il VHF e il resto. Se la fonte non risponde si mostra
l'ultimo dato buono con la sua ora, invece di una pagina vuota.

Nella scheda **Invio QSL** ci sono le colonne del programma — da mandare, inviate,
confermate — e la **coda delle QSL di carta**: quelle che aspettano la cartolina, quelle
gia' partite e per che via (bureau, diretta, manager), quelle tornate.

I numeri non stanno in tabelle di riepilogo: si rifanno dai QSO a ogni richiesta, con le
stesse regole del programma — `server/decolog_cloud/analytics.py` e' `src/core/Awards.cpp`
portato in Python, gruppi di modi compresi, il prefisso WPX di CQ e i cinquanta stati.
Cosi' una correzione a un QSO si vede subito da tutte e due le parti, e la pagina non puo'
dire una cosa diversa dal programma.

Provato fra due log: il primo ha spinto 40 QSO, un profilo e 39 impostazioni; il
secondo, partito vuoto, si e' ritrovato il profilo "Casa di prova" gia' predefinito e le
impostazioni alla revisione 1, senza toccare niente a mano. E tre giri di sync di fila
non ne rimandano nemmeno una.

**I nominativi sono quelli che sono.** Registrando la stazione, il Cloud chiedeva almeno
tre caratteri di nominativo. Una regola inventata: nel mondo ci sono indicativi speciali
corti, e ci sono 9H1SR/M e VY2XT che quella soglia la passavano ma non avevano motivo di
essere misurati. Via la regola — dal server, dal programma, dalla pagina web e dalla
finestra del contest: basta che il nominativo ci sia. La password resta di almeno otto
caratteri, perche' quella e' una scelta, non un dato di fatto.

## 0.3.0 — 18 settembre 2026

**DecoDXLog Cloud: il sync fra dispositivi (Fase 3).** Il log resta il file SQLite, che
funziona anche senza rete; il Cloud è il posto dove i dispositivi si passano le modifiche.

Il servizio sta in `server/`: FastAPI e SQLAlchemy, SQLite per provarlo sul proprio
computer e PostgreSQL in servizio, Dockerfile e compose già pronti. Registrazione con
nominativo e password (tenuta con Argon2), e un token per dispositivo di cui il server
conserva solo l'impronta.

Dentro DecoDXLog: Impostazioni → Sync e Cloud per l'indirizzo, l'accesso e il sync
automatico; "Sincronizza adesso" anche nella barra in alto, con la coda sempre in vista.
Un giro fa prima il pull e poi il push, così le revisioni partono allineate. Le regole
sono quelle scritte in Fase 0: chi spinge dice la revisione che conosceva, **vince
l'ultima modifica** e la versione che perde resta nello storico; il pull non sovrascrive
mai una modifica locale ancora da mandare; i duplicati con un altro uuid si riconoscono
per nominativo, banda, gruppo di modi e orario vicino; le cancellazioni viaggiano come
modifiche. La password passa una volta sola: DecoDXLog tiene solo il token, nel portachiavi.

**Il log dal browser.** Sullo stesso servizio c'e' la pagina: si entra con gli stessi
nominativo e password, e si vede il proprio log — tabella con la ricerca mentre si scrive,
filtri per banda e modo, la pagina che si allunga scorrendo, la scheda del QSO con tutti i
campi ADIF, e il tasto per riscaricare tutto in ADIF. Da qui si guarda e si scarica: si
scrive dal programma. Pagine servite dal server (Jinja) con un po' di HTMX tenuto in casa,
i colori sono quelli di DecoDXLog, e la sessione e' un cookie HttpOnly che dura trenta
giorni.

Provato per davvero fra due log: 40 QSO spinti dal primo e ripresi dal secondo, una
modifica che fa il giro, un conflitto risolto con la versione perdente nello storico del
server, e una cancellazione che arriva dall'altra parte.

**Rotore.** DecoDXLog parla con **DecoRotor** sul WebSocket (8765) e, per chi ha altro, con un
**rotctld** qualsiasi (DecoRotor stesso risponde sulla 4532). Il quadrante è quello di
DecoRotor, portato dentro DecoDXLog: corona graduata con le tacche ogni 2° e i numeri ogni
10°, mappa azimutale equidistante centrata sul proprio QTH (la direzione letta sulla corona
è la rotta vera, e la distanza dal centro cresce con i chilometri), cerchi di distanza, lobo
d'antenna, bersaglio tratteggiato e ago che gira dalla parte giusta. Sta in piccolo nella
colonna di destra, e con Ctrl+R si apre **il posto di comando**: la pagina "Controllo" di
DecoRotor rifatta com'e', con i suoi colori e le sue misure — testata con le spie (control
box, rotazione, client, modello, luce), quadrante sopra e mappa satellitare sotto divisi da
una maniglia, e a destra il display con l'azimut a caratteri grandi e l'indicatore CCW/CW,
le sei memorie a tasto diretto, i passi con lo STOP al centro, PARK, l'elenco delle memorie
e il puntamento a gradi con le otto direzioni. Sotto, la striscia di stato con le tre porte
del gateway. I riquadri della mappa arrivano dal gateway stesso (che fa da cache), gli spot
sono quelli del cluster di DecoDXLog e la barra in fondo punta per locatore, rotta breve o
lunga. Ci sono anche le altre due schede dell'originale: **DIAGNOSTICA** (i frame Prosistel che
passano sulla seriale con il loro esadecimale, l'andamento della posizione, i contatori
dell'esercizio e i tre indirizzi di rete) e **IMPOSTAZIONI** (nominativo, locatore, apertura
del lobo, finecorsa, riposo, tolleranza e lo stop se cade il collegamento), che scrivono nel
config.json del gateway con `config_set`. Memorie, finecorsa, riposo e lobo li dice il
gateway: DecoDXLog li legge e li rimanda, non se li inventa. Dove la rotta si sa già la si usa: **dal menu di
uno spot del cluster** (“punta il rotore su DL9ZZT, 287°”), dal nominativo che si sta
lavorando, e, se lo si accende, seguendo da solo quello che Decodium lavora. La direzione
dell'antenna si vede anche sulla mappa. La seriale resta a DecoRotor: i finecorsa sono del
control box.

**Propagazione.** Scheda nuova in basso: SFI, macchie, indice A e K, aurora, raggi X, campo
geomagnetico, rumore e vento solare, e le condizioni banda per banda di giorno e di notte
(più aurora ed E-skip in VHF), colorate. I dati arrivano dal XML di N0NBH (hamqsl.com), da
soli ogni ora o a comando. DecoDXLog tiene un campione all'ora e lo mette accanto ai QSO di
quel giorno: negli ultimi quattordici giorni si vede se il proprio ritmo segue davvero il
flusso solare. In testa alla mappa restano SFI e K, dove si guarda la propagazione.

**Contest.** Una finestra fatta per la tastiera (Ctrl+Shift+T): si scrive il nominativo,
Invio registra, Esc pulisce, la barra passa al rapporto. Mentre si scrive si vede se è un
doppio (in questa sessione, su questa banda, in questo modo), che ritmo si tiene (QSO
degli ultimi dieci minuti e dell'ultima ora, e i QSO/h che ne verrebbero), quanti DXCC e
locatori sono entrati, e gli ultimi QSO fatti. Il numero progressivo lo mette la sessione.

**Cabrillo.** Il log del contest esce come lo vuole chi lo riceve: testata 3.0 con
categorie, locatore, punteggio dichiarato e soapbox, e una riga per QSO a colonne fisse.
Le frequenze in kHz, dai 6 metri in su il numero di banda; i modi come li vuole Cabrillo
(CW, PH, RY, DG, FM) e FT2 come digitale.

**QSL di carta.** Una finestra propria con la coda: da mandare, mandate, ricevute, e
quante aspettano risposta. Un QSO ci finisce dal menu della riga nel log o tutto insieme
con “metti in coda tutte quelle da ricambiare”. Da lì escono le **etichette in PDF**:
una per corrispondente, con dentro fino a sei QSO, perché una cartolina sola risponde a
tutti i collegamenti fatti con quella stazione. Quattro fogli in commercio (Avery L7160,
L7163, L7165 e 70 × 36 mm), segni di taglio a scelta, nessuna stampante di mezzo: il PDF
si stampa quando si vuole. La via (bureau, diretta, elettronica) resta sul QSO e torna
nell'export come `QSL_SENT_VIA`. Schema del database alla versione 3, con migrazione.

**Club Log.** Invio dei QSO a Club Log: quello appena registrato parte da solo via
realtime.php, l'arretrato parte come un unico file ADIF. Servono l'email e la password
dell'account, il nominativo del profilo stazione e una chiave API (gratuita, si chiede su
clublog.org/need_api.php) che si mette in Impostazioni → Servizi QSL. Una chiave o una
password sbagliata si legge così com'è scritta da Club Log e non viene ritentata
all'infinito; un duplicato conta come inviato.

**Statistiche** in una finestra propria: totali (QSO, nominativi, entita', locatori, primo e
ultimo QSO, giorno e ora migliori), QSO per anno, per mese, per ora UTC e per banda, modi e
continenti, e la mappa di calore banda per ora UTC — quella che dice a colpo d'occhio quando
una banda e' aperta. Filtri per modo e per anno.

**Mappa** rifatta: coste del mondo (Natural Earth, pubblico dominio, 29 kB dentro
l'eseguibile), linea grigia calcolata dalla posizione del Sole, locatori lavorati, spot del
cluster colorati per stato, la stazione e il cerchio massimo verso il nominativo scelto.
Livelli accendibili e spegnibili.

## 0.2.0 — 18 settembre 2026

**Interfaccia in italiano.** Tutte le stringhe tradotte (`translations/decodxlog_it.ts`), la
lingua segue il sistema oppure si sceglie in Impostazioni → Generale.

**QSL.**

- Conferme LoTW scaricate da `lotwreport.adi`, solo quelle nuove dall'ultimo sync, a mano o
  ogni 6/12/24 ore; abbinamento per nominativo, banda, gruppo di modi e ora entro mezz'ora.
  I dettagli di LoTW riempiono solo i campi vuoti, i nuovi DXCC confermati finiscono nel
  registro attività.
- Invio: LoTW facendo firmare un ADIF temporaneo al TQSL installato, QRZ Logbook con la
  chiave API, eQSL con utente e password. A mano o automatico dopo ogni QSO; un duplicato
  conta come inviato, un rifiuto resta scritto sul QSO con il motivo.

**DX cluster.** Nodi telnet (DX Spider, CC Cluster), Reverse Beacon Network, HamAlert e
attivazioni POTA in un elenco solo, con gli spot confrontati col log (nuovo DXCC, nuova
banda, nuovo modo, nuovo slot, già lavorato, entità non confermata, utente LoTW). Filtri
completi e salvabili, regole d'avviso con annuncio vocale, invio degli spot a Decodium e
doppio clic per sintonizzarlo. Console per i comandi al nodo e per mandare spot.

**Log.** Etichette sui QSO (`APP_DECOLOG_TAGS`), filtri per entità DXCC, stato QSL, profilo
stazione, etichetta e intervallo di date, azioni sulle righe mostrate (etichetta di gruppo,
export ADIF). Schema del database alla versione 2, con migrazione.

**Award.** Totali per banda e band slot, elenco di quello che manca (DXCC, FT2, WAZ, WAS),
mappa dei locatori lavorati e confermati, filtri per profilo stazione ed etichetta.

**Attivazioni e contest.** Sessione POTA/SOTA/WWFF/IOTA o contest: campi dell'attivatore su
ogni QSO, locatore del posto, etichetta, numero progressivo, duplicati contati dentro la
sessione, conteggi e export ADIF con il nome che POTA si aspetta.

**Confezione.** Icona propria (`resources/make_icon.py`) nell'eseguibile e nelle finestre,
versione nelle proprietà del file, `scripts/deploy.sh` che prepara anche
`DecoDXLog-<versione>-win64.zip` con LEGGIMI e strumenti di prova.

**Correzioni.** Gli errori di rete non riportano più l'URL con la password; la freccia degli
elenchi a discesa apre la tendina anche nelle caselle in cui si può scrivere.

## 0.1.0 — 17 settembre 2026

Prima versione: ricezione dei QSO da Decodium e WSJT-X via UDP, log SQLite con nomi ADIF e
storico delle revisioni, profili stazione, import/export ADIF senza perdite, logbook con
filtri, scheda del nominativo, entità DXCC dal `cty.csv` di AD1C, credenziali nel
portachiavi di sistema, callbook QRZ.com e HamQTH, award calcolati dal log, copie di
sicurezza notturne, DecoLink verso Decodium.
