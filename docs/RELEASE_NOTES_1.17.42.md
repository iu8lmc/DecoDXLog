# DecoDXLog 1.17.42

Aggiornamenti dalla **1.17.41 alla 1.17.42**.

## Italiano

### Compatibilità CAT condivisa

- La risposta CAT condivisa a `get_lock_mode` ora include la terminazione Hamlib
  `RPRT 0`, evitando che Decodium, rigctld o altri client restino in attesa di una
  risposta completa.
- Le richieste CAT non riconosciute ricevono una risposta di chiusura prima della
  disconnessione, rendendo il comportamento del bridge più prevedibile.
- La velocità CW (`KEYSPD`) viene letta sia nel formato numerico semplice sia nel
  formato etichettato e decimale `Level Value: 24.000000` prodotto da alcuni bridge
  rigctld.

### CW via CAT e seriale

- Il messaggio del keyer descrive correttamente il supporto a `send_morse` attraverso
  un CAT condiviso, quando il server master autorizza la trasmissione.
- La manipolazione diretta DTR/RTS resta disponibile per radio o configurazioni che
  non espongono un keyer CW via CAT.

### Verifica

- Aggiunti test per `get_lock_mode`, per le risposte KEYSPD con valore etichettato e
  per la lettura della velocità attraverso un CAT condiviso.
- La suite locale e il release gate devono superare tutti i test prima della
  pubblicazione degli asset multipiattaforma.

## English

### Shared CAT compatibility

- The shared CAT response to `get_lock_mode` now includes the Hamlib `RPRT 0`
  terminator, so Decodium, rigctld and other clients do not wait for an incomplete
  response.
- Unknown CAT requests receive a closing response before disconnecting, making bridge
  behavior more predictable.
- CW speed (`KEYSPD`) is now read both from the plain numeric format and from the
  labelled decimal format `Level Value: 24.000000` emitted by some rigctld bridges.

### CW over CAT and serial fallback

- The keyer message now accurately documents `send_morse` support through shared CAT
  when the master server permits transmission.
- Direct DTR/RTS keying remains available for radios or configurations without a CAT
  CW keyer.

### Verification

- Added tests for `get_lock_mode`, labelled KEYSPD replies, and speed reading through
  a shared CAT connection.
- The complete local suite and release gate must pass before publishing the
  cross-platform assets.
