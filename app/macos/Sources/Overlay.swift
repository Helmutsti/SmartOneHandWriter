// Overlay: box colorato senza bordi, sempre in primo piano, che mostra il buffer
// con la parola selezionata/aperta evidenziate e una riga di suggerimenti sotto.
// Porting di paintOverlay/OverlayProc/overlayClick di app/windows/main.cpp.
//
// - NSPanel .nonactivatingPanel: non ruba il focus all'app in cui si scrive
//   (equivalente di WS_EX_NOACTIVATE). Livello .floating = sempre sopra (TOPMOST).
// - Banda del testo (in alto) = trascinabile; banda dei suggerimenti = cliccabile
//   (equivalente della logica WM_NCHITTEST con kSugBandTop).

import AppKit

// Colori (mirror delle COSTANTI RGB di main.cpp).
private func rgb(_ r: Int, _ g: Int, _ b: Int) -> NSColor {
    NSColor(srgbRed: CGFloat(r)/255, green: CGFloat(g)/255, blue: CGFloat(b)/255, alpha: 1)
}
private let kBg     = rgb(43, 43, 43)
private let kFg     = rgb(240, 240, 240)
private let kSelBg  = rgb(58, 110, 165)
private let kOpenBg = rgb(217, 164, 65)
private let kTyped  = rgb(30, 30, 30)
private let kTail   = rgb(90, 74, 40)
private let kSugFg  = rgb(150, 150, 150)
private let kSugSel = rgb(58, 110, 165)

private let kSugBandTop: CGFloat = 38   // sopra = testo (drag); sotto = suggerimenti (click)

final class OverlayView: NSView {
    var spans: [RenderSpan] = []
    var suggestions: [String] = []
    var suggestionSel: Int = -1
    var onSuggestionClick: ((Int) -> Void)?

    private let textFont = NSFont.monospacedSystemFont(ofSize: 20, weight: .semibold)
    private let sugFont  = NSFont.systemFont(ofSize: 14)
    private struct SugHit { let x0: CGFloat; let x1: CGFloat; let idx: Int }
    private var sugHits: [SugHit] = []

    // Coordinate stile GDI: origine in alto a sinistra, y verso il basso.
    override var isFlipped: Bool { true }

    private func width(_ s: String, _ font: NSFont) -> CGFloat {
        (s as NSString).size(withAttributes: [.font: font]).width
    }

    override func draw(_ dirtyRect: NSRect) {
        let rc = bounds
        kBg.setFill()
        rc.fill()

        let textAttrs: [NSAttributedString.Key: Any] = [.font: textFont]
        let lineH = ("M" as NSString).size(withAttributes: textAttrs).height
        let spaceW = width(" ", textFont)
        let kCaretW: CGFloat = 3, kCaretAdv: CGFloat = 7

        // Larghezza totale per lo scorrimento (tiene visibile la coda del testo).
        var total: CGFloat = 0
        for s in spans {
            if s.spaceBefore { total += spaceW }
            if s.hl == .open && s.text.isEmpty { total += kCaretAdv; continue }
            total += width(s.text, textFont)
        }
        let scroll = total > rc.width - 12 ? total - (rc.width - 12) : 0

        var x: CGFloat = 6 - scroll
        let y: CGFloat = 8
        for s in spans {
            if s.spaceBefore { x += spaceW }

            // Slot vuoto in modifica: solo un cursore ambra.
            if s.hl == .open && s.text.isEmpty {
                kOpenBg.setFill()
                NSRect(x: x, y: y, width: kCaretW, height: lineH).fill()
                x += kCaretAdv
                continue
            }

            let w = width(s.text, textFont)
            if s.hl == .selected || s.hl == .open {
                (s.hl == .open ? kOpenBg : kSelBg).setFill()
                NSRect(x: x - 2, y: y - 2, width: w + 4, height: lineH + 4).fill()
            }

            // Parola aperta: prefisso DIGITATO (bianco + sottolineato) vs coda di
            // completamento (attenuata), per far vedere quante lettere si sono premute.
            if s.hl == .open && s.typed > 0 {
                let chars = Array(s.text)
                let n = min(s.typed, chars.count)
                let pre = String(chars[0..<n])
                let preW = width(pre, textFont)
                (pre as NSString).draw(at: NSPoint(x: x, y: y),
                                       withAttributes: [.font: textFont, .foregroundColor: kFg])
                if n < chars.count {
                    let tail = String(chars[n...])
                    (tail as NSString).draw(at: NSPoint(x: x + preW, y: y),
                                            withAttributes: [.font: textFont, .foregroundColor: kTail])
                }
                kTyped.setFill()
                NSRect(x: x, y: y + lineH, width: preW, height: 2).fill()   // sottolineatura
            } else {
                (s.text as NSString).draw(at: NSPoint(x: x, y: y),
                                          withAttributes: [.font: textFont, .foregroundColor: kFg])
            }
            x += w
        }

        // --- riga suggerimenti (sotto, font piccolo) --------------------------
        sugHits.removeAll()
        var sx: CGFloat = 6
        let sy = kSugBandTop + 3
        let sugH = ("M" as NSString).size(withAttributes: [.font: sugFont]).height
        for (i, t) in suggestions.enumerated() {
            let w = width(t, sugFont)
            if sx + w > rc.width - 6 { break }        // troncamento se non ci sta
            if i == suggestionSel {
                kSugSel.setFill()
                NSRect(x: sx - 3, y: sy - 1, width: w + 6, height: sugH + 2).fill()
            }
            (t as NSString).draw(at: NSPoint(x: sx, y: sy),
                                 withAttributes: [.font: sugFont,
                                                  .foregroundColor: i == suggestionSel ? kFg : kSugFg])
            sugHits.append(SugHit(x0: sx - 3, x1: sx + w + 3, idx: i))
            sx += w + 14
        }
    }

    // Banda testo (in alto) = trascina la finestra; banda suggerimenti = click.
    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)   // coord. flipped (y dall'alto)
        if p.y < kSugBandTop {
            window?.performDrag(with: event)                 // trascinamento nativo
            return
        }
        for h in sugHits where p.x >= h.x0 && p.x <= h.x1 {
            onSuggestionClick?(h.idx)
            return
        }
    }
}

final class OverlayPanel: NSPanel {
    let overlayView: OverlayView

    init() {
        let w: CGFloat = 900, h: CGFloat = 66
        overlayView = OverlayView(frame: NSRect(x: 0, y: 0, width: w, height: h))
        super.init(contentRect: NSRect(x: 0, y: 0, width: w, height: h),
                   styleMask: [.borderless, .nonactivatingPanel],
                   backing: .buffered, defer: false)
        isFloatingPanel = true
        level = .floating                       // sempre sopra (TOPMOST)
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        alphaValue = 0.92                        // ~ SetLayeredWindowAttributes 235/255
        hidesOnDeactivate = false
        ignoresMouseEvents = false               // riceve mouse (drag/click)
        collectionBehavior = [.canJoinAllSpaces, .stationary, .fullScreenAuxiliary]
        contentView = overlayView

        // Posizione iniziale: in basso, centrato (come sw/2, sh-160 su Windows).
        if let screen = NSScreen.main {
            let f = screen.visibleFrame
            setFrameOrigin(NSPoint(x: f.midX - w/2, y: f.minY + 120))
        }
    }

    // Un pannello borderless normalmente non puo' diventare key: teniamolo cosi'
    // per non rubare il focus, ma lasciamolo ricevere i click del mouse.
    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }

    func update(_ m: RenderModel) {
        overlayView.spans = m.spans
        overlayView.suggestions = m.suggestions
        overlayView.suggestionSel = m.suggestionSel
        if m.isEmpty {
            orderOut(nil)                        // sparisce a buffer vuoto
        } else {
            orderFrontRegardless()               // mostra senza attivarsi
            overlayView.needsDisplay = true
        }
    }
}
