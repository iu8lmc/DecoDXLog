# Decisions

- Preserve existing award and import models; add evidence and contracts incrementally. A confirmed QSO is not automatically an official award credit.
- The web contest calculation remains the existing generic model while explicitly labelled as an estimate. Shared official scoring remains open; no scoring semantics changed.
- Marathon edition matching uses UTC for all timestamp representations; database loading already converts to UTC. This prevents equivalent instants receiving different edition decisions.
- Synthetic fixtures and real exports, simulated peers and real hardware, local build and packaged releases must be recorded separately. No mobile development.
- Keep the legacy Marathon `selected` entity array and add a separate `zoneCandidates` array so existing consumers retain their entity-selection contract. Candidates are explicitly unverified; a numeric CQ zone range is not proof of a valid program entry or location.
