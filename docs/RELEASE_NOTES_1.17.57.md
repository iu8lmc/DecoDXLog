## English

DecoDXLog 1.17.57 includes the following changes since 1.17.56.

### DX Cluster and CAT

- Selecting an FT8, FT4 or FT2 spot now requests USB from the radio. Some Hamlib radio drivers interpret the previous PKTUSB request as a dedicated RTTY mode.
- The spot retains its FT mode in the log and DecoLink messages. RTTY, CW and voice spot handling remains unchanged.
- Some radios use different audio inputs in USB and USB-DATA. Check the radio's audio input configuration when using FT modes.

### Freeze diagnostics

- Added timing for QSO detail, enrichment and QSL manager reads and for the QSO detail refresh following an edit.
- Slow QSO detail phases are included in the activity log and session support report to help diagnose stalls. A reported slow operation is evidence for investigation, not a confirmed diagnosis.

### Release checks

- Improved the timing of the Club Log download responsiveness test for slower runners. The test still checks that the interface event loop advances during simulated successful and failed downloads.

## Italiano

DecoDXLog 1.17.57 comprende le seguenti modifiche rispetto alla 1.17.56.

### DX Cluster e CAT

- Selezionando uno spot FT8, FT4 o FT2, il programma richiede alla radio il modo USB. Alcuni driver Hamlib interpretano la precedente richiesta PKTUSB come il modo RTTY dedicato.
- Lo spot mantiene il modo FT nel log e nei messaggi DecoLink. La gestione degli spot RTTY, CW e fonia resta invariata.
- Alcune radio usano ingressi audio diversi in USB e USB-DATA. Per i modi FT verifica la configurazione audio della radio.

### Diagnostica dei blocchi

- Aggiunte misurazioni per la lettura della scheda QSO, degli arricchimenti e del manager QSL, e per l'aggiornamento della scheda dopo una modifica.
- Le fasi lente della scheda QSO compaiono nel registro attività e nel rapporto di supporto della sessione. Un'operazione lenta segnalata è un indizio da approfondire, non una diagnosi definitiva.

### Controlli di rilascio

- Migliorata la temporizzazione del test di reattività del download Club Log sui runner più lenti. Il test continua a verificare che l'interfaccia risponda durante download simulati riusciti e con errore.
