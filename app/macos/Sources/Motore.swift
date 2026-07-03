// Wrapper Swift idiomatico sopra la C ABI del MOTORE (modulo CMotore).
//
// Tutta la logica di composizione vive nel MOTORE C++ (core/src/motore): qui c'e'
// solo il marshalling da/verso Swift. Equivale, per il FE macOS, all'uso diretto
// di motore::Engine che fa il FE Windows (app/windows/main.cpp).

import CMotore
import Foundation

// Mirror di motore::Highlight (valori MO_HL_* dell'ABI).
enum Highlight: Int {
    case none = 0, selected = 1, open = 2
}

// Uno span del render (parola/token) con evidenziazione e spaziatura.
struct RenderSpan {
    let text: String
    let hl: Highlight
    let spaceBefore: Bool
    let typed: Int          // n. lettere digitate (parola aperta): il FE ne sottolinea il prefisso
}

// Snapshot del render per l'overlay + azioni valide per il pannello.
struct RenderModel {
    var spans: [RenderSpan] = []
    var suggestions: [String] = []
    var suggestionSel: Int = -1
    var suggestionsAreNext: Bool = false
    var isEmpty: Bool = true
    var availability = Availability()
}

// Mirror di motore::Availability: quali comandi hanno senso adesso.
struct Availability {
    var navPrev = false, navNext = false, open = false, roll = false
    var confirm = false, advance = true, deleteLetter = false, deleteWord = false
    var punct = true, read = true, write = false, discard = false
}

final class Motore {
    private let handle: OpaquePointer

    init() { handle = mo_create() }
    deinit { mo_destroy(handle) }

    // --- risorse / modalita' (chiamare set(keymap:)+applyConfig PRIMA dei load) ---
    func setKeymap(_ groups: [Character: String]) {
        mo_keymap_clear(handle)
        for (key, letters) in groups {
            mo_keymap_add(handle, String(key), letters)
        }
    }
    func applyConfig(maxCandidates: Int) {
        mo_set_config(handle, Int32(maxCandidates))   // AZZERA il dizionario
    }
    @discardableResult func loadWordlist(path: String) -> Bool {
        mo_load_wordlist(handle, path) == 1
    }
    @discardableResult func loadBigrams(path: String) -> Bool {
        mo_load_bigrams(handle, path) == 1
    }
    func setMode(assisted: Bool) { mo_set_mode(handle, assisted ? 1 : 0) }

    // --- documento -----------------------------------------------------------
    func loadResolved(_ text: String) { mo_load_resolved(handle, text) }
    func clear() { mo_clear(handle) }
    var isEmpty: Bool { mo_empty(handle) == 1 }

    // --- azioni --------------------------------------------------------------
    func typeKey(_ sym: String) { mo_type_key(handle, sym) }
    func navigatePrev() { mo_navigate_prev(handle) }
    func navigateNext() { mo_navigate_next(handle) }
    func openSelected() { mo_open_selected(handle) }
    func roll() { mo_roll(handle) }
    func confirm() { mo_confirm(handle) }
    func advance() { mo_advance(handle) }
    func confirmContinue() { mo_confirm_continue(handle) }
    func punct(_ sym: String) { mo_punct(handle, sym) }
    func deleteLetter() { mo_delete_letter(handle) }
    func deleteWord() { mo_delete_word(handle) }
    func acceptSuggestion(_ k: Int) { mo_accept_suggestion(handle, Int32(k)) }

    // Write: conferma, svuota il buffer e ritorna il testo completo (UTF-8).
    func write() -> String {
        guard let p = mo_write(handle) else { return "" }
        return String(cString: p)
    }

    // --- render --------------------------------------------------------------
    // Ricalcola nel MOTORE e legge lo snapshot cachato in un RenderModel Swift.
    func render() -> RenderModel {
        mo_render(handle)
        var m = RenderModel()
        m.isEmpty = mo_empty(handle) == 1

        let nSpans = mo_span_count(handle)
        m.spans.reserveCapacity(nSpans)
        for i in 0..<nSpans {
            let text = mo_span_text(handle, i).map { String(cString: $0) } ?? ""
            let hl = Highlight(rawValue: Int(mo_span_hl(handle, i))) ?? .none
            m.spans.append(RenderSpan(text: text, hl: hl,
                                      spaceBefore: mo_span_space_before(handle, i) == 1,
                                      typed: Int(mo_span_typed(handle, i))))
        }

        let nSug = mo_suggestion_count(handle)
        m.suggestions.reserveCapacity(nSug)
        for i in 0..<nSug {
            m.suggestions.append(mo_suggestion_text(handle, i).map { String(cString: $0) } ?? "")
        }
        m.suggestionSel = Int(mo_suggestion_sel(handle))
        m.suggestionsAreNext = mo_suggestions_are_next(handle) == 1

        var av = mo_avail()
        mo_availability(handle, &av)
        m.availability = Availability(
            navPrev: av.navPrev != 0, navNext: av.navNext != 0, open: av.open != 0,
            roll: av.roll != 0, confirm: av.confirm != 0, advance: av.advance != 0,
            deleteLetter: av.deleteLetter != 0, deleteWord: av.deleteWord != 0,
            punct: av.punct != 0, read: av.read != 0, write: av.write != 0,
            discard: av.discard != 0)
        return m
    }
}
