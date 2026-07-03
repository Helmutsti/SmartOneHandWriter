// Read/Write via clipboard e incolla sintetico (porting di clipboardGet/Set,
// sendPaste, doRead/doWrite di app/windows/main.cpp).

import AppKit
import CoreGraphics

// Marcatore degli eventi che iniettiamo noi (il Cmd+V del Write): lo scriviamo nel
// campo utente dell'evento e lo ignoriamo nel tap, cosi' non rientra nel motore
// (equivalente del filtro LLKHF_INJECTED su Windows).
let kInjectedMagic: Int64 = 0x5350_4857   // 'SPHW'

enum Clipboard {
    static func get() -> String {
        NSPasteboard.general.string(forType: .string) ?? ""
    }
    static func set(_ text: String) {
        let pb = NSPasteboard.general
        pb.clearContents()
        pb.setString(text, forType: .string)
    }

    // Simula Cmd+V nell'app attiva (marcato come iniettato).
    static func sendPaste() {
        let src = CGEventSource(stateID: .combinedSessionState)
        let down = CGEvent(keyboardEventSource: src, virtualKey: KC.v, keyDown: true)
        let up   = CGEvent(keyboardEventSource: src, virtualKey: KC.v, keyDown: false)
        down?.flags = .maskCommand
        up?.flags = .maskCommand
        down?.setIntegerValueField(.eventSourceUserData, value: kInjectedMagic)
        up?.setIntegerValueField(.eventSourceUserData, value: kInjectedMagic)
        down?.post(tap: .cghidEventTap)
        up?.post(tap: .cghidEventTap)
    }
}
