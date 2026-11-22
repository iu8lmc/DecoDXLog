# Capability evidence

| Capability | Implementation state | Local proof | macOS / Windows / Linux | Hardware / external |
|---|---|---|---|---|
| Marathon year UTC selection | Increment implemented | A/B unit test passed | macOS arm64 tested; Windows/Linux untested | No hardware; official rule compliance and submission not verified |
| Generic web contest labelling | Increment implemented | A/B local HTTP test passed | macOS local server; other systems untested | No hardware; production deployment not performed |
| VUCC preview | Partial, candidate-only implementation | A/B boundary-grid, ADIF and offscreen controller tests passed | macOS arm64 only | Official eligibility, confirmation sources, location rules and submission remain open |
| Full desktop roadmap | In progress | Not established | Not established | See PLAN and TEST-MATRIX |
| Radio-net roster, QSO draft and JSON export | Partial local desktop implementation | Synthetic SQLite export/cancellation and programmatic offscreen UI/restart passed | macOS arm64 tested; Windows/Linux untested | No RF or live multi-computer proof; saved-QSO association, timers and external interoperability remain open |

| Satellite manual split entry | Increment implemented, not tracker integration | Offscreen isolated app persists and exports synthetic TX 145.9 MHz / RX 435.25 MHz QSO; simulated connected UI invokes no tuning | macOS arm64 / Qt 6.11.2 only | No real tracker, Doppler, pass prediction, CAT or RF proof |

| PREDICT desktop snapshot | Partial, read-only | Offscreen app and local UDP fixture check active/stopped modes and old-TLE warning; actual PREDICT 2.2.5 returns OSCAR-7 sample with 2018 TLE | macOS arm64 only | No continuous tracking, current orbit validation, CAT/rotor, radio or RF proof |

- G09 rover: local session and position history, manual QSO `MY_GRIDSQUARE`, and historical browsing are available. Automatic attribution for imported, contest and received contacts, cross-process synchronization and official award eligibility remain open. See [rover guide](../ROVER.md).
