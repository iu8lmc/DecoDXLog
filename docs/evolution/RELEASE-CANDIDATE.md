# Release candidate

Not prepared. Integrated acceptance across all goals, packaging, clean installs, migration, optional dependencies and Windows/macOS/Linux verification remain open. Signing/notarisation and publication require their actual identities and applicable authorisation; an ad-hoc build is not notarisation. No packages have been published.

A local macOS arm64 ad-hoc test DMG has been mounted and exercised with an isolated synthetic QSO; see `PROGRESS.md`. This is preliminary evidence only and does not prepare or approve a release candidate.

The common release gate passed locally on macOS arm64 (117 CTest groups, with individual opt-in and platform skips); see `PROGRESS.md` for limits. This does not replace clean packaged-platform acceptance.

A local macOS arm64 ad-hoc test DMG also opened a synthetic schema-29-like log and preserved the compared QSO, QSL, attachment and API data through schema 30; see `PROGRESS.md` for the fixture and limits.

A separate locally built historical 1.17.61 source revision created a synthetic schema-11 log that the macOS arm64 test DMG migrated to schema 30, including a verified legacy API replay; see `PROGRESS.md` for coverage and limitations.
