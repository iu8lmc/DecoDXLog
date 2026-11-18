## English

DecoDXLog 1.17.59 includes the changes made since v1.17.57, including the upstream v1.17.58 alignment.

### Call information: band and mode details

- Click a worked or confirmed square in the Call Info panel to see the QSOs behind it. The callsign grid shows contacts with that station; the DXCC grid shows contacts with the entity on the selected band and mode group.
- The list distinguishes confirmed and unconfirmed contacts, identifies confirmation services and opens the existing QSO Detail window when a contact is selected. File-based logbook queries run in the background.

### Large logbooks and responsiveness

- Editing a QSO no longer resets the whole table when the visible contacts and their order are unchanged. The table still updates when a change affects sorting or filtering.
- A synthetic log of more than 20,000 contacts was used to reproduce and investigate a table rebuild stall. This change addresses the observed case; it does not establish that every reported Windows freeze has the same cause.

### Club Log download

- The import preview accepts a Club Log download when a QSO does not include the optional remote identifier or station callsign. Missing station callsigns are filled from the authenticated download request; existing identifiers are retained and newly imported contacts are marked as already uploaded.
- A conflicting station callsign or invalid supplied remote identifier is rejected, and preview errors are more specific. The operator still reviews and confirms the import; duplicates follow the existing import rules.
- Tested with a simulated service, including missing fields, invalid responses, server errors, duplicate imports and event-loop responsiveness. Interoperability for the newly handled missing-field case has not yet been verified against a live Club Log response.

### Verification

- Added coverage for band/mode detail results and confirmation status, table updates after edits, and Club Log download validation. The release workflows build and smoke-test the final packages for the supported platforms.

## Italiano

DecoDXLog 1.17.59 comprende le modifiche successive alla v1.17.57, incluso l’allineamento alla v1.17.58 upstream.

### Scheda nominativo: dettagli per banda e modo

- Cliccando una casella lavorata o confermata della scheda nominativo si apre la lista dei QSO corrispondenti. La griglia del nominativo mostra i contatti con quella stazione; la griglia DXCC mostra i contatti con l’entità sulla banda e nel gruppo di modi selezionati.
- La lista distingue contatti confermati e non confermati, indica i servizi di conferma e apre la scheda QSO esistente quando si seleziona un contatto. Le interrogazioni dei log su file avvengono in background.

### Log grandi e reattività

- La modifica di un QSO non ricostruisce più l’intera tabella quando i contatti mostrati e il loro ordine non cambiano. La tabella continua ad aggiornarsi quando cambiano ordinamento o filtri.
- Un log sintetico di oltre 20.000 contatti ha permesso di riprodurre e analizzare un blocco dovuto alla ricostruzione della tabella. La correzione affronta il caso osservato; non dimostra che tutti i blocchi segnalati su Windows abbiano la stessa causa.

### Download Club Log

- L’anteprima di importazione accetta un download Club Log anche quando un QSO non contiene l’identificativo remoto o il nominativo della stazione. Il nominativo mancante viene ricavato dalla richiesta autenticata; gli identificativi presenti sono conservati e i nuovi contatti importati vengono segnati come già inviati.
- Una stazione discordante o un identificativo remoto presente ma non valido impediscono l’importazione, con messaggi di errore più chiari. L’operatore continua a controllare e confermare l’importazione; i duplicati seguono le normali regole di importazione.
- Prove con un servizio simulato per campi mancanti, risposte non valide, errori del server, duplicati e reattività dell’interfaccia. Il nuovo caso dei campi mancanti non è ancora stato verificato su una risposta reale di Club Log.

### Verifiche

- Aggiunti test per i dettagli banda/modo e lo stato delle conferme, gli aggiornamenti della tabella dopo una modifica e la validazione del download Club Log. I workflow di rilascio compilano e verificano l’avvio dei pacchetti finali per le piattaforme supportate.
