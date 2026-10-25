# DecoDXLog 1.17.37

## Italiano

Aggiornamenti dalla `1.17.36` alla `1.17.37`.

### 1.17.36

- I pannelli staccati sono diventati vere finestre indipendenti, trasferibili su monitor diversi
  dalla dashboard principale.
- Posizione, dimensione e stato dei pannelli vengono salvati e ripristinati, anche durante la
  chiusura dell'applicazione.
- Aggiunti test QML per tutti i percorsi di sgancio e persistenza multi-monitor.

### 1.17.37

- Corretto l'invio automatico combinato LoTW + eQSL: eQSL non viene piu' saltato quando LoTW e'
  ancora occupato.
- I servizi automatici vengono elaborati in sequenza, nell'ordine configurato; il completamento
  o il fallimento di LoTW non impedisce l'avvio di eQSL.
- La coda elimina duplicati e servizi disattivati durante l'invio e mantiene gli altri servizi
  automatici compatibili con la stessa gestione.
- Aggiunto un test dedicato al caso segnalato e verificata la suite completa: 76 test superati.

## English

Updates from `1.17.36` to `1.17.37`.

### 1.17.36

- Detached panels became real independent windows that can be moved to monitors different from the
  main dashboard.
- Panel position, size, and state are saved and restored, including during application shutdown.
- Added QML tests for all detachment and multi-monitor persistence routes.

### 1.17.37

- Fixed combined automatic LoTW + eQSL uploads: eQSL is no longer skipped while LoTW is busy.
- Automatic services are processed sequentially in configured order; LoTW completion or failure
  no longer prevents eQSL from starting.
- The queue removes duplicates and services disabled during an upload while preserving the same
  behavior for the other automatic services.
- Added a focused regression test for the reported case and verified the full suite: 76 tests
  passed.
