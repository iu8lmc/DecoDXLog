# DecoDXLog 1.17.65

## English

Changes since v1.17.64:

- Preview the animated QSO map directly from the callsign card before logging a contact. The route starts at the active station and identifies approximate DXCC-centre destinations.
- Reduce work after QSL uploads by refreshing affected contacts instead of the entire table. Batch status updates now use a single transaction, and freeze diagnostics distinguish incoming QSO stages.
- Automatic QRZ Logbook confirmation checks now require explicit activation in QSL settings. Existing users who want scheduled QRZ checks need to enable this option. Manual downloads remain available. QRZ authentication errors identify whether credentials belong to general settings or a station profile.
- Fix a cluster-window boolean warning and deprecated title-bar mouse handlers.
- Add tests for the map, QSL uploads, confirmation scheduling, large logs and cluster window.

The reported Windows freezes have not yet been confirmed resolved on the affected computer.

## Italiano

Novità dalla v1.17.64:

- Anteprima della mappa animata direttamente dalla scheda nominativo, prima di registrare il contatto. Il percorso parte dalla stazione attiva e segnala le destinazioni approssimate al centro DXCC.
- Ridotto il lavoro successivo agli invii QSL aggiornando i contatti interessati invece dell'intera tabella. Gli stati dei lotti vengono salvati in una transazione e la diagnostica distingue le fasi di ricezione dei QSO.
- Lo scarico automatico delle conferme QRZ Logbook richiede ora l'attivazione esplicita nei servizi QSL. Chi desidera mantenere i controlli programmati QRZ deve attivare l'opzione. Lo scarico manuale resta disponibile. Gli errori QRZ indicano se le credenziali provengono dalle impostazioni generali o da un profilo.
- Corretti un avviso booleano della finestra cluster e gli avvisi sui gestori mouse della barra del titolo.
- Aggiunti test per mappa, invii QSL, sincronizzazione delle conferme, log lunghi e finestra cluster.

La risoluzione degli stalli Windows segnalati deve ancora essere verificata sul computer interessato.
