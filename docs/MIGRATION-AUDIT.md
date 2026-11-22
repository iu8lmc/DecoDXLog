# Logbook migration audit

The import path accepts ADIF plus the documented CSV/TSV and N1MM conversions.
Before confirmation, the import dialog reports the source kind, records read,
estimated new records, duplicates and rejected records, incomplete UTC, QSL
status-bearing records, and preserved unmapped fields. The estimate is made in
a private in-memory copy of the current log; the real database is not touched.

The committed import report also records inserted IDs, duplicates, invalid
records, incomplete UTC, QSL-bearing records and unmapped fields. Imports are
performed in batches and a per-log journal records the identity and baseline of
each new QSO. The File menu offers undo of the latest recorded import, after
confirmation. The operation runs in the background and moves unchanged QSO to
recycle/history; modified, missing and older entries without a baseline are
left for review. Cancellation stops between QSO and a retry can continue.
Imports made before this journal cannot be recovered through this action.

Verified locally:

- ADIF round-trip preserves mapped and extra fields.
- QSL states and UTC/mode fields survive import/export.
- Duplicate detection works across batches.
- `ctest -R 'migration|database|qmlpopups'` passes.

The before/after estimate is intentionally advisory: the final report remains
authoritative because another local QSO can arrive between preview and import.

Verification on 2026-10-15: the focused migration and import-preview tests
passed (`migration` 1/1 and `qmlpopups` 1/1). The tests cover Italian and
English CSV, N1MM SQLite, ADIF round-trip, UTC/mode normalization, QSL states,
duplicate handling, preview counts and undo through the inserted-ID path.
Real exports from Log4OM, DXKeeper or other third-party versions were not
available in this checkout, so their vendor-specific presets remain unclaimed.

The repository now includes `tests/fixtures/migration_matrix.json`, a clearly
labelled synthetic/anonymized matrix for Log4OM and DXKeeper ADIF-shaped data
plus a Log4OM-style CSV spreadsheet row. The test dispatches each fixture to
the matching ADIF or CSV parser and checks the same preservation contract.
The matrix validator requires each case to declare its source, fixture version,
format, evidence class and fields that must survive. It verifies preservation
of UTC, band, mode, RST, grid, QSL and vendor extra fields through the parser.
This is a regression fixture, not evidence from a real vendor export;
real-export validation remains an explicit follow-up. N1MM SQLite coverage is
kept in the dedicated migration test and is likewise synthetic.
