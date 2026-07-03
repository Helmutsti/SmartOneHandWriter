// Pannello di comando: un bottone per funzione + Play/Pause + toggle Modalità.
// Porting di createPanel/updateButtonStates/PanelProc di app/windows/main.cpp.
// I bottoni si abilitano/disabilitano in base a motore::Availability e mostrano tra
// parentesi il tasto che invoca la funzione (etichetta a due righe).

import AppKit

// Tre modalità d'ingresso del FE (il MOTORE conosce solo T9 vs Literal; il Multi-tap
// è Literal con il ciclo delle lettere gestito nel FE).
enum Mode: Int {
    case t9 = 0, classic, multitap
    var next: Mode { Mode(rawValue: (rawValue + 1) % 3)! }
    var label: String {
        switch self {
        case .t9:       return "Assistita (T9)"
        case .classic:  return "Classica"
        case .multitap: return "Multi-tap (scorrimento lettere)"
        }
    }
    var assisted: Bool { self == .t9 }   // T9 vs Literal per il MOTORE
}

final class PanelWindow: NSWindow {
    var onPlay: (() -> Void)?
    var onMode: (() -> Void)?
    var onAction: ((Act) -> Void)?

    private var buttons: [Act: NSButton] = [:]
    private let status = NSTextField(labelWithString: "")
    private let bindings: Bindings

    init(bindings: Bindings) {
        self.bindings = bindings
        super.init(contentRect: NSRect(x: 0, y: 0, width: 530, height: 216),
                   styleMask: [.titled, .closable, .miniaturizable],
                   backing: .buffered, defer: false)
        title = "SmartOneHandWriter — Assistente"
        isReleasedWhenClosed = false
        buildContent()
    }

    // Etichetta del bottone: testo + "(tasto)" su una seconda riga.
    private func withKey(_ base: String, _ act: Act) -> String {
        let k = bindings.keyDisplay(for: act)
        return k.isEmpty ? base : "\(base)\n(\(k))"
    }

    private func makeButton(_ title: String, tag: Int, x: CGFloat, y: CGFloat,
                            w: CGFloat, h: CGFloat, act: Act?) -> NSButton {
        let b = NSButton(frame: NSRect(x: x, y: y, width: w, height: h))
        b.title = title
        b.bezelStyle = .regularSquare        // altezza libera: consente i bottoni a due righe
        b.setButtonType(.momentaryPushIn)
        b.alignment = .center
        (b.cell as? NSButtonCell)?.usesSingleLineMode = false   // consente le due righe
        b.lineBreakMode = .byWordWrapping
        b.tag = tag
        b.target = self
        b.action = #selector(buttonClicked(_:))
        contentView?.addSubview(b)
        if let act = act { buttons[act] = b }
        return b
    }

    // La contentView usa origine in basso a sinistra: convertiamo la y "stile Windows"
    // (dall'alto) nella y AppKit una sola volta con topY().
    private func buildContent() {
        let W: CGFloat = 96, H: CGFloat = 42, PAD: CGFloat = 6
        let total = contentView!.frame.height
        func topY(_ yTop: CGFloat, _ h: CGFloat) -> CGFloat { total - yTop - h }
        func row(_ col: Int) -> CGFloat { PAD + CGFloat(col) * (W + PAD) }

        var y = PAD
        _ = makeButton("Play/Pause", tag: Tag.play.rawValue, x: row(0), y: topY(y, H), w: W, h: H, act: nil)
        _ = makeButton("Modalità",   tag: Tag.mode.rawValue, x: row(1), y: topY(y, H), w: W, h: H, act: nil)
        status.frame = NSRect(x: row(2), y: topY(y + 4, 22), width: 3 * (W + PAD), height: 22)
        status.font = .systemFont(ofSize: 12)
        contentView?.addSubview(status)

        y += H + PAD
        _ = makeButton(withKey("◀ Naviga", .navPrev), tag: Tag.navPrev.rawValue, x: row(0), y: topY(y, H), w: W, h: H, act: .navPrev)
        _ = makeButton(withKey("Naviga ▶", .navNext), tag: Tag.navNext.rawValue, x: row(1), y: topY(y, H), w: W, h: H, act: .navNext)
        _ = makeButton(withKey("Apri/Edit", .open),   tag: Tag.open.rawValue,    x: row(2), y: topY(y, H), w: W, h: H, act: .open)
        _ = makeButton(withKey("Roll", .roll),        tag: Tag.roll.rawValue,    x: row(3), y: topY(y, H), w: W, h: H, act: .roll)
        _ = makeButton(withKey("Conferma", .confirm), tag: Tag.confirm.rawValue, x: row(4), y: topY(y, H), w: W, h: H, act: .confirm)

        y += H + PAD
        _ = makeButton(withKey("Avanti", .advance),        tag: Tag.advance.rawValue, x: row(0), y: topY(y, H), w: W, h: H, act: .advance)
        _ = makeButton(withKey("Conf. continua", .cont),   tag: Tag.cont.rawValue,    x: row(1), y: topY(y, H), w: W, h: H, act: .cont)
        _ = makeButton(withKey("Canc. lettera", .delLetter), tag: Tag.delLetter.rawValue, x: row(2), y: topY(y, H), w: W, h: H, act: .delLetter)
        _ = makeButton(withKey("Canc. parola", .delWord),  tag: Tag.delWord.rawValue, x: row(3), y: topY(y, H), w: W, h: H, act: .delWord)

        y += H + PAD
        _ = makeButton(withKey(".", .dot),   tag: Tag.dot.rawValue,   x: row(0),         y: topY(y, H), w: W/2, h: H, act: .dot)
        _ = makeButton(withKey(",", .comma), tag: Tag.comma.rawValue, x: row(0) + W/2,   y: topY(y, H), w: W/2, h: H, act: .comma)
        _ = makeButton(withKey("?", .ques),  tag: Tag.ques.rawValue,  x: row(1),         y: topY(y, H), w: W/2, h: H, act: .ques)
        _ = makeButton(withKey("!", .excl),  tag: Tag.excl.rawValue,  x: row(1) + W/2,   y: topY(y, H), w: W/2, h: H, act: .excl)
        _ = makeButton(withKey("Read", .read),   tag: Tag.read.rawValue,   x: row(2), y: topY(y, H), w: W, h: H, act: .read)
        _ = makeButton(withKey("Write", .write), tag: Tag.write.rawValue,  x: row(3), y: topY(y, H), w: W, h: H, act: .write)
        _ = makeButton(withKey("Scarta", .discard), tag: Tag.discard.rawValue, x: row(4), y: topY(y, H), w: W, h: H, act: .discard)
    }

    @objc private func buttonClicked(_ sender: NSButton) {
        guard let tag = Tag(rawValue: sender.tag) else { return }
        switch tag {
        case .play: onPlay?()
        case .mode: onMode?()
        default:
            if let act = tag.act { onAction?(act) }
        }
    }

    func setStatus(active: Bool, mode: Mode) {
        status.stringValue = "Stato: \(active ? "ATTIVO" : "in pausa")  |  Modalità: \(mode.label)"
    }

    // Attiva/disattiva i bottoni secondo le azioni valide adesso (Availability).
    func updateButtonStates(_ a: Availability) {
        func en(_ act: Act, _ on: Bool) { buttons[act]?.isEnabled = on }
        en(.navPrev, a.navPrev)
        en(.navNext, a.navNext)
        en(.open, a.open)
        en(.roll, a.roll)
        en(.confirm, a.confirm)
        en(.advance, a.advance)
        en(.cont, a.advance)            // Conf. continua = Avanti nel MOTORE
        en(.delLetter, a.deleteLetter)
        en(.delWord, a.deleteWord)
        en(.dot, a.punct)
        en(.comma, a.punct)
        en(.ques, a.punct)
        en(.excl, a.punct)
        en(.read, a.read)
        en(.write, a.write)
        en(.discard, a.discard)
    }

    // Tag numerico dei bottoni -> Act (per performAction).
    private enum Tag: Int {
        case play = 1, mode, navPrev, navNext, open, roll, confirm
        case advance, cont, delLetter, delWord, dot, comma, ques, excl, read, write, discard
        var act: Act? {
            switch self {
            case .play, .mode: return nil
            case .navPrev: return .navPrev
            case .navNext: return .navNext
            case .open: return .open
            case .roll: return .roll
            case .confirm: return .confirm
            case .advance: return .advance
            case .cont: return .cont
            case .delLetter: return .delLetter
            case .delWord: return .delWord
            case .dot: return .dot
            case .comma: return .comma
            case .ques: return .ques
            case .excl: return .excl
            case .read: return .read
            case .write: return .write
            case .discard: return .discard
            }
        }
    }
}
