# DecoDXLog v1.17.48

## English

Changes since upstream v1.17.47:

- **Map layers:** fixed layer menu indicators and toggling so hidden layers can be shown again, including when the map is detached.
- **Day/night map:** added a persistent night-shading intensity control in the Layers menu of the full renderer. The compatible renderer does not currently draw the night layer.
- **Manual QSO entry:** failed CAT connection attempts no longer replace a manually selected mode and frequency with stale values when both the radio and Decodium are disconnected. Live connected sources continue to update the panel.
- **Tests:** added QML regression coverage for map layers and for manual tuning with disconnected and connected sources.

The CAT fix has been tested with simulated sources on macOS; verification with Windows, OmniRig and physical radios is still needed.

## Italiano

Novità rispetto alla v1.17.47 di upstream:

- **Livelli della mappa:** corretti gli indicatori e i comandi del menu, così i livelli nascosti possono essere riattivati anche nella mappa staccata.
- **Mappa giorno/notte:** aggiunto nel menu Livelli un controllo persistente dell'intensità della zona notte nel renderer completo. Il renderer compatibile attualmente non disegna il livello notte.
- **Inserimento manuale QSO:** se radio e Decodium sono scollegati, i tentativi CAT falliti non sostituiscono più modo e frequenza inseriti a mano con valori precedenti. Le sorgenti realmente connesse continuano ad aggiornare il pannello.
- **Test:** aggiunte verifiche QML dei livelli e della sintonia manuale con sorgenti connesse e scollegate.

La correzione CAT è stata verificata con sorgenti simulate su macOS; resta da provarla su Windows con OmniRig e radio fisiche.
