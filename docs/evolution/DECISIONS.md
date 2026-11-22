# Decisions

- Preserve existing award and import models; add evidence and contracts incrementally. A confirmed QSO is not automatically an official award credit.
- The web contest calculation remains the existing generic model while explicitly labelled as an estimate. Shared official scoring remains open; no scoring semantics changed.
- Marathon edition matching uses UTC for all timestamp representations; database loading already converts to UTC. This prevents equivalent instants receiving different edition decisions.
- Synthetic fixtures and real exports, simulated peers and real hardware, local build and packaged releases must be recorded separately. No mobile development.
- Keep the legacy Marathon `selected` entity array and add a separate `zoneCandidates` array so existing consumers retain their entity-selection contract. Candidates are explicitly unverified; a numeric CQ zone range is not proof of a valid program entry or location.
- Require an explicit station profile for Marathon preview in the desktop UI, while preserving the selector's existing all-profiles API for other callers. This avoids silently combining profiles, but is not evidence that a profile contains one operating location. Use atomic replacement for the internal JSON export.
- Import batches must own their transaction; refusal is explicit if one is already active. Each QSO insertion uses a savepoint so a failed QSL subwrite cannot commit an unreported QSO. Committed prior batches remain available after a later failure; import progress is emitted only for committed batches. Measure savepoint overhead on representative logs.
