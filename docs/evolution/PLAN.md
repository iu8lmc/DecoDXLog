# Desktop evolution plan

Scope: desktop Windows, macOS and Linux, and the existing cloud; no mobile client. The eleven product streams remain in scope. Status below is an initial assessment, not acceptance of any goal.

| Goal | Dependencies | Initial state / next verifiable work |
|---|---|---|
| G00 Baseline | checkout | In progress: inspect each UI, persistence and test path; build and record A/B baselines. |
| G01 Reliability | G00 | In progress: generic web contest estimate labelled; measure logging and recovery and reproduce Windows issue. |
| G02 Awards | G00, G01 | Partial: VUCC and Marathon previews and claims exist; verify official rules, multi-grid and independent entity/zone selection. |
| G03 QSL planner | G02 | Partial foundations: QSL status and routing exist; marginal award benefit and alternatives need verification/implementation. |
| G04 DX campaigns | G02 | Partial foundations: calendar and DX Assistant; persistent campaign workflow to verify. |
| G05 Migration and sync history | G01 | Partial: import preview, extras, memory-only undo and reconciliation exist; schema-13 journal and guarded undo core added. Background undo, cancellable advisory preview and bounded per-QSO outcome list are present; add exhaustive review, test cross-process membership guards and UI cancellation/crash on large logs, resolve measured fingerprint/index/trigger cost, source-specific migration and per-QSO history. |
| G06 Multi-program activation | G02, G05 | Partial: activation sessions exist; independent program references and exports remain. |
| G07 Typed fields and reports | G01, G02 | Partial: text custom fields exist; typed fields and saved reports to verify. |
| G08 Radio net manager | G01 | Local roster, session persistence, statuses, order, turns, paginated history UI, status/check-in summary, persisted visual reminder, explicit QSO draft and complete JSON export implemented. Complete persisted QSO association, broader reminder notification and operational verification. |
| G09 Satellite and rover | G02, G06, G11 contracts | Partial: manual entry stores distinct TX/RX; read-only PREDICT desktop snapshots and optional repeated refresh implemented. Next/following pass prediction is available through PREDICT; complete pass planning, source freshness, Doppler use, rover and operational verification. |
| G10 Contest | G01, G05 | Partial: scoring rules, SO2R and ContestNet exist; convergence, serial partitions and advanced workflow remain. |
| G11 SDK | G01 | Partial: authenticated local API exists; versioned permissions, isolation and adapters remain to verify. |
| G12 OCR and historical logs | G05, G07 | Partial foundation: QSL image archive exists; OCR and review workflow remain unverified. |
| G13 Integrated release candidate | all | Not started: integrated workflow and real platform package checks required. |

Next: finish G00 evidence and build, examine G05 import/undo contracts, then implement and A/B test independent Marathon entity/zone candidates without presenting them as official credit. Obtain authoritative edition rules and actual supported submission format before claiming compliance.
