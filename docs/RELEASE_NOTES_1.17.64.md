# DecoDXLog 1.17.64

## English

Changes since v1.17.63 focus on preventing service connections when required credentials are missing:

- QRZ Logbook and eQSL confirmation downloads now reject missing or whitespace-only credentials before opening a network connection. The activity log announces a download only after credentials have been obtained and checked.
- LoTW confirmation downloads also check for a username and password before connecting.
- Upload requests to QRZ Logbook, eQSL, HRDLog, CRX and Wavelog now reject missing credentials; CRX and Wavelog account-list requests are guarded too. Automatic QSL operations check the saved credential state before and after reading the system keystore.
- QRZ and HamQTH callbook lookups check whether credentials are still configured before using a session, and do not sign in with blank usernames or passwords.
- Added local mock-server tests for missing credentials and retained tests for configured services, including a selected upload in a 20,000-contact synthetic log.

QRZ Logbook credentials can be stored separately in the general settings and in individual station profiles. An HTTP 403 means the server refused a request; it does not by itself establish why a particular user's request was made or refused. The reported Windows freeze and live third-party interoperability have not been confirmed resolved by these tests.

## Italiano

Le modifiche rispetto alla v1.17.63 si concentrano sulla prevenzione delle connessioni ai servizi quando mancano le credenziali necessarie:

- Gli scarichi delle conferme QRZ Logbook ed eQSL rifiutano credenziali assenti o composte solo da spazi prima di aprire una connessione. Il registro segnala l'avvio dello scarico soltanto dopo aver ottenuto e verificato le credenziali.
- Anche lo scarico delle conferme LoTW verifica nome utente e password prima di collegarsi.
- Gli invii verso QRZ Logbook, eQSL, HRDLog, CRX e Wavelog rifiutano le credenziali mancanti; sono protette anche le richieste degli elenchi CRX e Wavelog. Le operazioni QSL automatiche verificano lo stato delle credenziali prima e dopo la lettura dal portachiavi di sistema.
- Le ricerche nel callbook QRZ e HamQTH verificano che le credenziali siano ancora configurate prima di usare una sessione e non effettuano l'accesso con nome utente o password vuoti.
- Aggiunti test con server simulati locali per le credenziali mancanti, mantenendo le prove dei servizi configurati, incluso un invio selezionato in un log sintetico di 20.000 QSO.

Le credenziali QRZ Logbook possono essere salvate separatamente nelle impostazioni generali e nei singoli profili di stazione. Un HTTP 403 indica che il server ha rifiutato la richiesta; da solo non chiarisce perché sia stata avviata o rifiutata per un determinato utente. Questi test non confermano la soluzione dello stallo Windows segnalato né l'interoperabilità con i servizi reali.
