# LoTW upload checkpoint

Before starting TQSL, DecoDXLog records a checkpoint in its application settings containing the original log identity and path, station location, contact identities and revisions, and a SHA-256 fingerprint of the prepared ADIF. It contains neither credentials nor QSO comments. The checkpoint blocks further LoTW uploads across logs until the batch is resolved.

If a batch remains pending, open its original log and use **Settings → QSL services** to inspect the station location and contact list. After an application crash, TQSL may still be running: wait until it stops. Check the batch on LoTW, correct local QSO statuses as appropriate, then explicitly acknowledge the reviewed batch. Acknowledgement removes the checkpoint; it does not mark contacts as sent. The operator is responsible for avoiding duplicate submissions. The application does not verify the remote outcome automatically.

A checkpoint cannot be acknowledged while the controller process holding the upload lock is running. The lock cannot prove that an orphaned TQSL process has stopped after its parent crashes. A malformed checkpoint remains blocked and currently has no guided recovery. The checkpoint uses QSettings; a process-crash test is not evidence of durability under power loss or every filesystem failure. The temporary ADIF may remain on disk after an abrupt process exit.
