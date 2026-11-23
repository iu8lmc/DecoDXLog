# ContestNet QSO convergence foundations

The existing ContestNet QSO path remains the legacy UDP path. The schema-28 change journal and `ContestNetInventory` are local foundations and do not currently transmit or apply QSO revisions.

An inventory opens an existing schema-28-or-newer file read-only and starts one SQLite read transaction. Its log identity and journal watermark are obtained from that snapshot; pages enumerate QSO ID, UUID, revision and soft-deleted state in ID order, including pre-migration records and tombstones. Pages are limited to 500 rows. Closing or destroying the inventory releases the transaction. The object and its SQLite connection must remain on their creating thread.

A future reconciliation can compare that inventory, then inspect journal entries after its watermark. Journal entries are only local metadata; there is no QSO payload, peer acknowledgement, ordering contract, conflict policy, checkpoint or implemented replay. UUID adoption and Cloud revision updates can generate entries, and a QSO revision is not necessarily monotonic across Cloud operations. Equality of UUID and revision alone does not prove equal content. The sequence and log identity cannot prove continuity across a restored or copied database without additional binding and validation.

The inventory checks the file identity captured on open before and after each page, and stops if the path is replaced. Capturing the token from the path after opening SQLite is not atomic with the SQLite open; in-place changes, file-ID reuse and network-filesystem semantics are not covered. Keep read transactions brief: in WAL mode a long snapshot can retain WAL pages, while rollback-journal mode can block writers. The current API has no asynchronous UI integration, automatic deadline or resumable snapshot after process restart.

See [PROGRESS.md](PROGRESS.md) for the tests actually run and remaining LAN, conflict and cross-platform work.
