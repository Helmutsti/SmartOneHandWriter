// Entry point del frontend macOS. Crea l'NSApplication, installa il controller come
// delegate e avvia la run loop (equivalente di wWinMain + il loop dei messaggi).

import AppKit

let app = NSApplication.shared
let controller = AppController()
app.delegate = controller
// App con icona nel Dock: il pannello di comando è la superficie principale.
// Per nasconderla dal Dock, impostare LSUIElement in Info.plist e .accessory qui.
app.setActivationPolicy(.regular)
app.run()
