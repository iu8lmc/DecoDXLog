# Progress

## 2026-11-22 — G00 / initial G01 and G02 increments

Starting revision `c8c64a842d47573192032ff866d154be01ad2f4e`, branch `evolution/desktop-roadmap`; no schema migration. Baseline and goals: see BASELINE.md and PLAN.md. G00 remains in progress: the full eleven-stream UI/persistence/test audit, performance dataset and full build result are not yet complete.

G01: `server/decolog_cloud/templates/_contest.html` now describes the web score as a generic estimate, distinct from desktop-specific rules. Existing computation is unchanged. A/B: the new page assertion failed before and passed after; web test plus analytics: 22 passed. No production deployment or visual browser check.

G02: `src/core/CqMarathon.cpp` now checks edition year in UTC. Same instant represented as 2027-01-01 00:30 +01 and 2026-12-31 23:30 UTC: before, the former was rejected; after, both select identically and export the same JSON. Three focused CTest targets pass; `tests/tst_cqmarathon.cpp` holds the fixture. No database migration; `loadEntries` already normalised timestamps. This does not implement independent zone selection or prove official compliance.

Full clean Release build succeeded with `cmake --build /tmp/decolog-evolution-build -j 4` on Apple M4, macOS arm64, Qt 6.11.2. Focused `database`, `migration`, `cqmarathon`, `cqmarathon_db`, `vucc`, `qmlpopups` CTest targets passed 6/6. This is not a clean-install or full-suite result. G05 inspection: undo tracks only IDs inserted by the last import in memory and soft-deletes those IDs; restart persistence and concurrent-QSO behaviour require separate tests. Existing migration fixtures are synthetic; real vendor exports remain needed. No latency benchmark, Windows issue reproduction, hardware, external services, package, signing or real export verified. Next: test G05 import/undo with an independently added QSO and restart, then implement tested independent zone candidates with accurate UI and explicitly preliminary status. Commits: see `git log` for this branch.
