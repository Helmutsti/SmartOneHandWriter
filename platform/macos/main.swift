// OneHand - frontend macOS (demo, Swift + AppKit).
//
// App autonoma con un editor interno: intercetta i tasti DENTRO la propria
// finestra (nessun permesso di Accessibilita') e li passa al motore portabile
// tramite la C ABI (onehand_c.h). Mostra il testo ricostruito e il popup delle
// alternative. Tutta la logica vive nel core; qui c'e' solo la GUI.
//
// Lo scopo e' vedere subito OneHand al lavoro su macOS. Scrivere in ALTRE app
// (system-wide) arrivera' dopo e richiedera' i permessi di Accessibilita'.

import Cocoa

// Costanti dei tipi di tasto: combaciano con onehand::KeyKind / ONEHAND_KEY_*.
private let KIND_LETTER:    Int32 = 0
private let KIND_SPACE:     Int32 = 1
private let KIND_BACKSPACE: Int32 = 2
private let KIND_TAB:       Int32 = 3
private let KIND_ENTER:     Int32 = 4

// La C ABI restituisce stringhe come const wchar_t* (UTF-32 su macOS).
private func wstr(_ p: UnsafePointer<wchar_t>?) -> String {
    guard let p = p else { return "" }
    var view = String.UnicodeScalarView()
    var i = 0
    while p[i] != 0 {
        if let s = Unicode.Scalar(UInt32(bitPattern: p[i])) { view.append(s) }
        i += 1
    }
    return String(view)
}

// MARK: - vista che cattura i tasti e disegna il testo

final class OneHandView: NSView {
    private let engine: OpaquePointer
    private var buffer = ""   // testo composto, interamente guidato dal motore
    private var popup  = ""   // riga del popup (alternative / punteggiatura)
    private var timer: Timer? // timer del doppio-tap, pilotato dagli effetti

    init(engine: OpaquePointer) {
        self.engine = engine
        super.init(frame: .zero)
    }
    required init?(coder: NSCoder) { fatalError("non supportato") }

    override var isFlipped: Bool { true }            // testo dall'alto verso il basso
    override var acceptsFirstResponder: Bool { true }

    // ---- input ----
    override func keyDown(with e: NSEvent) {
        // lascia passare le scorciatoie (Cmd/Ctrl/Alt), es. Cmd+Q
        if !e.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            super.keyDown(with: e); return
        }
        guard let chars = e.charactersIgnoringModifiers,
              chars.unicodeScalars.count == 1,
              let c = chars.unicodeScalars.first else { return }

        var kind: Int32
        var letter: Int32 = 0
        switch c {
        case "a"..."z":  kind = KIND_LETTER; letter = Int32(c.value)
        case "A"..."Z":  kind = KIND_LETTER; letter = Int32(c.value) + 32   // -> minuscola
        case " ":        kind = KIND_SPACE
        case "\u{7F}":   kind = KIND_BACKSPACE                              // tasto Canc
        case "\t":       kind = KIND_TAB
        case "\r", "\n": kind = KIND_ENTER
        default:         return                                            // altri tasti: ignora
        }
        onehand_on_key(engine, kind, letter)
        applyEffects()
    }

    // ---- applica gli effetti restituiti dal motore (stesso schema del FE Windows) ----
    private func applyEffects() {
        let count = onehand_edit_count(engine)
        var i: Int32 = 0
        while i < count {
            let bs = Int(onehand_edit_backspaces(engine, i))
            if bs > 0 { buffer.removeLast(min(bs, buffer.count)) }
            buffer += wstr(onehand_edit_insert(engine, i))
            i += 1
        }
        switch onehand_timer_action(engine) {
        case 1: restartTimer(ms: Int(onehand_timer_ms(engine)))   // Start (riavvia)
        case 2: timer?.invalidate(); timer = nil                  // Cancel
        default: break                                            // None
        }
        popup = (onehand_popup_visible(engine) != 0) ? wstr(onehand_popup_text(engine)) : ""
        needsDisplay = true
    }

    private func restartTimer(ms: Int) {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: Double(ms) / 1000.0, repeats: false) { [weak self] _ in
            guard let self = self else { return }
            onehand_on_timeout(self.engine)
            self.applyEffects()
        }
    }

    @objc func clearPressed() {
        onehand_reset(engine)
        buffer = ""; popup = ""
        timer?.invalidate(); timer = nil
        needsDisplay = true
    }

    // ---- disegno ----
    override func draw(_ dirtyRect: NSRect) {
        NSColor.textBackgroundColor.setFill()
        NSBezierPath(rect: bounds).fill()

        let pad: CGFloat = 24
        let textAttr: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 30),
            .foregroundColor: NSColor.labelColor,
        ]
        let popupAttr: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 16, weight: .regular),
            .foregroundColor: NSColor.secondaryLabelColor,
        ]
        let textRect = NSRect(x: pad, y: pad,
                              width: bounds.width - 2 * pad,
                              height: bounds.height - 2 * pad - 30)
        (buffer as NSString).draw(in: textRect, withAttributes: textAttr)
        (popup as NSString).draw(at: NSPoint(x: pad, y: bounds.height - pad - 20),
                                 withAttributes: popupAttr)
    }
}

// MARK: - delegato: esci quando si chiude la finestra

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

// MARK: - avvio

private func dataDir() -> String {
    CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "data"
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)

let delegate = AppDelegate()
app.delegate = delegate

guard let engine = onehand_create() else { fatalError("impossibile creare il motore") }
let wordlist = dataDir() + "/wordlist_it.txt"
if onehand_load_wordlist_file(engine, wordlist) == 0 {
    FileHandle.standardError.write(Data("OneHand: dizionario non trovato: \(wordlist)\n".utf8))
}

let view = OneHandView(engine: engine)

let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 440),
                   styleMask: [.titled, .closable, .miniaturizable, .resizable],
                   backing: .buffered, defer: false)
win.title = "OneHand — demo (mano sinistra)"
win.center()

let content = win.contentView!
let barH: CGFloat = 44

let clearBtn = NSButton(title: "Pulisci", target: view, action: #selector(OneHandView.clearPressed))
clearBtn.bezelStyle = .rounded
clearBtn.frame = NSRect(x: 12, y: content.bounds.height - barH + 8, width: 90, height: 28)
clearBtn.autoresizingMask = [.minYMargin]

let hint = NSTextField(labelWithString: "scrivi qui — una mano sola, lo spazio è il jolly")
hint.frame = NSRect(x: 112, y: content.bounds.height - barH + 12, width: 560, height: 20)
hint.autoresizingMask = [.minYMargin, .width]
hint.textColor = .secondaryLabelColor

view.frame = NSRect(x: 0, y: 0, width: content.bounds.width, height: content.bounds.height - barH)
view.autoresizingMask = [.width, .height]

content.addSubview(view)
content.addSubview(clearBtn)
content.addSubview(hint)

// mini-menu con Esci (Cmd+Q)
let mainMenu = NSMenu()
let appItem = NSMenuItem()
mainMenu.addItem(appItem)
let appMenu = NSMenu()
appMenu.addItem(withTitle: "Esci da OneHand",
                action: #selector(NSApplication.terminate(_:)),
                keyEquivalent: "q")
appItem.submenu = appMenu
app.mainMenu = mainMenu

win.makeKeyAndOrderFront(nil)
win.makeFirstResponder(view)
app.activate(ignoringOtherApps: true)
app.run()
