# Test matrix

| Requirement | A/B evidence | Environment | Remaining |
|---|---|---|---|
| G01 generic web score labelled | A: `PYTHONPATH=server python3 -m pytest server/tests/test_web.py::test_the_contest_page_counts_the_score -q` failed on missing label; B: same test plus `server/tests/test_analytics.py`, 22 passed. Two synthetic QSO; calculation retained. | macOS arm64, local Python test client | Browser rendering and shared desktop/web rules not verified. |
| G02 Marathon UTC year | A: `tst_cqmarathon editionUsesUtcAcrossYearBoundary` failed: local-offset timestamp excluded though same UTC instant belongs to 2026. B: `QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/decolog-evolution-build -R '^(cqmarathon|cqmarathon_db|vucc)$' --output-on-failure` passed 3/3 including equivalent ISO instants and JSON comparison. | macOS arm64, Qt 6.11.2, synthetic entries | Official edition rules, zone selection, submission format, UI and cross-platform proof remain open. |
| G05 import/undo | `ctest -R` focused database/migration/qmlpopups passed; no before/after change | macOS arm64 | Synthetic concurrent import/undo and anonymised real exports for each source. |
| G00 full desktop | Clean Release build passed | macOS arm64 | Apple M4, macOS arm64, Qt 6.11.2; Windows/Linux, clean install and hardware remain open. |

No A/B test can be inferred from static inspection. Record fixture, before/after result, commands and platform for every subsequent increment.
| G02 independent CQ zone candidates | A: same-entity zone fixture failed, 0 instead of 2. B: focused unit fixture and SQLite loading test pass, four CTest targets pass. | macOS arm64, Qt 6.11.2; synthetic QSOs | Official program lists, operating location, category, UI runtime, organiser submission and other platforms remain open. |
| G02 mode category grouping | A: AM and USB phone fixture returned 0; B: phone, CW-R and FT8 cases pass. | macOS arm64, Qt 6.11.2 | Unknown modes, edition-specific eligibility, UI runtime and other platforms remain open. |
| G02 station scope | Synthetic two-profile SQLite comparison: all=2, chosen=1; exported JSON has profile ID. | macOS arm64, Qt 6.11.2 | Controller/QML A/B runtime, file error paths and physical location confirmation remain open. |
| G01/G05 import commit and item atomicity | A: injected commit failure reported 1 inserted; injected QSL failure left 1 unreported QSO. B: both fixtures pass; later batch failure retains committed IDs and an independently inserted QSO; retry and file reopen pass. | macOS arm64, Qt 6.11.2, synthetic SQLite triggers; database/migration/qmlpopups 3/3 | Large-log A/B latency, actual crash/recovery, Windows/Linux and durable undo remain open. |
