# Frontend macOS (Swift + AppKit)

Assistente di digitazione a una mano per macOS: adattatore di solo I/O sopra il
**MOTORE** condiviso (`core/src/motore`, C++), a cui parla tramite la **C ABI**
`motore_c` (`core/include/motore/motore_c.h`). Replica tutte le funzionalità del
frontend Windows (`app/windows/main.cpp`).

## Cosa fa

- **Assistente di sistema**: intercetta i tasti in qualunque app tramite un
  `CGEventTap` globale e, con **Write**, incolla il testo composto nel campo attivo.
- **Pannello di comando**: un bottone per funzione + Play/Pause + toggle Modalità.
  I bottoni si abilitano/disabilitano in base alle azioni valide (Availability) e
  mostrano tra parentesi il tasto che le invoca.
- **Overlay**: box senza bordi, sempre in primo piano, trascinabile, che mostra il
  buffer con la parola selezionata/aperta evidenziate e una riga di suggerimenti
  cliccabile. Non ruba il focus (`NSPanel .nonactivatingPanel`).
- **Tre modalità**: Assistita (T9), Classica (lettere dirette), Multi-tap
  (scorrimento lettere: ripremi lo stesso tasto per ciclare le lettere del gruppo).
- **Binding tasto→funzione** letti da `data/tasti.conf` (lo stesso file di Windows;
  i nomi di tasto sono tradotti nei keycode macOS), con default di fabbrica.

## Requisiti

- macOS 11+ con **Xcode Command Line Tools** (`swiftc`, `cmake`).
- Permesso **Accessibilità** (Impostazioni di Sistema › Privacy e sicurezza ›
  Accessibilità): necessario perché `CGEventTap` possa intercettare i tasti. L'app
  lo richiede all'avvio; abilitala nell'elenco e premi Play.

## Build

```sh
app/macos/build_macos.sh
```

Produce `build/SmartOneHandWriter.app`. Avvialo con `open build/SmartOneHandWriter.app`.

Per firmare con un'identità stabile (consigliato: l'Accessibilità è legata alla
firma):

```sh
CODESIGN_ID="Developer ID Application: Nome (TEAMID)" app/macos/build_macos.sh
```

Per la distribuzione fuori Mac App Store, notarizzare l'`.app` con
`xcrun notarytool submit`. Nota: un event tap che assorbe tasti **non** è compatibile
con la sandbox del Mac App Store.

## Struttura

| File | Ruolo (analogo in `app/windows/main.cpp`) |
|---|---|
| `Sources/main.swift` | entry point (`wWinMain` + loop messaggi) |
| `Sources/AppController.swift` | hub: `handleKeyDown`, `performAction`, multi-tap, `refreshOverlay`, Play/Pause |
| `Sources/EventTap.swift` | `CGEventTap` (hook `WH_KEYBOARD_LL`) |
| `Sources/Bindings.swift` | keycode macOS + parser `tasti.conf` (`loadBindings`/`defaultBindings`) |
| `Sources/Overlay.swift` | `NSPanel`+`NSView` (overlay, `paintOverlay`) |
| `Sources/Panel.swift` | `NSWindow`+`NSButton` (`createPanel`/`updateButtonStates`) |
| `Sources/Clipboard.swift` | `NSPasteboard` + Cmd+V sintetico (`doRead`/`doWrite`) |
| `Sources/Permissions.swift` | permesso Accessibilità (nessun equivalente Windows) |
| `Sources/Motore.swift` | wrapper Swift della C ABI `motore_c` |
| `CMotore/` | module map per importare la C ABI in Swift |
