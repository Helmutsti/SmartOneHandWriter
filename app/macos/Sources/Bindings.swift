// Mappatura TASTO fisico (keycode macOS) -> FUNZIONE, letta da data/tasti.conf con
// fallback ai default di fabbrica. Porting di app/windows/main.cpp: funcNameToAct,
// keyNameToVk, loadBindings, defaultBindings, vkDisplay, keyForAct.
//
// Nota: il file tasti.conf usa nomi di tasto stile Windows (Spazio, Tab, Backspace,
// BlocMaiusc, Esc, Backtick, lettere, cifre). Qui li traduciamo nei keycode macOS
// equivalenti, cosi' lo stesso file di conf vale su entrambe le piattaforme.

import Foundation

// Azioni (mirror di enum Act in main.cpp).
enum Act {
    case none
    case navPrev, navNext, open, roll, confirm, advance, cont
    case delLetter, delWord, dot, comma, ques, excl, read, write, discard
}

// ------------------------------------------------------------------ keycode macOS
// Lettera (a..z) -> keycode ANSI.
let letterCode: [Character: UInt16] = [
    "a": 0x00, "b": 0x0B, "c": 0x08, "d": 0x02, "e": 0x0E, "f": 0x03, "g": 0x05,
    "h": 0x04, "i": 0x22, "j": 0x26, "k": 0x28, "l": 0x25, "m": 0x2E, "n": 0x2D,
    "o": 0x1F, "p": 0x23, "q": 0x0C, "r": 0x0F, "s": 0x01, "t": 0x11, "u": 0x20,
    "v": 0x09, "w": 0x0D, "x": 0x07, "y": 0x10, "z": 0x06,
]
// Cifra (0..9) -> keycode (fila superiore e tastierino).
let digitCode: [Character: UInt16] = [
    "0": 0x1D, "1": 0x12, "2": 0x13, "3": 0x14, "4": 0x15,
    "5": 0x17, "6": 0x16, "7": 0x1A, "8": 0x1C, "9": 0x19,
]
let keypadCode: [Character: UInt16] = [
    "0": 0x52, "1": 0x53, "2": 0x54, "3": 0x55, "4": 0x56,
    "5": 0x57, "6": 0x58, "7": 0x59, "8": 0x5B, "9": 0x5C,
]
enum KC {
    static let space: UInt16   = 0x31
    static let tab: UInt16     = 0x30
    static let delete: UInt16  = 0x33   // Backspace fisico su Mac
    static let capsLock: UInt16 = 0x39  // BlocMaiusc
    static let escape: UInt16  = 0x35
    static let grave: UInt16   = 0x32   // ` (backtick, a sinistra dell'1)
    static let v: UInt16       = 0x09   // per il Cmd+V sintetico (Write)
}

// keycode -> lettera minuscola (se e' un tasto lettera).
func letterOf(_ code: UInt16) -> Character? {
    letterCode.first(where: { $0.value == code })?.key
}
// keycode -> cifra (fila alta o tastierino).
func digitOf(_ code: UInt16) -> Character? {
    if let ch = digitCode.first(where: { $0.value == code })?.key { return ch }
    if let ch = keypadCode.first(where: { $0.value == code })?.key { return ch }
    return nil
}

// Tasti-gruppo T9 (come su un cellulare): w e a s d z x c.
private let groupKeys: Set<Character> = ["w", "e", "a", "s", "d", "z", "x", "c"]
func isGroupKey(_ code: UInt16) -> Bool {
    if let ch = letterOf(code) { return groupKeys.contains(ch) }
    return false
}

// ------------------------------------------------------------------ parser conf
// Nome-funzione del file di conf -> azione. .none = sconosciuto.
func funcNameToAct(_ name: String) -> Act {
    switch name {
    case "conferma_continua": return .cont
    case "cancella_parola":   return .delWord
    case "cancella_lettera":  return .delLetter
    case "scarta":            return .discard
    case "punto":             return .dot
    case "virgola":           return .comma
    case "domanda":           return .ques
    case "esclamativo":       return .excl
    case "write":             return .write
    case "read":              return .read
    case "roll":              return .roll
    case "conferma":          return .confirm
    case "avanti":            return .advance
    case "apri":              return .open
    case "naviga_prev":       return .navPrev
    case "naviga_next":       return .navNext
    default:                  return .none
    }
}

// Nome-tasto del file di conf -> keycode macOS. nil = sconosciuto. Case-insensitive.
func keyNameToCode(_ raw: String) -> UInt16? {
    let s = raw.lowercased()
    if s.count == 1, let c = s.first {
        if let code = letterCode[c] { return code }
        if let code = digitCode[c] { return code }
        if c == "`" { return KC.grave }
    }
    switch s {
    case "spazio", "space":                        return KC.space
    case "tab":                                    return KC.tab
    case "backspace", "back":                      return KC.delete
    case "blocmaiusc", "capslock", "caps":         return KC.capsLock
    case "esc", "escape":                          return KC.escape
    case "backtick", "grave", "apice":             return KC.grave
    default:                                       return nil
    }
}

// keycode -> nome breve da mostrare sul bottone (porting di vkDisplay).
func codeDisplay(_ code: UInt16) -> String {
    switch code {
    case KC.space:    return "Spazio"
    case KC.tab:      return "Tab"
    case KC.delete:   return "Backspace"
    case KC.capsLock: return "BlocMaiusc"
    case KC.escape:   return "Esc"
    case KC.grave:    return "`"
    default:
        if let ch = letterOf(code) { return String(ch).uppercased() }
        if let ch = digitOf(code) { return String(ch) }
        return ""
    }
}

// La mappa TASTO->FUNZIONE, con parser del file di conf e default di fabbrica.
final class Bindings {
    private(set) var map: [UInt16: Act] = [:]

    init() { defaults() }

    // Binding di fabbrica: coincidono con data/tasti.conf (usati se il file manca).
    func defaults() {
        map = [:]
        map[KC.space]    = .cont
        map[KC.tab]      = .delWord
        map[KC.delete]   = .delLetter
        map[KC.capsLock] = .delLetter
        map[KC.escape]   = .discard
        map[digitCode["1"]!] = .dot
        map[digitCode["2"]!] = .comma
        map[digitCode["3"]!] = .ques
        map[digitCode["4"]!] = .excl
        map[digitCode["5"]!] = .write
        map[KC.grave]        = .read
        map[letterCode["f"]!] = .roll
        map[letterCode["g"]!] = .confirm
        map[letterCode["r"]!] = .advance
        map[letterCode["t"]!] = .open
        map[letterCode["v"]!] = .navPrev
        map[letterCode["b"]!] = .navNext
    }

    // Carica dal file di conf. Formato: "funzione = tasto [tasto...]". '#' = commento.
    // La mappa e' ricostruita interamente dal file (nessun merge coi default): cosi'
    // rimappare una funzione ne SPOSTA il tasto. Ritorna false (usa i default) se il
    // file manca o non contiene binding validi.
    @discardableResult
    func load(path: String) -> Bool {
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return false }
        var parsed: [UInt16: Act] = [:]
        for var line in text.split(separator: "\n", omittingEmptySubsequences: false).map(String.init) {
            if let h = line.firstIndex(of: "#") { line = String(line[..<h]) }
            guard let eq = line.firstIndex(of: "=") else { continue }
            let fname = line[..<eq].trimmingCharacters(in: .whitespaces).lowercased()
            let act = funcNameToAct(fname)
            if act == .none { continue }
            let keys = line[line.index(after: eq)...]
            for tok in keys.split(whereSeparator: { $0 == " " || $0 == "\t" || $0 == "," || $0 == "\r" }) {
                if let code = keyNameToCode(String(tok)) { parsed[code] = act }
            }
        }
        if parsed.isEmpty { return false }
        map = parsed
        return true
    }

    // NB: Act e' una enum senza valori associati -> gia' Equatable/Hashable.

    // Un tasto e' "gestito" (da consumare)? Porting di isMapped, dipende dalla modalita'.
    func isMapped(_ code: UInt16, mode: Mode) -> Bool {
        if mode == .classic {
            if letterOf(code) != nil { return true }   // in Classica ogni lettera si digita
            return map[code] != nil
        }
        if map[code] != nil { return true }
        return isGroupKey(code)
    }

    // Primo tasto (per nome visualizzato) mappato a un'azione; "" se nessuno.
    // Ordinato per stabilita' visiva (come il std::map ordinato di Windows).
    func keyDisplay(for act: Act) -> String {
        let codes = map.filter { $0.value == act }.map { $0.key }.sorted()
        guard let first = codes.first else { return "" }
        return codeDisplay(first)
    }
}
