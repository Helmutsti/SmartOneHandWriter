// OneHand - frontend macOS (Swift + AppKit).
//
// App autonoma con un editor interno: intercetta i tasti DENTRO la propria
// finestra (nessun permesso di Accessibilita') e li passa al motore portabile
// tramite la C ABI (onehand_c.h). Tutta la logica di composizione vive nel
// core, in core/; qui c'e' solo la GUI e la gestione dei tasti fisici.
//
// Allineato al frontend Windows: stesso schema "tasti funzione" (Wildcard,
// Rolling, Canc. lettera, Canc. parola, Accetta sugg.) riassegnabili a
// qualunque tasto fisico con pressione singola o doppia, stesso config.json,
// stesso pulsante Play/Stop. La differenza resta lo scope: qui si scrive solo
// nella finestra dell'app (system-wide, come su Windows, richiedera' in
// futuro il permesso di Accessibilita').

import Cocoa

// Messaggi diagnostici su stderr (es. quale dizionario viene caricato).
private func dbg(_ s: String) { FileHandle.standardError.write(Data((s + "\n").utf8)) }

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

// ------------------------------------------------------------------ azioni
// Mirror di ONEHAND_ACTION_* (core/include/onehand/onehand_c.h). Valori
// espliciti anziche' i simboli importati dall'enum C anonimo, per evitare
// ambiguita' di tipo nel bridging.
private let ACTION_LETTER:      Int32 = 0
private let ACTION_WILDCARD:    Int32 = 1
private let ACTION_ACCEPT:      Int32 = 2
private let ACTION_ROLLING:     Int32 = 3
private let ACTION_DELETE_CHAR: Int32 = 4
private let ACTION_DELETE_WORD: Int32 = 5
private let ACTION_FINALIZE:    Int32 = 6

// ------------------------------------------------------------------ tasti funzione
// Stesso schema del frontend Windows: il core esegue "azioni"; il frontend
// decide quale tasto fisico e quale pressione (singola/doppia) le attiva.
private let FN_COUNT = 5
private let FN_LABEL       = ["Wildcard", "Rolling", "Canc. lettera", "Canc. parola", "Accetta sugg."]
private let FN_CFGKEY      = ["wildcard_key", "rolling_key", "delete_char_key", "delete_word_key", "accept_key"]
private let FN_MODEKEY     = ["wildcard_mode", "rolling_mode", "delete_char_mode", "delete_word_mode", "accept_mode"]
private let FN_DEFAULT_TOKEN = ["Space", "Tab", "Backspace", "Backspace", "Space"]
private let FN_DEFAULT_MODE: [Int] = [0, 0, 0, 1, 1]   // 0 = singola, 1 = doppia
private let FN_ACTION: [Int32] = [ACTION_WILDCARD, ACTION_ROLLING, ACTION_DELETE_CHAR, ACTION_DELETE_WORD, ACTION_ACCEPT]

// Preset dei tasti per una mano (layout QWERTY), come su Windows.
private let PRESET_LEFT  = "qwertasdfgzxcvb"
private let PRESET_RIGHT = "yuiophjklnm"

// ------------------------------------------------------------------ tasti fisici (macOS)
// Virtual keyCode ANSI/US standard (indipendenti dal layout per le posizioni
// fisiche). Il token salvato in config.json e' lo stesso vocabolario di
// Windows ("Space", "Tab", "Enter", "Backspace", singola lettera/cifra), cosi'
// il file resta leggibile su entrambe le piattaforme.
private let MAC_LETTER_CODE: [Character: UInt16] = [
    "a": 0x00, "b": 0x0B, "c": 0x08, "d": 0x02, "e": 0x0E, "f": 0x03, "g": 0x05,
    "h": 0x04, "i": 0x22, "j": 0x26, "k": 0x28, "l": 0x25, "m": 0x2E, "n": 0x2D,
    "o": 0x1F, "p": 0x23, "q": 0x0C, "r": 0x0F, "s": 0x01, "t": 0x11, "u": 0x20,
    "v": 0x09, "w": 0x0D, "x": 0x07, "y": 0x10, "z": 0x06,
]
private let MAC_DIGIT_CODE: [Character: UInt16] = [
    "0": 0x1D, "1": 0x12, "2": 0x13, "3": 0x14, "4": 0x15,
    "5": 0x17, "6": 0x16, "7": 0x1A, "8": 0x1C, "9": 0x19,
]
private let vkSpace: UInt16  = 0x31
private let vkTab: UInt16    = 0x30
private let vkReturn: UInt16 = 0x24
private let vkDelete: UInt16 = 0x33   // Backspace fisico sulla tastiera Mac
private let vkEscape: UInt16 = 0x35

private func keyCodeToToken(_ code: UInt16) -> String {
    if let ch = MAC_LETTER_CODE.first(where: { $0.value == code })?.key { return String(ch).uppercased() }
    if let ch = MAC_DIGIT_CODE.first(where: { $0.value == code })?.key { return String(ch) }
    switch code {
    case vkSpace:  return "Space"
    case vkTab:    return "Tab"
    case vkReturn: return "Enter"
    case vkDelete: return "Backspace"
    default:       return "VK\(code)"
    }
}

private func tokenToKeyCode(_ token: String, fallback: UInt16) -> UInt16 {
    if token.count == 1, let ch = token.lowercased().first {
        if let c = MAC_LETTER_CODE[ch] { return c }
        if let c = MAC_DIGIT_CODE[ch] { return c }
    }
    switch token {
    case "Space":     return vkSpace
    case "Tab":       return vkTab
    case "Enter":     return vkReturn
    case "Backspace": return vkDelete
    default:
        if token.hasPrefix("VK"), let n = UInt16(token.dropFirst(2)) { return n }
        return fallback
    }
}

// Nome leggibile del tasto per l'interfaccia.
private func keyDisplayName(_ code: UInt16) -> String {
    if let ch = MAC_LETTER_CODE.first(where: { $0.value == code })?.key { return String(ch).uppercased() }
    if let ch = MAC_DIGIT_CODE.first(where: { $0.value == code })?.key { return String(ch) }
    switch code {
    case vkSpace:  return "Barra spaziatrice"
    case vkTab:    return "Tab"
    case vkReturn: return "Invio"
    case vkDelete: return "Backspace"
    case vkEscape: return "Esc"
    default:       return "Tasto \(code)"
    }
}

// ------------------------------------------------------------------ JSON tollerante
// Stessa logica (a livello di carattere) di findVal/readJsonString/
// replaceJsonString del frontend Windows: preserva struttura e campi non
// esposti dalla UI, invece di ri-serializzare l'intero file.
private func indexOf(_ chars: [Character], of pattern: [Character], from start: Int = 0) -> Int? {
    if pattern.isEmpty || start >= chars.count { return pattern.isEmpty ? start : nil }
    let last = chars.count - pattern.count
    if last < start { return nil }
    var i = start
    while i <= last {
        var matched = true
        for j in 0..<pattern.count where chars[i + j] != pattern[j] { matched = false; break }
        if matched { return i }
        i += 1
    }
    return nil
}

private func findStringValueRange(_ chars: [Character], key: String) -> (start: Int, end: Int)? {
    guard let k = indexOf(chars, of: Array("\"\(key)\"")) else { return nil }
    guard let c = indexOf(chars, of: [":"], from: k) else { return nil }
    var i = c + 1
    while i < chars.count, chars[i] == " " || chars[i] == "\t" || chars[i] == "\n" || chars[i] == "\r" { i += 1 }
    guard i < chars.count, chars[i] == "\"" else { return nil }
    guard let e = indexOf(chars, of: ["\""], from: i + 1) else { return nil }
    return (i + 1, e)
}

private func readJsonString(_ chars: [Character], _ key: String) -> String {
    guard let r = findStringValueRange(chars, key: key) else { return "" }
    return String(chars[r.start..<r.end])
}

private func replaceJsonString(_ chars: [Character], _ key: String, _ val: String) -> [Character] {
    guard let r = findStringValueRange(chars, key: key) else { return chars }
    var out = chars
    out.replaceSubrange(r.start..<r.end, with: Array(val))
    return out
}

private func upsertJsonString(_ chars: [Character], _ key: String, _ val: String) -> [Character] {
    if findStringValueRange(chars, key: key) != nil { return replaceJsonString(chars, key, val) }
    guard let b = indexOf(chars, of: ["{"]) else { return chars }
    var out = chars
    out.insert(contentsOf: Array("\n  \"\(key)\": \"\(val)\","), at: b + 1)
    return out
}

// ------------------------------------------------------------------ percorsi dati
private func candidateDataDirs() -> [String] {
    let fm = FileManager.default
    var dirs = [fm.currentDirectoryPath + "/data", fm.currentDirectoryPath]
    if let exe = Bundle.main.executablePath {
        let dir = (exe as NSString).deletingLastPathComponent
        dirs.append(dir + "/data")
        dirs.append(dir)
        dirs.append(dir + "/../data")
    }
    return dirs
}

// Cerca wordlist_it.txt (o il nome dato) in piu' posti, cosi' funziona
// comunque tu lo lanci: argomento esplicito, cartella dati, o accanto
// all'eseguibile.
private func resolveWordlist(_ name: String) -> String {
    let fm = FileManager.default
    var tries: [String] = []
    if CommandLine.arguments.count > 1 {
        let a = CommandLine.arguments[1]
        tries.append(a)
        tries.append(a + "/" + name)
    }
    for dir in candidateDataDirs() { tries.append(dir + "/" + name) }
    tries.append(name)
    for t in tries where fm.fileExists(atPath: t) {
        dbg("dizionario trovato in: \(t)")
        return t
    }
    dbg("dizionario NON trovato. Provati: \(tries)")
    return name
}

private func resolveConfigPath() -> String? {
    let fm = FileManager.default
    for dir in candidateDataDirs() {
        let p = dir + "/config.json"
        if fm.fileExists(atPath: p) { return p }
    }
    return nil
}

private func listWordlistNames(current: String) -> [String] {
    var names = Set<String>()
    let fm = FileManager.default
    for dir in candidateDataDirs() {
        if let items = try? fm.contentsOfDirectory(atPath: dir) {
            for f in items where f.lowercased().hasSuffix(".txt") { names.insert(f) }
        }
    }
    names.insert(current)
    return names.sorted()
}

// MARK: - vista che cattura i tasti e disegna il testo

final class OneHandView: NSView {
    weak var controller: AppController?
    var buffer = ""   // testo composto, interamente guidato dal motore (o dalla digitazione normale a Stop)
    var popup  = ""   // riga del popup (alternative / punteggiatura)

    init(controller: AppController) {
        self.controller = controller
        super.init(frame: .zero)
    }
    required init?(coder: NSCoder) { fatalError("non supportato") }

    override var isFlipped: Bool { true }            // testo dall'alto verso il basso
    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with e: NSEvent) {
        // lascia passare le scorciatoie (Cmd/Ctrl/Alt), es. Cmd+Q
        if !e.modifierFlags.intersection([.command, .control, .option]).isEmpty {
            super.keyDown(with: e); return
        }
        controller?.handleKeyEvent(e)
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.textBackgroundColor.setFill()
        NSBezierPath(rect: bounds).fill()

        let pad: CGFloat = 20
        let textAttr: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 26),
            .foregroundColor: NSColor.labelColor,
        ]
        let popupAttr: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedSystemFont(ofSize: 15, weight: .regular),
            .foregroundColor: NSColor.secondaryLabelColor,
        ]
        let textRect = NSRect(x: pad, y: pad,
                              width: bounds.width - 2 * pad,
                              height: bounds.height - 2 * pad - 24)
        (buffer as NSString).draw(in: textRect, withAttributes: textAttr)
        (popup as NSString).draw(at: NSPoint(x: pad, y: bounds.height - pad - 18),
                                 withAttributes: popupAttr)
    }
}

// Vista invisibile usata solo per catturare "il prossimo tasto premuto"
// quando si clicca "Assegna" — stesso ruolo dell'hook temporaneo di cattura
// su Windows, ma qui basta spostare il focus della tastiera per un istante.
final class CaptureView: NSView {
    var onKey: ((UInt16) -> Void)?
    override var acceptsFirstResponder: Bool { true }
    override func keyDown(with e: NSEvent) {
        onKey?(e.keyCode == vkEscape ? AppController.escSentinel : e.keyCode)
    }
}

// MARK: - controller: pannello di configurazione + instradamento dei tasti

final class AppController: NSObject, NSTextFieldDelegate {
    static let escSentinel: UInt16 = 0xFFFF

    let engine: OpaquePointer
    var window: NSWindow!
    var view: OneHandView!
    let captureView = CaptureView(frame: NSRect(x: -20, y: -20, width: 1, height: 1))

    var handPopup: NSPopUpButton!
    var keysField: NSTextField!
    var wordlistPopup: NSPopUpButton!
    var fnFld: [NSTextField] = []
    var fnBtn: [NSButton] = []
    var playBtn: NSButton!

    var cfgPath = "config.json"
    var cfgText = ""
    var cfgAvailableKeys = PRESET_LEFT
    var cfgWordlistName = "wordlist_it.txt"
    var doublePressMs = 280
    var suppressHandChange = false

    var fnVk       = [UInt16](repeating: 0, count: FN_COUNT)   // attivi (usati dal routing)
    var fnMode     = [Int](repeating: 0, count: FN_COUNT)
    var fnPend     = [UInt16](repeating: 0, count: FN_COUNT)   // in modifica nella UI, applicati al Salva
    var fnModePend = [Int](repeating: 0, count: FN_COUNT)

    var capturing: Int? = nil

    // stato "attivo" (Play/Stop): a Stop i tasti si comportano normalmente
    // (digitazione letterale nel foglio), a Play li interpreta il motore.
    var active = false

    // doppia pressione: la rilevazione e' del frontend (stesso schema di Windows).
    var pending = false
    var pendingCode: UInt16 = 0
    var pendingSingleFn: Int? = nil
    var pendingTimer: Timer? = nil

    init(engine: OpaquePointer) {
        self.engine = engine
        super.init()
    }

    // ---------------------------------------------------------------- avvio
    func start() {
        cfgPath = resolveConfigPath() ?? "config.json"
        cfgText = (try? String(contentsOfFile: cfgPath, encoding: .utf8)) ?? ""

        onehand_apply_config_json(engine, cfgText)
        cfgAvailableKeys = wstr(onehand_config_available_keys(engine))
        cfgWordlistName  = wstr(onehand_config_wordlist_name(engine))
        doublePressMs    = Int(onehand_config_double_press_ms(engine))

        let chars = Array(cfgText)
        for i in 0..<FN_COUNT {
            let tok = readJsonString(chars, FN_CFGKEY[i])
            let defCode = tokenToKeyCode(FN_DEFAULT_TOKEN[i], fallback: 0)
            fnVk[i] = tok.isEmpty ? defCode : tokenToKeyCode(tok, fallback: defCode)
            let m = readJsonString(chars, FN_MODEKEY[i])
            fnMode[i] = (m == "double") ? 1 : (m == "single") ? 0 : FN_DEFAULT_MODE[i]
            fnPend[i] = fnVk[i]
            fnModePend[i] = fnMode[i]
        }

        let wlPath = resolveWordlist(cfgWordlistName)
        let loaded = onehand_load_wordlist_file(engine, wlPath)
        dbg("dizionario: \(wlPath) caricato=\(loaded)")
        if loaded == 0 {
            FileHandle.standardError.write(Data("OneHand: dizionario non trovato: \(cfgWordlistName)\n".utf8))
        }

        buildUI()
    }

    // ---------------------------------------------------------------- UI
    func buildUI() {
        let W: CGFloat = 520, H: CGFloat = 760
        func topY(_ top: CGFloat, _ h: CGFloat) -> CGFloat { H - top - h }

        let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: W, height: H),
                            styleMask: [.titled, .closable, .miniaturizable],
                            backing: .buffered, defer: false)
        win.title = "OneHand"
        win.center()
        window = win
        let content = win.contentView!

        func addLabel(_ text: String, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat) -> NSTextField {
            let l = NSTextField(labelWithString: text)
            l.frame = NSRect(x: x, y: y, width: w, height: h)
            content.addSubview(l)
            return l
        }
        func addPopup(x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat) -> NSPopUpButton {
            let p = NSPopUpButton(frame: NSRect(x: x, y: y, width: w, height: h), pullsDown: false)
            content.addSubview(p)
            return p
        }
        func addField(x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat, editable: Bool) -> NSTextField {
            let f = NSTextField(frame: NSRect(x: x, y: y, width: w, height: h))
            f.isEditable = editable
            f.isSelectable = editable
            f.isBezeled = true
            f.bezelStyle = .square
            f.drawsBackground = true
            content.addSubview(f)
            return f
        }
        func addButton(_ title: String, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat,
                       action: Selector) -> NSButton {
            let b = NSButton(frame: NSRect(x: x, y: y, width: w, height: h))
            b.title = title
            b.bezelStyle = .rounded
            b.target = self
            b.action = action
            content.addSubview(b)
            return b
        }

        _ = addLabel("Mano disponibile:", x: 20, y: topY(16, 18), w: 480, h: 18)
        handPopup = addPopup(x: 20, y: topY(38, 24), w: 480, h: 24)
        handPopup.addItems(withTitles: ["Mano sinistra", "Mano destra", "Personalizzato"])
        handPopup.target = self
        handPopup.action = #selector(handPresetChanged(_:))

        keysField = addField(x: 20, y: topY(66, 24), w: 480, h: 24, editable: true)
        keysField.delegate = self

        _ = addLabel("Dizionario:", x: 20, y: topY(100, 18), w: 480, h: 18)
        wordlistPopup = addPopup(x: 20, y: topY(122, 24), w: 480, h: 24)

        _ = addLabel("Tasti funzione (Assegna, poi premi un tasto):", x: 20, y: topY(156, 18), w: 480, h: 18)

        for i in 0..<FN_COUNT {
            let top: CGFloat = 180 + CGFloat(i) * 30
            _ = addLabel(FN_LABEL[i], x: 20, y: topY(top, 24), w: 120, h: 24)

            let fld = addField(x: 148, y: topY(top, 24), w: 150, h: 24, editable: false)
            fld.stringValue = keyDisplayName(fnPend[i])
            fnFld.append(fld)

            let btn = addButton("Assegna", x: 306, y: topY(top, 24), w: 80, h: 24,
                                 action: #selector(assegnaPressed(_:)))
            btn.tag = i
            fnBtn.append(btn)

            let modePop = addPopup(x: 394, y: topY(top, 24), w: 106, h: 24)
            modePop.addItems(withTitles: ["Singola", "Doppia"])
            modePop.tag = i
            modePop.target = self
            modePop.action = #selector(modeChanged(_:))
            modePop.selectItem(at: fnModePend[i])
        }

        _ = addButton("Salva", x: 20, y: topY(336, 34), w: 232, h: 34,
                       action: #selector(savePressed(_:)))
        playBtn = addButton("▶  Play", x: 268, y: topY(336, 34), w: 232, h: 34,
                             action: #selector(playPressed(_:)))

        _ = addButton("Pulisci", x: 20, y: topY(380, 28), w: 90, h: 28,
                       action: #selector(clearPressed(_:)))
        let hint = addLabel("scrivi qui — lo spazio (o il tasto assegnato) è il jolly",
                             x: 124, y: topY(384, 20), w: 376, h: 20)
        hint.textColor = .secondaryLabelColor

        let canvasTop: CGFloat = 416
        view = OneHandView(controller: self)
        view.frame = NSRect(x: 20, y: 16, width: 480, height: H - canvasTop - 16)
        view.autoresizingMask = [.width, .height]
        content.addSubview(view)

        captureView.onKey = { [weak self] code in
            guard let self = self, let fn = self.capturing else { return }
            self.capturing = nil
            self.fnBtn[fn].title = "Assegna"
            if code != AppController.escSentinel {
                self.fnPend[fn] = code
                self.fnFld[fn].stringValue = keyDisplayName(code)
            }
            self.window.makeFirstResponder(self.view)
        }
        content.addSubview(captureView)

        refreshHandCombo()
        populateWordlistMenu()

        win.makeKeyAndOrderFront(nil)
        win.makeFirstResponder(view)
    }

    func refreshHandCombo() {
        let sel = (cfgAvailableKeys == PRESET_LEFT) ? 0 : (cfgAvailableKeys == PRESET_RIGHT) ? 1 : 2
        handPopup.selectItem(at: sel)
        suppressHandChange = true
        keysField.stringValue = cfgAvailableKeys
        suppressHandChange = false
    }

    func populateWordlistMenu() {
        wordlistPopup.removeAllItems()
        wordlistPopup.addItems(withTitles: listWordlistNames(current: cfgWordlistName))
        wordlistPopup.selectItem(withTitle: cfgWordlistName)
    }

    // ---------------------------------------------------------------- azioni UI
    @objc func handPresetChanged(_ sender: NSPopUpButton) {
        let sel = sender.indexOfSelectedItem
        if sel == 0 || sel == 1 {
            suppressHandChange = true
            keysField.stringValue = (sel == 0) ? PRESET_LEFT : PRESET_RIGHT
            suppressHandChange = false
        }
    }

    func controlTextDidChange(_ obj: Notification) {
        guard let field = obj.object as? NSTextField, field === keysField else { return }
        if !suppressHandChange { handPopup.selectItem(at: 2) }   // digitato a mano: "Personalizzato"
    }

    @objc func assegnaPressed(_ sender: NSButton) {
        let fn = sender.tag
        if capturing != nil { return }
        capturing = fn
        fnBtn[fn].title = "Premi un tasto… (Esc)"
        window.makeFirstResponder(captureView)
    }

    @objc func modeChanged(_ sender: NSPopUpButton) {
        fnModePend[sender.tag] = sender.indexOfSelectedItem
    }

    @objc func playPressed(_ sender: NSButton) {
        setActive(!active)
    }

    func setActive(_ on: Bool) {
        active = on
        pending = false; pendingSingleFn = nil
        pendingTimer?.invalidate(); pendingTimer = nil
        onehand_reset(engine)
        refreshFromEngine()
        playBtn.title = on ? "⏹  Stop" : "▶  Play"
        window.makeFirstResponder(view)
    }

    @objc func clearPressed(_ sender: NSButton) {
        onehand_reset(engine)
        view.buffer = ""; view.popup = ""
        view.needsDisplay = true
        pending = false; pendingSingleFn = nil
        pendingTimer?.invalidate(); pendingTimer = nil
        window.makeFirstResponder(view)
    }

    @objc func savePressed(_ sender: NSButton) {
        var keys = ""
        for ch in keysField.stringValue.lowercased() where ch >= "a" && ch <= "z" { keys.append(ch) }
        if keys.isEmpty {
            alert("Inserisci almeno una lettera (a-z) tra i tasti.")
            return
        }
        let wl = wordlistPopup.titleOfSelectedItem ?? cfgWordlistName

        // stesso tasto + stessa pressione su due funzioni: ambiguo, non ammesso.
        for i in 0..<FN_COUNT {
            for j in (i + 1)..<FN_COUNT where fnPend[i] == fnPend[j] && fnModePend[i] == fnModePend[j] {
                alert("«\(FN_LABEL[i])» e «\(FN_LABEL[j])» hanno lo stesso tasto e la stessa pressione. Cambiane uno.")
                return
            }
        }
        // avviso non bloccante: funzione assegnata a una lettera fra i tasti disponibili
        var warn = ""
        for i in 0..<FN_COUNT {
            if let low = MAC_LETTER_CODE.first(where: { $0.value == fnPend[i] })?.key, keys.contains(low) {
                warn += "\n• «\(FN_LABEL[i])» sul tasto '\(low)': quella lettera non sarà più digitabile."
            }
        }
        if !warn.isEmpty && !confirm("Attenzione:\(warn)\n\nSalvare comunque?") { return }

        var chars = Array(cfgText)
        chars = replaceJsonString(chars, "available_keys", keys)
        chars = replaceJsonString(chars, "wordlist", wl)
        for i in 0..<FN_COUNT {
            chars = upsertJsonString(chars, FN_CFGKEY[i], keyCodeToToken(fnPend[i]))
            chars = upsertJsonString(chars, FN_MODEKEY[i], fnModePend[i] == 1 ? "double" : "single")
        }
        let newText = String(chars)
        do {
            try newText.write(toFile: cfgPath, atomically: true, encoding: .utf8)
        } catch {
            alert("Impossibile scrivere config.json.")
            return
        }
        cfgText = newText
        for i in 0..<FN_COUNT { fnVk[i] = fnPend[i]; fnMode[i] = fnModePend[i] }
        applyConfigText(newText)
        refreshHandCombo()
        alert("Configurazione salvata e applicata.")
        window.makeFirstResponder(view)
    }

    // Riapplica al motore una config (dopo un salvataggio): reimposta i
    // parametri e ricarica il dizionario. Se attivo, azzera la composizione.
    func applyConfigText(_ text: String) {
        onehand_apply_config_json(engine, text)
        cfgAvailableKeys = wstr(onehand_config_available_keys(engine))
        cfgWordlistName  = wstr(onehand_config_wordlist_name(engine))
        doublePressMs    = Int(onehand_config_double_press_ms(engine))

        let path = resolveWordlist(cfgWordlistName)
        if onehand_load_wordlist_file(engine, path) == 0 {
            alert("Dizionario non trovato: \(cfgWordlistName)")
        }
        if active {
            onehand_reset(engine)
            refreshFromEngine()
        }
        populateWordlistMenu()
    }

    private func alert(_ msg: String) {
        let a = NSAlert()
        a.messageText = "OneHand"
        a.informativeText = msg
        a.runModal()
    }
    private func confirm(_ msg: String) -> Bool {
        let a = NSAlert()
        a.messageText = "OneHand"
        a.informativeText = msg
        a.addButton(withTitle: "Sì")
        a.addButton(withTitle: "No")
        return a.runModal() == .alertFirstButtonReturn
    }

    // ---------------------------------------------------------------- routing dei tasti
    // A Stop: digitazione normale nel foglio. A Play: stesso schema di Windows,
    // il frontend risolve quale tasto fisico/pressione attiva quale funzione.
    func handleKeyEvent(_ e: NSEvent) {
        if active { handleActiveKey(e) } else { insertLiteral(e) }
    }

    private func insertLiteral(_ e: NSEvent) {
        if e.keyCode == vkDelete {
            if !view.buffer.isEmpty { view.buffer.removeLast() }
            view.needsDisplay = true
            return
        }
        if e.keyCode == vkReturn {
            view.buffer += "\n"
            view.needsDisplay = true
            return
        }
        guard let chars = e.characters, !chars.isEmpty else { return }
        let filtered = chars.unicodeScalars.filter { $0.value >= 0x20 && $0.value != 0x7F && !(0xF700...0xF8FF).contains($0.value) }
        if !filtered.isEmpty {
            view.buffer += String(String.UnicodeScalarView(filtered))
            view.needsDisplay = true
        }
    }

    private func handleActiveKey(_ e: NSEvent) {
        let code = e.keyCode
        var sFn = -1, dFn = -1
        for i in 0..<FN_COUNT where fnVk[i] == code {
            if fnMode[i] == 1 { dFn = i } else { sFn = i }
        }
        let letterChar = MAC_LETTER_CODE.first(where: { $0.value == code })?.key
        let isHandled = (sFn != -1 || dFn != -1 || code == vkReturn || letterChar != nil)
        if !isHandled {           // tasto non riconosciuto: passa come digitazione normale
            insertLiteral(e)
            return
        }

        // seconda pressione dello stesso tasto in attesa -> azione "doppia"
        if pending, code == pendingCode, dFn != -1 {
            pendingTimer?.invalidate(); pendingTimer = nil
            pending = false; pendingSingleFn = nil
            performAction(FN_ACTION[dFn])
            return
        }
        // un altro tasto era in attesa: risolvilo come pressione singola
        if pending {
            pendingTimer?.invalidate(); pendingTimer = nil
            let pf = pendingSingleFn
            pending = false; pendingSingleFn = nil
            if let pf = pf { performAction(FN_ACTION[pf]) }
        }
        if dFn != -1 {             // puo' raddoppiare: aspetta il timer
            pending = true; pendingCode = code; pendingSingleFn = (sFn != -1 ? sFn : nil)
            pendingTimer?.invalidate()
            pendingTimer = Timer.scheduledTimer(withTimeInterval: Double(doublePressMs) / 1000.0, repeats: false) { [weak self] _ in
                self?.onPendingTimeout()
            }
            if let sFn = sFn, FN_ACTION[sFn] == ACTION_WILDCARD {
                onehand_preview_wildcard(engine)      // anteprima jolly durante l'attesa
                refreshFromEngine()
            }
        } else if sFn != -1 {      // solo singola: esegui subito
            performAction(FN_ACTION[sFn])
        } else if code == vkReturn {
            performAction(ACTION_FINALIZE)
        } else if let ch = letterChar {
            performAction(ACTION_LETTER, Int32(ch.asciiValue!))
        }
    }

    private func onPendingTimeout() {
        pendingTimer = nil
        guard pending else { return }
        let pf = pendingSingleFn
        pending = false; pendingSingleFn = nil
        if let pf = pf { performAction(FN_ACTION[pf]) }
    }

    private func performAction(_ action: Int32, _ letter: Int32 = 0) {
        onehand_on_action(engine, action, letter)
        refreshFromEngine()
        if onehand_pass_through(engine) != 0 {   // es. Invio: la riga va comunque a capo
            view.buffer += "\n"
            view.needsDisplay = true
        }
    }

    private func refreshFromEngine() {
        let count = onehand_edit_count(engine)
        var i: Int32 = 0
        while i < count {
            let bs = Int(onehand_edit_backspaces(engine, i))
            if bs > 0 { view.buffer.removeLast(min(bs, view.buffer.count)) }
            view.buffer += wstr(onehand_edit_insert(engine, i))
            i += 1
        }
        view.popup = (onehand_popup_visible(engine) != 0) ? wstr(onehand_popup_text(engine)) : ""
        view.needsDisplay = true
    }
}

// MARK: - delegato: esci quando si chiude la finestra

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

// MARK: - avvio

let app = NSApplication.shared
app.setActivationPolicy(.regular)

let delegate = AppDelegate()
app.delegate = delegate

guard let engine = onehand_create() else { fatalError("impossibile creare il motore") }
let controller = AppController(engine: engine)
controller.start()

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

app.activate(ignoringOtherApps: true)
app.run()
