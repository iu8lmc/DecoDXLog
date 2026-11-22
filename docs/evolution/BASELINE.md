# Baseline — 2026-11-22

Checkout `/Volumes/Projects/decolog`, branch `evolution/desktop-roadmap`; starting HEAD `c8c64a842d47573192032ff866d154be01ad2f4e`, tag v1.17.66. Working tree clean before work. Exact match to the document's baseline; zero commit difference. Existing build caches point to `/Users/salvo/Desktop/decolog` and were not reused. Fresh Release Ninja build configured in `/tmp/decolog-evolution-build` using Homebrew Qt on macOS arm64.

Initial code inspection: Marathon selects by entity, loads QSO and scope from SQLite, supplies controller preview and JSON export to AwardsDialog; it does not independently select CQ zones. VUCC reads a single gridsquare, confirmed QSL services and profile/site from SQLite and explicitly reports `officialRulesVerified=false`. Tests exist for both. Web contest score is a configurable generic model; desktop has separate contest rules. These are partial observations, not a complete audit of all eleven streams; see PLAN and existing audits in docs/.

Baseline local tests: `QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/decolog-evolution-build -R '^(cqmarathon|cqmarathon_db|vucc)$' --output-on-failure` passed 3/3. No production data or real services used. Platform, physical hardware, live service interoperability, latency, migration and release package claims are not established by these tests.
