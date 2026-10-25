# DecoDXLog 1.17.38

## Italiano

Aggiornamenti dalla **1.17.37 alla 1.17.38**.

### CW via CAT Yaesu

- Rimosso il blocco preventivo che impediva di inviare macro e testo CW alle radio Yaesu
  collegate direttamente tramite il rigctld gestito da DecoDXLog, incluso il **FTDX-3000**.
- Il pannello mostra ora un avviso informativo, senza disabilitare l'invio: **Hamlib puo'
  sovrascrivere la memoria 1 del keyer con il testo trasmesso e il contenuto precedente non
  viene ripristinato**. Salvare il contenuto della memoria prima di usare questa funzione.
- Corretti i messaggi di un solo carattere, come `?`, `K`, `1` e `5`: vengono inoltrati come
  testo CW, evitando che Hamlib li rifiuti o li interpreti come richiami di memoria.
- Le macro Yaesu oltre 50 caratteri vengono suddivise nella coda di trasmissione, preferendo
  i confini delle parole. Le parti successive attendono il tempo Morse stimato della parte
  precedente, senza lasciare che Hamlib tronchi il testo al limite della memoria.
- Conservata la precedenza dei keyer seriali DTR/RTS e WinKeyer. L'avviso segue il percorso
  effettivo del CW e non viene applicato automaticamente a una radio SO2R alternativa o a
  un rigctld esterno il cui modello non e' noto.
- Conservato il comportamento di **Ferma**: annullamento della coda locale, rilascio PTT e
  richiesta CAT di ritorno in ricezione per i collegamenti Yaesu locali configurati.
  L'arresto fisico del keyer dipende comunque dalla radio e dal backend Hamlib.
- Avviso aggiornato in italiano e inglese; implementazione condivisa da Windows, macOS e Linux.

### Configurazione del FTDX-3000

Per usare lo stesso percorso CAT del keyer interno, chiudere Decodium e scegliere in
**Impostazioni → Radio (CAT)**: collegamento seriale, modello **1037 / Yaesu FTDX-3000**, la
porta CAT corretta (COM5 nel caso segnalato), **38400 baud, 8 bit dati, 2 stop, parita' None**.
Con handshake **None**, impostare **CAT RTS = DISABLE** sulla radio; DTR e RTS possono essere
impostati su **OFF**. Selezionare PTT **Il CAT stesso (RIG)**.

**Impostare la porta del manipolatore su «nessuna», non COM8**: un keyer seriale aperto ha la
precedenza e impedisce di usare questo percorso CAT. Usare CW/CWR e BK-IN attivo; per la
memoria testuale verificare **026 CW MEMORY 1 = TEXT**, dopo averne salvato il contenuto.
Provare prima un breve `VVV` dal campo del pannello CW, a bassa potenza su carico fittizio.

### Test e distribuzione

- Aggiunte regressioni per abilitazione del CW Yaesu, instradamento SO2R/rete, caratteri
  singoli, suddivisione delle macro e annullamento dei messaggi in coda con Ferma.
- Verifica locale macOS: build completata, **76/76 test superati**, controllo della versione
  documentata e smoke test di avvio/chiusura riusciti con impostazioni e database isolati.
- Il workflow Windows preleva ora il tag richiesto, anziche' `main`, allineando la provenienza
  del codice ai pacchetti macOS e Linux. Conservata la procedura installer MSYS2/MinGW.
- Pacchetti previsti: DMG macOS Apple Silicon e Intel, installer Windows x64, AppImage Linux
  x86_64 e aarch64, con manifest firmati per l'autoaggiornamento del fork elisir80.
- I runner eseguono il gate di test e lo smoke test del pacchetto prima dell'upload. Su Windows
  resta l'esclusione preesistente del test `udppipeline`; gli altri test restano attivi.
- I test software non sostituiscono il collaudo RF sul FTDX-3000. Un errore come
  `The remote host closed the connection` richiede una diagnosi separata di rigctld/seriale.
- I DMG mantengono la firma ad-hoc del bundle: **non** sono notarizzati Apple. Le firme di
  autoaggiornamento sono distinte dai certificati Apple Developer ID / Windows Authenticode.

## English

Changes from **1.17.37 to 1.17.38**.

### Yaesu CAT CW

- Removed the preventive block that stopped CW macros and text from reaching Yaesu radios
  connected directly through DecoDXLog's managed rigctld, including the **FTDX-3000**.
- The panel now displays a non-blocking warning: **Hamlib may overwrite keyer memory 1 with
  the transmitted text, and its previous contents are not restored**. Back up that memory
  before using this feature.
- Fixed single-character messages such as `?`, `K`, `1`, and `5`: they are forwarded as
  literal CW text instead of being rejected or interpreted as memory recall commands.
- Yaesu macros longer than 50 characters are split into queued chunks, preferring word
  boundaries. Each chunk waits for the estimated Morse duration of the previous one,
  preventing Hamlib from silently truncating the message at the memory limit.
- Serial DTR/RTS keyers and WinKeyer retain priority. The warning follows the actual CW
  route and is not automatically applied to an alternate SO2R radio or an external rigctld
  whose radio model is unknown.
- **Stop** retains local queue cancellation, PTT release, and the CAT receive request for
  configured local Yaesu connections. Physical keyer interruption still depends on the
  radio and its Hamlib backend.
- Updated Italian and English warning text; shared implementation across Windows, macOS,
  and Linux.

### FTDX-3000 setup

To use the internal CAT keyer route, close Decodium and select in **Settings → Radio (CAT)**:
serial connection, model **1037 / Yaesu FTDX-3000**, the correct CAT port (COM5 in the reported
case), **38400 baud, 8 data bits, 2 stop bits, parity None**. With handshake **None**, set
**CAT RTS = DISABLE** on the radio; DTR and RTS can be set to **OFF**. Select **CAT itself
(RIG)** for PTT.

**Set the serial keyer port to “none”, not COM8**: an open serial keyer takes precedence
over CAT. Use CW/CWR with BK-IN enabled; verify **026 CW MEMORY 1 = TEXT**, after backing up
its contents. Start with a short `VVV` from the CW panel's text field, at low power into a
dummy load.

### Tests and distribution

- Added regressions for Yaesu CW enablement, SO2R/network routing, single characters,
  long-message splitting, and cancellation of queued messages with Stop.
- Local macOS validation: successful build, **76/76 tests passed**, documentation/version
  checks, and successful startup/shutdown smoke test with isolated settings and database.
- The Windows workflow now checks out the requested tag rather than `main`, matching the
  source provenance of macOS and Linux packages. The MSYS2/MinGW installer procedure is retained.
- Target packages: Apple Silicon and Intel macOS DMGs, Windows x64 installer, Linux x86_64
  and aarch64 AppImages, with signed automatic-update manifests for the elisir80 fork.
- Runners execute the test gate and final-package smoke test before upload. Windows retains
  the existing `udppipeline` test exclusion; other tests remain enabled.
- Software tests do not replace an RF check on the actual FTDX-3000. Errors such as
  `The remote host closed the connection` need a separate rigctld/serial diagnosis.
- DMG app bundles retain ad-hoc signing and are **not** Apple-notarized. Automatic-update
  signatures are separate from Apple Developer ID / Windows Authenticode certificates.
