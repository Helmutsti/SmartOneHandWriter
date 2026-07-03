// Hub del frontend macOS: possiede il MOTORE, i binding, la modalità, le finestre e
// l'event tap. Porting della logica di app/windows/main.cpp (handleKeyDown,
// performAction, multiTapKey, refreshOverlay, togglePlay, toggle Modalità).

import AppKit
import Foundation

final class AppController: NSObject, NSApplicationDelegate, KeyTapDelegate {
    private let motore = Motore()
    private let bindings = Bindings()
    private let tap = EventTap()
    private var panel: PanelWindow!
    private var overlay: OverlayPanel!

    private var active = false               // Play/Pause
    private var mode: Mode = .t9

    // keymap T9 stile cellulare (serve al multi-tap e al MOTORE).
    private let keymap: [Character: String] = [
        "w": "abc", "e": "def", "a": "ghi", "s": "jkl",
        "d": "mno", "z": "pqrs", "x": "tuv", "c": "wxyz",
    ]
    // Stato del multi-tap (scorrimento lettere).
    private var mtKey: UInt16? = nil
    private var mtIdx = 0
    private var mtTime: CFAbsoluteTime = 0
    private let kMultiTapSec: CFAbsoluteTime = 0.9

    // ------------------------------------------------------------------ avvio
    func applicationDidFinishLaunching(_ note: Notification) {
        // MOTORE: keymap + config PRIMA di caricare i dati (setConfig azzera il dizionario).
        motore.setKeymap(keymap)
        motore.applyConfig(maxCandidates: 8)
        if let wl = dataPath("wordlist_it.txt") { motore.loadWordlist(path: wl) }
        if let bg = dataPath("it.bigrams.bin") { motore.loadBigrams(path: bg) }
        motore.setMode(assisted: true)

        // Binding tasto->funzione dal file di conf (default se assente).
        if let conf = dataPath("tasti.conf") { _ = bindings.load(path: conf) }

        // Finestre.
        panel = PanelWindow(bindings: bindings)
        panel.onPlay = { [weak self] in self?.togglePlay() }
        panel.onMode = { [weak self] in self?.toggleMode() }
        panel.onAction = { [weak self] act in self?.performAction(act) }
        overlay = OverlayPanel()
        overlay.overlayView.onSuggestionClick = { [weak self] idx in
            self?.resetMultiTap()
            self?.motore.acceptSuggestion(idx)
            self?.refreshOverlay()
        }

        tap.delegate = self

        panel.center()
        panel.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        panel.setStatus(active: active, mode: mode)
        refreshOverlay()

        // Richiedi il permesso Accessibilità all'avvio (prompt di sistema).
        Permissions.ensureAccessibility(prompt: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool { true }

    // ------------------------------------------------------------------ Play/Pause
    private func togglePlay() {
        if !active {
            guard tap.start() else {            // fallisce se manca il permesso
                Permissions.warnMissing()
                return
            }
            active = true
        } else {
            tap.stop()
            active = false
        }
        panel.setStatus(active: active, mode: mode)
        refreshOverlay()
    }

    private func toggleMode() {
        mode = mode.next                        // T9 -> Classica -> Multi-tap
        motore.setMode(assisted: mode.assisted) // T9 vs Literal
        resetMultiTap()
        panel.setStatus(active: active, mode: mode)
    }

    // ------------------------------------------------------------------ KeyTapDelegate
    func isMapped(_ code: UInt16) -> Bool { bindings.isMapped(code, mode: mode) }

    func handleKeyDown(_ code: UInt16) -> Bool {
        let letterKey = letterOf(code) != nil

        // 1) Funzione mappata al tasto -> comando al MOTORE (in Classica le lettere
        //    si digitano, quindi una funzione su lettera lì è inattiva).
        if let act = bindings.map[code], !(letterKey && mode == .classic) {
            performAction(act)
            return true
        }

        // 2) Digitazione di lettere / gruppi.
        if mode == .classic {
            if let ch = letterOf(code) {
                motore.typeKey(String(ch)); refreshOverlay(); return true
            }
            return false
        }
        // T9 e Multi-tap: i tasti-gruppo alimentano dizionario / scorrimento lettere.
        if isGroupKey(code) {
            if mode == .multitap {
                multiTapKey(code)
            } else if let ch = letterOf(code) {   // MODE_T9: il tasto-gruppo va al dizionario
                motore.typeKey(String(ch)); refreshOverlay()
            }
            return true
        }
        return false    // altri tasti: lasciali passare all'app
    }

    // ------------------------------------------------------------------ multi-tap
    private func resetMultiTap() { mtKey = nil }

    private func multiTapKey(_ code: UInt16) {
        guard let keych = letterOf(code) else { return }
        let grp = Array(keymap[keych] ?? String(keych))
        if grp.isEmpty { return }
        let now = CFAbsoluteTimeGetCurrent()
        if code == mtKey && (now - mtTime) < kMultiTapSec {
            // stessa lettera in corso: cicla al glifo successivo (sostituisce l'ultima cella)
            mtIdx = (mtIdx + 1) % grp.count
            motore.deleteLetter()
            motore.typeKey(String(grp[mtIdx]))
        } else {
            // nuova lettera: primo glifo del gruppo
            mtIdx = 0
            motore.typeKey(String(grp[0]))
            mtKey = code
        }
        mtTime = now
        refreshOverlay()
    }

    // ------------------------------------------------------------------ azioni
    private func performAction(_ a: Act) {
        resetMultiTap()   // qualunque comando chiude il ciclo del multi-tap in corso
        switch a {
        case .navPrev:   motore.navigatePrev()
        case .navNext:   motore.navigateNext()
        case .open:      motore.openSelected()
        case .roll:      motore.roll()
        case .confirm:   motore.confirm()
        case .advance:   motore.advance()
        case .cont:      motore.confirmContinue()
        case .delLetter: motore.deleteLetter()
        case .delWord:   motore.deleteWord()
        case .dot:       motore.punct(".")
        case .comma:     motore.punct(",")
        case .ques:      motore.punct("?")
        case .excl:      motore.punct("!")
        case .read:      doRead();  return   // già refresha
        case .write:     doWrite(); return   // già refresha
        case .discard:   motore.clear()
        case .none:      break
        }
        refreshOverlay()
    }

    private func doRead() {
        let w = Clipboard.get()
        if !w.isEmpty { motore.loadResolved(w) }
        refreshOverlay()
    }

    private func doWrite() {
        let text = motore.write()
        if text.isEmpty { refreshOverlay(); return }
        let saved = Clipboard.get()
        Clipboard.set(text)
        Clipboard.sendPaste()
        // Ripristina la clipboard dell'utente dopo che l'incolla è stato consegnato
        // (asincrono: su macOS non si può bloccare la run loop come faceva Sleep(80)).
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            Clipboard.set(saved)
        }
        refreshOverlay()
    }

    // ------------------------------------------------------------------ render
    private func refreshOverlay() {
        let m = motore.render()
        panel.updateButtonStates(m.availability)   // anche a documento vuoto
        overlay.update(m)                           // nasconde l'overlay se vuoto
    }

    // ------------------------------------------------------------------ dati
    // Cerca <name> in: Resources del bundle, accanto all'eseguibile, ../data, cwd.
    private func dataPath(_ name: String) -> String? {
        let fm = FileManager.default
        var tries: [String] = []
        if let res = Bundle.main.resourceURL {
            tries.append(res.appendingPathComponent("data/\(name)").path)
            tries.append(res.appendingPathComponent(name).path)
        }
        if let exe = Bundle.main.executableURL?.deletingLastPathComponent() {
            tries.append(exe.appendingPathComponent("data/\(name)").path)
            tries.append(exe.appendingPathComponent("../data/\(name)").path)
        }
        tries.append(fm.currentDirectoryPath + "/data/\(name)")
        tries.append(fm.currentDirectoryPath + "/\(name)")
        for t in tries where fm.fileExists(atPath: t) { return t }
        return nil
    }
}
