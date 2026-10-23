# DecoDXLog

[![Release](https://img.shields.io/github/v/release/iu8lmc/DecoDXLog?label=release)](https://github.com/iu8lmc/DecoDXLog/releases)
[![Licenza: GPL-3.0](https://img.shields.io/badge/licenza-GPL--3.0-blue.svg)](LICENSE)
[![Linguaggio: C++20](https://img.shields.io/badge/linguaggio-C%2B%2B20-00599C.svg?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Interfaccia: Qt 6 · QML](https://img.shields.io/badge/interfaccia-Qt%206%20%C2%B7%20QML-41cd52.svg?logo=qt&logoColor=white)](https://www.qt.io/)
[![Autore: Martino Merola IU8LMC](https://img.shields.io/badge/autore-Martino%20Merola%20IU8LMC-orange.svg)](https://github.com/iu8lmc)
[![Sviluppatore: Salvatore Raccampo 9H1SR](https://img.shields.io/badge/sviluppatore-Salvatore%20Raccampo%209H1SR-orange.svg)](https://github.com/elisir80)
![Supporto e social media manager: Filippo Ricci G0YCE](https://img.shields.io/badge/supporto%20%C2%B7%20social%20media-Filippo%20Ricci%20G0YCE-8a2be2.svg)
[![Offrimi un caffè](https://img.shields.io/badge/%E2%98%95%20offrimi%20un%20caff%C3%A8-buymeacoffee.com%2Fiu8lmc-FFDD00.svg?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/iu8lmc)

**Autore e sviluppatore:** Martino Merola IU8LMC · **Sviluppatore:** Salvatore Raccampo 9H1SR ·
**Supporto e social media manager:** Filippo Ricci G0YCE.

Se DecoDXLog ti è utile, [☕ offrimi un caffè](https://buymeacoffee.com/iu8lmc).

Il log di stazione della famiglia Decodium. Offline-first, SQLite, stesso tema e
stessa grammatica di finestre di Decodium e DecoRTTY. FT2 è un modo di prima
classe (`MODE=MFSK`, `SUBMODE=FT2`).

Specifica di partenza: [`docs/FASE0_SPEC.md`](docs/FASE0_SPEC.md).
Licenza: GPL-3.0. Dentro `libs/ggmorse` c'e' ggmorse di Georgi Gerganov, licenza MIT,
con la sua `LICENSE` accanto.

## Stato: 1.17.37

La versione e le funzioni descritte qui corrispondono al manifest di build
`CMakeLists.txt` (1.17.37). Le note incrementali sono in
[`docs/CHANGELOG.md`](docs/CHANGELOG.md); le opzioni sperimentali o opzionali
sono indicate esplicitamente nelle sezioni relative.

Dalla 0.3.1: il QSO appena scritto **si completa da solo** con quello che sa il callbook
(nome, QTH, locatore, indirizzo), diplomi nuovi — **WAC**, **WAAC**, **WAJA**, **AJD**,
**JCC**, **JCG** e il **DXCC Challenge** —, le statistiche mostrano tutte le bande, nel log
si scelgono piu' righe insieme e si cancellano, e il Cloud conta gli stessi diplomi.

Dalla 0.3.0: il Cloud porta **tutto il log**, non solo i QSO — profili stazione e tutte le
impostazioni, e le **password dei servizi** cifrate sul proprio computer (AES-256-GCM, chiave
dalla password del Cloud: il server vede solo byte); il log dal browser e' **la stessa finestra**
del programma, con i colori della propria stazione, sei schede e la **frequenza in aria** che
arriva da casa; e i nominativi non hanno piu' una lunghezza minima.

Dalla 0.2.0: **statistiche** in una finestra propria (anni, mesi, ore UTC, bande, modi,
continenti e la mappa di calore banda per ora) e **mappa** del mondo con coste, linea
grigia, locatori e spot; **Club Log**; **QSL di carta** con la coda e le etichette in PDF;
**contest** da tastiera con export **Cabrillo**; **propagazione** (numeri del Sole e
condizioni banda) con lo storico accanto ai propri QSO; **rotore**, con il posto di
comando di DecoRotor dentro DecoDXLog e i gradi presi dagli spot del cluster; e
**DecoDXLog Cloud**, il sync fra dispositivi, con il proprio log anche dal browser.
Elenco completo delle funzioni (italiano e inglese): [`docs/FEATURES.md`](docs/FEATURES.md).
Storia: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

L'interfaccia segue i mockup "DecoDXLog Mockups" (finestra principale 1a, Nuovo QSO 1b,
scheda QSO 1c, profili stazione 1d, Setup 1e) nei tre temi di Decodium.

Funziona:

- **Ricezione da Decodium / WSJT-X / JTDX** sul protocollo UDP (porta 2237 di
  default, multicast opzionale). `LoggedADIF` è la fonte primaria; `QSOLogged`
  si usa solo se l'ADIF non arriva entro 1,5 s (oppure come fonte scelta in Setup).
- **Recupero dal log di Decodium**: ogni 5 minuti DecoDXLog legge la coda di
  `decodium_log.adi` e salva i QSO che non ha (DecoDXLog chiuso, porta sbagliata,
  rete giù). Quelli corretti o cancellati qui, o finiti in un altro log dell'elenco,
  non tornano.
- **Log SQLite** con lo schema di `db/schema.sql`: nomi ADIF, campi di sync,
  stati QSL per servizio in `qsl_status`, campi non mappati in `adif_extra`.
- **Modifica QSO** con storico: ogni salvataggio è una nuova revisione, la
  precedente va in `qso_history` e si può ripristinare; cancellazione morbida.
- **Profili stazione** (`station_profile`), profilo attivo per i nuovi QSO; il primo
  si crea da solo dal nominativo e locatore che Decodium manda nello Status.
- **Duplicati** configurabili (predefiniti ±2 min digitali, ±10 min a mano).
- **Import/Export ADIF** senza perdite, anche degli stati QSL (verificato dai test).
- **Logbook** con filtri a pillole (banda, modo, mese o intervallo di date, entità DXCC,
  stato QSL, profilo stazione, etichetta, ricerca), filtri salvati, colonne
  nascondibili, finestra separata ("Pop"), colonne QSL L Q C E. Azioni sulle righe
  mostrate: aggiungere o togliere un'etichetta, esportare in ADIF.
- **Etichette** sui QSO (attivazione, contest, evento, portatile) in
  `APP_DECOLOG_TAGS`: si scrivono nel Nuovo QSO e nella scheda, tornano nell'export.
- **Call info** dal log: worked-before, distanza e azimut dal locatore, ora locale
  approssimata, stato QSL dell'ultimo QSO. **FT2 Award** (DXCC e locatori in FT2,
  conferme LoTW), riepilogo QSL, mappa dei locatori.
- **Statistiche** in una finestra propria: totali, QSO per anno, mese, ora UTC e banda,
  modi e continenti, e la mappa di calore banda per ora che dice quando una banda è
  aperta; filtri per modo e per anno.
- **Mappa** con le coste del mondo (Natural Earth, dentro l'eseguibile), linea grigia
  dalla posizione del Sole, locatori lavorati, spot del cluster colorati per stato e
  cerchio massimo verso il nominativo scelto; livelli accendibili e spegnibili.
- **Entità DXCC** dal nominativo con il `cty.csv` di AD1C (incluso, aggiornabile da
  Setup): DXCC, paese, zone e continente sui QSO da Decodium e manuali, "NEW DXCC" e
  "NEW DXCC on <banda>" mentre si lavora, completamento dei QSO già nel log.
- **Credenziali** di Cloud, QRZ.com, QRZ Logbook, LoTW, Club Log, eQSL e HamQTH nel
  portachiavi di sistema (qtkeychain): nel file delle impostazioni resta solo il nome
  utente. Senza qtkeychain i segreti non si salvano affatto.
- **Callbook** QRZ.com (XML) o HamQTH: nome, QTH, locatore, zone, foto, utente LoTW/eQSL
  in Call info; riempie i campi vuoti del Nuovo QSO. Sessione rinnovata da sola,
  risultati tenuti in memoria per un giorno.
- **Award** calcolati dal log: DXCC, FT2 Award, WAZ, WAS, WPX, locatori, IOTA, POTA,
  SOTA, WWFF. Lavorati e confermati per banda con i totali per banda e i band slot,
  elenco di quello che manca (DXCC, FT2, WAZ, WAS), mappa dei locatori; filtri per
  banda, gruppo di modi, profilo stazione ed etichetta; conferme accettate a scelta
  (LoTW, cartolina, eQSL).
- **Attivazioni e contest** (Ctrl+T): una sessione POTA/SOTA/WWFF/IOTA o un contest mette su
  ogni QSO i campi dell'attivatore (`MY_SIG`, `MY_SIG_INFO`, `MY_SOTA_REF`, `MY_IOTA`), il
  locatore del posto, un'etichetta e il numero progressivo (`STX`); il numero ricevuto si
  scrive nel pannello Nuovo QSO. Dentro la sessione un nominativo rilavorato sulla stessa
  banda e modo è un duplicato, a qualunque ora. Conteggi in tempo reale, quanto manca per
  validare l'attivazione (10 QSO POTA, 4 SOTA), QSO per banda e modo, ed export ADIF con il
  nome che POTA si aspetta (`IU8LMC@IT-1234-20260917.adi`). La sessione sta nel log e
  sopravvive alla chiusura del programma.
- **Invio QSL**: LoTW facendo firmare un ADIF temporaneo al TQSL installato (il certificato
  resta dov'è), QRZ Logbook con la chiave API ed eQSL con utente e password. A mano o
  automatico dopo ogni QSO; i duplicati contano come inviati, i rifiuti restano scritti sul
  QSO con il motivo. Conteggi e pulsanti nella scheda "Invio QSL".
- **Conferme LoTW** scaricate da `lotwreport.adi` (solo quelle nuove dall'ultimo sync,
  a mano o ogni 6/12/24 ore) e abbinate ai QSO per nominativo, banda, gruppo di modi e
  ora entro 30 minuti; i dettagli LoTW riempiono i campi vuoti, i nuovi DXCC confermati
  finiscono nel registro attività.
- **Conferme eQSL e QRZ Logbook** (le eQSL ricevute nella casella, i QSO confermati del
  logbook QRZ) scaricate a mano o ogni 6/12/24 ore, abbinate come quelle LoTW.
- **DX Cluster** in una finestra propria (Ctrl+K) e nella scheda in basso: nodi DX Spider/
  CC Cluster via telnet (DecoDXLog entra come CALL-2), Reverse Beacon Network (CW/RTTY e
  FT8/FT4), HamAlert (password nel portachiavi) e attivazioni POTA, tutto in una lista.
  Ogni spot e' confrontato col log: NEW DXCC, NEW BAND, NEW MODE, NEW SLOT, gia' lavorato,
  entita' non confermata, utente LoTW; distanza e azimut; referenze POTA/SOTA/WWFF/IOTA dal
  commento. Filtri per banda, modo, stato, continente del DX e dello spotter, fonte,
  nominativi con jolly, SNR degli skimmer, eta'; filtri salvati. Regole d'avviso con
  annuncio vocale (voci di sistema, italiano o inglese, alfabeto fonetico). Gli spot e gli
  avvisi vanno a Decodium con DecoLink; doppio clic su uno spot sintonizza Decodium.
  Console per i comandi al nodo e per mandare spot. `decodxlog_clusterprobe` prova una fonte
  da riga di comando.
- **Backup** notturno con `VACUUM INTO`, copie a rotazione.
- **Tema**: Ocean Blue / Stellar Light / Darkcodium, variant d'accento, densità,
  colori personalizzati.

- **Interfaccia in italiano** (`translations/decodxlog_it.ts`): segue la lingua del sistema,
  o si sceglie in Impostazioni → Generale.

- **DecoDXLog Cloud** (`server/`): il sync fra dispositivi. Non solo i QSO: anche i profili
  stazione e tutte le impostazioni — tema, filtri, cluster, porte, percorsi — così il
  secondo computer si ritrova la stessa stazione. Anche le password dei servizi, ma
  sigillate sul proprio computer (AES-256-GCM, chiave dalla password del Cloud): il server
  vede solo byte che senza quella password non si aprono. Il proprio log si guarda e si
  scarica anche dal browser.

## Struttura

```
libs/decodium-ui/   tema e componenti comuni (candidato a modulo condiviso)
libs/ggmorse/       il decodificatore Morse di G. Gerganov (MIT), copiato qui
src/core/           ADIF, bande, database, protocollo UDP — senza GUI
src/app/            controller, modello della tabella e dei profili
qml/DecoDXLog/        finestra principale
db/schema.sql       schema SQLite v1
resources/cty/      cty.csv di AD1C (country-files.com) e la sua licenza
tests/              Qt Test: adif, protocol, database
tools/udpsend.cpp   finge di essere Decodium, per provare senza radio
scripts/deploy.sh   prepara la cartella distribuibile
```

## Compilare (Windows, macOS e Linux)

Su macOS e Linux, con Qt 6.5 o successivo installato:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Su macOS l'avvio dell'app compilata è:

```sh
./build/DecoDXLog.app/Contents/MacOS/DecoDXLog
```

Su Linux:

```sh
./build/DecoDXLog
```

Su Windows, con MSYS2 MinGW64:

```sh
export PATH=/c/msys64/mingw64/bin:$PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

Richiede Qt ≥ 6.5 con Quick, QuickControls2, Sql (driver QSQLITE), Network, Test.
Per le credenziali locali: `pacman -S mingw-w64-x86_64-qtkeychain` (facoltativo: senza,
DecoDXLog si compila ma non salva password). Per la cassaforte delle credenziali che deve
viaggiare col Cloud serve anche OpenSSL 3: `pacman -S mingw-w64-x86_64-openssl`.
I pacchetti ufficiali non chiedono all'utente di installarlo: le DLL OpenSSL sono incluse
nel setup Windows, le dylib nel DMG macOS e `libcrypto`/`libssl` nell'AppImage Linux.
Se CMake conserva un percorso Homebrew non piu' esistente dopo un aggiornamento, lo
scarta automaticamente; su macOS si puo' forzare il controllo completo con:

```sh
brew install openssl@3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDECODXLOG_REQUIRE_OPENSSL=ON \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
```

Il build macOS firma automaticamente in modo ad-hoc l'app locale, evitando il
`Code Signature Invalid` di dyld se il linker o un deploy precedente aveva lasciato
una firma non piu' valida. Il workflow di release rifirma il bundle dopo `macdeployqt`.

Le impostazioni Cloud (tema, filtri, profili e dati sincronizzabili) funzionano anche senza
la cassaforte; senza OpenSSL restano invece sul dispositivo le password/API key dei servizi.
Per VOACAP: `pacman -S mingw-w64-x86_64-gcc-fortran` (facoltativo: senza, la previsione sul
percorso resta quella semplificata); il sorgente e' in `third_party/voacapl`.

## Rendering GPU/CPU

Il rendering Qt Quick segue la stessa selezione di Decodium 4. `auto` usa Metal su macOS,
D3D11 su Windows (dalla 1.16.32; D3D12 si chiede con `--graphics d3d12`) e OpenGL su Linux: su GPU Linux datate è il percorso più compatibile con
Qt Quick e con la mappa. Vulkan resta disponibile come scelta esplicita quando il driver è
stabile. Se serve, si può usare anche il renderer CPU:

```sh
./build/DecoDXLog.app/Contents/MacOS/DecoDXLog --graphics auto
./build/DecoDXLog.app/Contents/MacOS/DecoDXLog --graphics opengl
./build/DecoDXLog.app/Contents/MacOS/DecoDXLog --graphics software
```

Sono disponibili anche `--disable-gpu`, `--software-renderer` e `--safe-graphics`.
La modalità completa e il fallback automatico sono descritti in [docs/GRAPHICS.md](docs/GRAPHICS.md).

## Cartella distribuibile (Windows)

```sh
scripts/deploy.sh      # compila e prepara dist/: DecoDXLog.exe, Qt, QML, TLS, SQLite, qtkeychain
                       # e l'archivio DecoDXLog-<versione>-win64.zip
BUILD=$PWD/build-dev scripts/deploy.sh    # se build/ e' in uso da un DecoDXLog aperto
NO_ZIP=1 scripts/deploy.sh                # solo la cartella
```

La cartella si avvia con doppio clic anche senza MSYS2: dentro ci sono anche
`decodxlog_udpsend.exe` (finge di essere Decodium), `decodxlog_clusterprobe.exe` (prova una
fonte di spot), l'icona e un `LEGGIMI.txt` con i primi passi. La CI la produce a ogni push
come artefatto `decodxlog-windows-x64`.

## Icona

L'icona sta in `resources/icon/`: il disegno (`decolog-icon.svg`, e `decolog-icon-small.svg`
senza il nome per le taglie minute) e le taglie gia' esportate da 16 a 1024 pixel.
`resources/make_icon.py` non disegna niente — mette insieme le taglie in
`resources/decodxlog.ico` (che l'eseguibile prende dal `.rc`, insieme al numero di versione)
e copia la 256 in `resources/decodxlog.png` (la finestra e i dialoghi, risorsa Qt). Si
rilancia solo quando l'icona cambia:

```sh
python resources/make_icon.py
```

## Provare senza radio

```sh
./build/DecoDXLog.exe --db prova.sqlite --port 22370
./build/decodxlog_udpsend.exe --port 22370                       # QSO FT2 come Decodium
./build/decodxlog_udpsend.exe --port 22370 --call K1AB --mode FT8 --freq 7074000 --only-qsologged

# Le impostazioni di prova per conto loro, senza toccare quelle vere:
./build/DecoDXLog.exe --settings /tmp/prova --db prova.sqlite --port 0
```

Per le schermate e le prove dell'interfaccia: `--import file.adi` importa all'avvio,
`--theme "Stellar Light"` sceglie il tema, `--show new|qso:<id>|profiles|setup:<pagina>`
apre una finestra di dialogo (anche `menu:columns|filters|saved|row`, `tab:<n>`, `pop`),
`--grab file.png` salva la schermata e chiude. Con `QT_QPA_PLATFORM=offscreen` la finestra non
compare sul desktop.

In Decodium: impostare il server UDP sull'indirizzo e la porta di DecoDXLog.
