# Accessibility and first-configuration audit

This is a source-level baseline for DXL-031, not a substitute for a user
usability session or an assistive-technology certification.

The focused check was run on 2026-10-16 with
`cmake --build build --target tst_qmlpopups decolog` followed by
`ctest --test-dir build -R '^qmlpopups$'`; result: 1/1 passed.

## Verified

- The main window exposes keyboard entry points for new QSO, search,
  import/export, cluster, activation, contest and rotor.
- The New QSO dialog accepts `Ctrl+Enter` and `Ctrl+Return` when Enter-to-log is enabled in the New QSO field settings.
- Popup windows containing editable fields are checked for a window-level focus
  declaration by `tst_qmlpopups`.
- The Panels menu exposes explicit Beginner, DX, Digital, Contest and Portable
  operational presets. They are opt-in; applying one resets only the panel
  arrangement and returns detached panels to the main window.
- Escape/Enter handling exists in the main startup badge and several data-entry
  dialogs.
- The first-run guide gives its three actions explicit accessible names and
  descriptions, starts focus on “Configure radio”, and defines a closed Tab
  cycle through Configure radio, Open New QSO and Close.

## Not yet implemented or externally verified

- A real user session has not yet verified the first-run guide on all desktop
  platforms.
- Complete tab-order and keyboard-only traversal of every detached panel.
- Screen-reader announcements, high-contrast verification and a real user
  usability session on macOS, Windows and Linux.

The static checks must remain green, but these remaining items require a
separate design and runtime/accessibility test plan before implementation.

## Verifica ripetuta — 16 ottobre 2026

```text
cmake --build build --parallel 4 --target tst_qmlpopups decolog
ctest --test-dir build --output-on-failure -R '^qmlpopups$'
1/1 passed
```

La build e il controllo statico della guida iniziale, dei preset e degli
ingressi da tastiera restano verdi. Non viene dichiarata certificazione di
accessibilità: servono ancora prove con utenti e tecnologie assistive reali.
