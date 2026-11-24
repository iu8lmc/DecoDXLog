# Desktop release status

DecoDXLog [1.17.67](https://github.com/elisir80/DecodxLog/releases/tag/v1.17.67) is published as a normal release, with Windows x64, Linux x86-64 and ARM64, and macOS Intel and Apple Silicon installers. The published version is a snapshot at tag `v1.17.67`; later commits on the desktop roadmap branch are not thereby included in that release.

The complete desktop roadmap has not been accepted or packaged as a finished deliverable. Integrated acceptance across all goals, clean installs, migrations, optional dependencies and Windows/macOS/Linux verification remain open. Signing and notarisation require their actual identities and applicable authorisation; an ad-hoc build is not notarisation.

A local macOS arm64 ad-hoc test DMG has been mounted and exercised with an isolated synthetic QSO; see `PROGRESS.md`. This is preliminary evidence only and does not establish completion of the full roadmap.

The common release gate passed locally on macOS arm64 (117 CTest groups, with individual opt-in and platform skips); see `PROGRESS.md` for limits. This does not replace clean packaged-platform acceptance.

A local macOS arm64 ad-hoc test DMG also opened a synthetic schema-29-like log and preserved the compared QSO, QSL, attachment and API data through schema 30; see `PROGRESS.md` for the fixture and limits.

A separate locally built historical 1.17.61 source revision created a synthetic schema-11 log that the macOS arm64 test DMG migrated to schema 30, including a verified legacy API replay; see `PROGRESS.md` for coverage and limitations.
