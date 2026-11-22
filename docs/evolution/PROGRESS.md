# Progress

## 2026-11-22 — G00 / initial G01 and G02 increments

Starting revision `c8c64a842d47573192032ff866d154be01ad2f4e`, branch `evolution/desktop-roadmap`; no schema migration. Baseline and goals: see BASELINE.md and PLAN.md. G00 remains in progress: the full eleven-stream UI/persistence/test audit, performance dataset and full build result are not yet complete.

G01: `server/decolog_cloud/templates/_contest.html` now describes the web score as a generic estimate, distinct from desktop-specific rules. Existing computation is unchanged. A/B: the new page assertion failed before and passed after; web test plus analytics: 22 passed. No production deployment or visual browser check.

G02: `src/core/CqMarathon.cpp` now checks edition year in UTC. Same instant represented as 2027-01-01 00:30 +01 and 2026-12-31 23:30 UTC: before, the former was rejected; after, both select identically and export the same JSON. Three focused CTest targets pass; `tests/tst_cqmarathon.cpp` holds the fixture. No database migration; `loadEntries` already normalised timestamps. This does not implement independent zone selection or prove official compliance.

Full clean Release build succeeded with `cmake --build /tmp/decolog-evolution-build -j 4` on Apple M4, macOS arm64, Qt 6.11.2. Focused `database`, `migration`, `cqmarathon`, `cqmarathon_db`, `vucc`, `qmlpopups` CTest targets passed 6/6. This is not a clean-install or full-suite result. G05 inspection: undo tracks only IDs inserted by the last import in memory and soft-deletes those IDs; restart persistence and concurrent-QSO behaviour require separate tests. Existing migration fixtures are synthetic; real vendor exports remain needed. No latency benchmark, Windows issue reproduction, hardware, external services, package, signing or real export verified. Next: test G05 import/undo with an independently added QSO and restart, then implement tested independent zone candidates with accurate UI and explicitly preliminary status. Commits: see `git log` for this branch.

## 2026-11-22 — G02 preliminary independent CQ zone candidates

The original selector discarded every later QSO once its entity limit was reached, even when the later QSO supplied a new CQ zone. It now retains a separate deterministic preliminary zone candidate list, checks zone values 1–40, and keeps the existing entity `selected` JSON array. A zone-only candidate is not reported as wholly excluded. The local JSON adds `zoneCandidates` and `officialRulesVerified: false`; the Awards preview shows entity and zone candidate counts. No schema migration and no change to existing QSO data. English and Italian strings added; other languages fall back to English pending translation.

Official 2026 rules checked 2026-11-22: https://www.dxmarathon.com/rules/2026/ — one point per distinct country and zone, one QSO can count for both, single operating location; program country and zone lists are authoritative. The selector still does not validate those lists, location, all bands/modes, classes, or official submission format. Counts are candidates, not score or official credit. JSON remains an internal format.

A/B: `tst_cqmarathon selectsZonesIndependentlyOfEntity` before the change failed (0 zone candidates versus expected 2); after the change it passes. Fixture includes two zones in the same entity, duplicate zone, missing and out-of-range zone, and checks unchanged entity selection. A separate in-memory SQLite integration test loads two CQ zones for one entity. `cmake --build /tmp/decolog-evolution-build --target tst_cqmarathon tst_cqmarathon_db tst_qmlpopups decolog -j 4` passed; `QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/decolog-evolution-build -R '^(cqmarathon|cqmarathon_db|vucc|qmlpopups)$' --output-on-failure` passed 4/4. macOS arm64 / Qt 6.11.2. UI rendering, real submission, other platforms and full rules verification remain open.

Next: make scope/location explicit in the UI and evaluate authoritative entity/zone lists and accepted submission format; separately test import undo with intervening local QSO and restart. Do not mark G02 complete.
