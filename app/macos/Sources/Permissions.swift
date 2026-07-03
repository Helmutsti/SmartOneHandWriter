// Permesso Accessibilita' (TCC): necessario perche' CGEventTap possa intercettare
// i tasti globali. Senza, tapCreate fallisce. Non ha equivalente su Windows (dove
// l'hook non richiede permessi espliciti).

import AppKit
import ApplicationServices

enum Permissions {
    // true se l'app e' gia' autorizzata. Con prompt:true mostra la richiesta di
    // sistema e apre il pannello Impostazioni la prima volta.
    @discardableResult
    static func ensureAccessibility(prompt: Bool) -> Bool {
        let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
        let opts = [key: prompt] as CFDictionary
        return AXIsProcessTrustedWithOptions(opts)
    }

    // Guida l'utente al riquadro Accessibilita' in Impostazioni di Sistema.
    static func openAccessibilitySettings() {
        let url = URL(string:
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!
        NSWorkspace.shared.open(url)
    }

    // Avviso quando manca il permesso e l'utente prova ad attivare l'assistente.
    static func warnMissing() {
        let a = NSAlert()
        a.messageText = "Permesso Accessibilità necessario"
        a.informativeText = """
        Per intercettare i tasti in tutte le app, SmartOneHandWriter ha bisogno del \
        permesso Accessibilità.

        Aprilo in Impostazioni di Sistema › Privacy e sicurezza › Accessibilità, \
        abilita l'app, poi premi di nuovo Play.
        """
        a.addButton(withTitle: "Apri Impostazioni")
        a.addButton(withTitle: "Annulla")
        if a.runModal() == .alertFirstButtonReturn { openAccessibilitySettings() }
    }
}
