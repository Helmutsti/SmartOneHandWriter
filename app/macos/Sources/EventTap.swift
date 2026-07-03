// Hook tastiera globale via CGEventTap (Quartz Event Services). Equivalente macOS
// di SetWindowsHookExW(WH_KEYBOARD_LL)+KeyProc di app/windows/main.cpp: intercetta
// i tasti a livello di sessione e puo' CONSUMARLI (return nil) prima che
// raggiungano l'app con focus.
//
// Richiede il permesso Accessibilita' (vedi Permissions.swift). Il tap gira sulla
// run loop principale; va ri-abilitato se il sistema lo disattiva per timeout.

import CoreGraphics
import Foundation

protocol KeyTapDelegate: AnyObject {
    func handleKeyDown(_ code: UInt16) -> Bool   // true = consumato (non passa all'app)
    func isMapped(_ code: UInt16) -> Bool        // per consumare anche il keyUp corrispondente
}

final class EventTap {
    weak var delegate: KeyTapDelegate?
    private(set) var isRunning = false
    private var tap: CFMachPort?
    private var source: CFRunLoopSource?

    // Callback C (non cattura contesto): recupera self dal refcon.
    private static let callback: CGEventTapCallBack = { _, type, event, refcon in
        guard let refcon = refcon else { return Unmanaged.passUnretained(event) }
        let me = Unmanaged<EventTap>.fromOpaque(refcon).takeUnretainedValue()

        // Il sistema puo' disabilitare il tap (timeout / input dell'utente): riabilitalo.
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            if let t = me.tap { CGEvent.tapEnable(tap: t, enable: true) }
            return Unmanaged.passUnretained(event)
        }

        // Ignora gli eventi che iniettiamo noi (Cmd+V del Write): lasciali passare.
        if event.getIntegerValueField(.eventSourceUserData) == kInjectedMagic {
            return Unmanaged.passUnretained(event)
        }

        let code = UInt16(event.getIntegerValueField(.keyboardEventKeycode))
        guard let delegate = me.delegate else { return Unmanaged.passUnretained(event) }

        if type == .keyDown {
            if delegate.handleKeyDown(code) { return nil }   // consumato
        } else if type == .keyUp {
            if delegate.isMapped(code) { return nil }         // consuma anche il keyUp
        }
        return Unmanaged.passUnretained(event)
    }

    // Crea e attiva il tap. Ritorna false se manca il permesso Accessibilita'.
    @discardableResult
    func start() -> Bool {
        if isRunning { return true }
        let mask = (1 << CGEventType.keyDown.rawValue) | (1 << CGEventType.keyUp.rawValue)
        guard let tap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .headInsertEventTap,
            options: .defaultTap,                 // .defaultTap = puo' modificare/scartare
            eventsOfInterest: CGEventMask(mask),
            callback: EventTap.callback,
            userInfo: Unmanaged.passUnretained(self).toOpaque()
        ) else {
            return false                          // tipicamente: permesso non concesso
        }
        self.tap = tap
        let src = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        self.source = src
        CFRunLoopAddSource(CFRunLoopGetMain(), src, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        isRunning = true
        return true
    }

    func stop() {
        guard isRunning, let tap = tap else { return }
        CGEvent.tapEnable(tap: tap, enable: false)
        if let src = source { CFRunLoopRemoveSource(CFRunLoopGetMain(), src, .commonModes) }
        source = nil
        self.tap = nil
        isRunning = false
    }
}
