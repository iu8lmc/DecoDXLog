# DecoDXLog 1.17.08 / Release notes

## Italiano

Questa versione raccoglie gli aggiornamenti dalla 1.17.04 alla 1.17.08.

- **CW in ricezione affidabile:** l'ingresso viene aperto nel formato nativo realmente negoziato e
  convertito esplicitamente in mono signed 16-bit, con downmix corretto per le periferiche stereo.
  Il pannello mostra formato, frequenza e canali effettivi; i formati non convertibili sono rifiutati
  con un messaggio esplicito invece di produrre decodifiche errate.
- **Tono e velocita' del decoder:** oltre alla ricerca automatica, si possono bloccare tono (300–1500
  Hz) e velocita' ricevuta (5–59 WPM) senza modificare il keyer in trasmissione. Il cambio pulisce
  l'analisi precedente e lo stato distingue segnale agganciato da rumore.
- **Macro CW su Mac:** fn/Globe+F1–F12 continua a richiamare le macro; con tastiere Mac senza fn e'
  disponibile anche Ctrl+F1–F12.
- **Pannelli stabili:** Band map, mappa, chat e rete contest non eseguono piu' callback QML dopo che
  una vista e' stata chiusa, eliminando l'errore di contesto QML durante il distacco o la chiusura.
- **DX Cluster:** si possono inviare spot dal Nuovo QSO, da Decodium e dal cluster, con controllo di
  nominativo, banda, commento e doppioni recenti.
- **Correzione QSO:** nominativo e campi manuali del log sono correggibili dalla scheda o dalla riga;
  il cambio di nominativo aggiorna i dati derivati e rimette in coda i servizi non confermati.
- **CW/CAT di sicurezza:** Ferma annulla il CW in coda e forza PTT OFF sulla radio interessata;
  il CW diretto resta protetto sulle Yaesu per cui Hamlib sovrascriverebbe la memoria 1 del keyer.

## English

This release collects the changes from 1.17.04 through 1.17.08.

- **Reliable CW reception:** audio is opened in the actually negotiated native format and explicitly
  converted to signed 16-bit mono, including correct stereo downmixing. The panel shows the real
  format, sample rate and channel count; unsupported formats are rejected with an explicit message
  instead of generating corrupt decodes.
- **Decoder tone and speed:** alongside automatic search, the receive tone (300–1500 Hz) and received
  speed (5–59 WPM) can be locked without changing the transmit keyer. Changing either resets the
  previous analysis, and the status now distinguishes a valid lock from noise.
- **CW macros on macOS:** fn/Globe+F1–F12 continues to trigger macros; Ctrl+F1–F12 is also available
  for Mac keyboards without an fn key.
- **Stable panels:** Band map, map, chat and contest network no longer execute QML callbacks after a
  view has been closed, removing the invalid-context error during detach or close operations.
- **DX Cluster:** spots can be sent from New QSO, Decodium and the cluster, with callsign, band,
  comment and recent-duplicate validation.
- **QSO correction:** callsigns and manually entered log fields can be corrected from the detail card
  or directly in the row; changing a callsign refreshes derived data and requeues unconfirmed services.
- **Safe CW/CAT:** Stop cancels queued CW and forces PTT OFF on the affected radio; direct CW remains
  protected for Yaesu rigs where Hamlib would overwrite keyer memory 1.
