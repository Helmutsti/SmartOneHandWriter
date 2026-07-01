#include "onehand/engine.hpp"
#include "dictionary.hpp"

#include <cwctype>

namespace onehand {

namespace {
// Varianti accentate proponibili quando lo scheletro e' una sola lettera (IT).
// towupper/towlower non maiuscolizzano gli accentati sotto la locale "C", quindi
// le coppie minuscola/maiuscola sono elencate esplicitamente.
struct AccentPair { wchar_t lo; wchar_t up; };

const std::vector<AccentPair>& accentsFor(wchar_t base) {
    static const std::vector<AccentPair> a = { {L'à', L'À'} };
    static const std::vector<AccentPair> e = { {L'è', L'È'}, {L'é', L'É'} };
    static const std::vector<AccentPair> i = { {L'ì', L'Ì'} };
    static const std::vector<AccentPair> o = { {L'ò', L'Ò'} };
    static const std::vector<AccentPair> u = { {L'ù', L'Ù'} };
    static const std::vector<AccentPair> none;
    switch (base) {
        case L'a': return a;
        case L'e': return e;
        case L'i': return i;
        case L'o': return o;
        case L'u': return u;
        default:   return none;
    }
}
} // namespace

// ------------------------------------------------------------------ Out
// Accumula la sequenza minima di EditEffect. Finche' si fanno cancellazioni e
// poi inserimenti, resta un'unica coppia; se un backspace arriva dopo un
// insert (interleaving), apre una nuova coppia.
void Engine::Out::backspace(std::size_t n) {
    if (n == 0) return;
    if (edits.empty() || !edits.back().insert.empty()) edits.push_back({});
    edits.back().backspaces += static_cast<int>(n);
}

void Engine::Out::insert(const std::wstring& s) {
    if (s.empty()) return;
    if (edits.empty()) edits.push_back({});
    edits.back().insert += s;
}

// ------------------------------------------------------------------ ciclo di vita
Engine::Engine() : dict_(new Dictionary()) {}
Engine::~Engine() = default;

void Engine::setConfig(const Config& cfg) { cfg_ = cfg; }

void Engine::loadWordlist(std::istream& in) {
    dict_->load(in, cfg_.availableKeys, cfg_.wildcardAny);
}

// ------------------------------------------------------------------ motore
std::wstring Engine::currentWord() const {
    if (pattern_.empty()) return L"";
    if (!cands_.empty()) return cands_[idx_];
    return pattern_;
}

void Engine::recompute() {
    cands_ = dict_->computeCandidates(pattern_, cfg_.maxCandidates);
    idx_ = 0;
    singleLetter_ = (!pattern_.empty() &&
                     pattern_.find(L'?') == std::wstring::npos &&
                     pattern_.size() == 1);
    if (singleLetter_) {
        // su una sola lettera proponi anche la maiuscola
        wchar_t lo = pattern_[0];
        wchar_t up = static_cast<wchar_t>(towupper(lo));
        cands_.clear();
        cands_.push_back(std::wstring(1, lo));
        if (up != lo) cands_.push_back(std::wstring(1, up));
        for (const auto& acc : accentsFor(lo)) {
            cands_.push_back(std::wstring(1, acc.lo));
            cands_.push_back(std::wstring(1, acc.up));
        }
        idx_ = (capNext_ && up != lo) ? 1 : 0;  // a inizio frase, default maiuscola
    }
    hasWord_ = !pattern_.empty();
}

std::wstring Engine::displayWord() const {
    if (pattern_.empty()) return L"";
    std::wstring w = currentWord();
    if (!singleLetter_ && capNext_ && !w.empty()) w[0] = static_cast<wchar_t>(towupper(w[0]));
    return w;
}

// ridisegna l'anteprima: diff tra il vecchio preview e quello nuovo
void Engine::render(Out& out) {
    std::wstring cur = punctMode_ ? std::wstring(1, cfg_.punctuation[punctIdx_]) : displayWord();
    if (cur != preview_) {
        out.backspace(preview_.size());
        out.insert(cur);
        preview_ = cur;
    }
}

// ------------------------------------------------------------------ punteggiatura
void Engine::enterPunctMode(Out& out) {
    if (cfg_.punctuation.empty()) return;
    punctMode_ = true;
    punctIdx_ = 0;
    // attacca il segno alla parola precedente: togli lo spazio finale
    if (trailingSpace_) { out.backspace(1); removedSpace_ = true; trailingSpace_ = false; }
    else removedSpace_ = false;
    preview_.clear();
    hasWord_ = true;
    render(out);
}

void Engine::cyclePunct(Out& out) {
    punctIdx_ = (punctIdx_ + 1) % static_cast<int>(cfg_.punctuation.size());
    render(out);
}

void Engine::cancelPunct(Out& out) {
    out.backspace(preview_.size());
    preview_.clear();
    if (removedSpace_) { out.insert(L" "); trailingSpace_ = true; }
    removedSpace_ = false;
    punctMode_ = false;
    hasWord_ = false;
}

void Engine::commitPunct(Out& out) {
    wchar_t p = cfg_.punctuation[punctIdx_];
    out.insert(L" ");                        // spazio spostato dopo il segno
    if (removedSpace_ && !committed_.empty() && !committed_.back().empty()
        && committed_.back().back() == L' ')
        committed_.back().pop_back();         // la parola prima ha perso lo spazio
    committed_.push_back(std::wstring(1, p) + L" ");
    committedPatterns_.push_back(L"");   // punteggiatura: nessuno scheletro da riaprire
    preview_.clear();
    punctMode_ = false; removedSpace_ = false; trailingSpace_ = true;
    if (p == L'.' || p == L'!' || p == L'?') capNext_ = true;  // maiuscola dopo il punto
    hasWord_ = false;
}

// ------------------------------------------------------------------ azioni
void Engine::actLiteral(Out& out, wchar_t ch) {
    if (punctMode_) cancelPunct(out);
    pattern_.push_back(ch); recompute(); render(out);
}

void Engine::actWildcard(Out& out) {
    if (punctMode_) return;
    pattern_.push_back(L'?'); recompute(); render(out);
}

void Engine::actTab(Out& out) {
    if (punctMode_) { cyclePunct(out); return; }
    if (!pattern_.empty()) {
        if (!cands_.empty()) { idx_ = (idx_ + 1) % static_cast<int>(cands_.size()); render(out); }
        return;
    }
    enterPunctMode(out);                      // a inizio parola: scegli punteggiatura
}

void Engine::actAccept(Out& out) {
    if (punctMode_) { commitPunct(out); return; }
    if (pattern_.empty()) return;
    std::wstring disp = displayWord();
    out.insert(L" ");
    committed_.push_back(disp + L" ");
    committedPatterns_.push_back(pattern_);
    pattern_.clear(); cands_.clear(); idx_ = 0; preview_.clear();
    singleLetter_ = false; trailingSpace_ = true; capNext_ = false;
    hasWord_ = false;
}

void Engine::actDeleteChar(Out& out) {
    if (punctMode_) { cancelPunct(out); return; }
    if (!pattern_.empty()) {
        pattern_.pop_back();
        recompute(); render(out);
        return;
    }
    if (committed_.empty()) return;

    // token di punteggiatura ("segno " oppure "segno"): cancellazione a due passi,
    // cosi' il Backspace toglie prima lo spazio e solo dopo il segno (come per le parole).
    std::wstring& back = committed_.back();
    if (!back.empty() && cfg_.punctuation.find(back[0]) != std::wstring::npos) {
        if (back.size() >= 2 && back.back() == L' ') {
            out.backspace(1);          // 1° passo: via solo lo spazio finale
            back.pop_back();           // "? " -> "?"
            trailingSpace_ = false;
        } else {
            out.backspace(back.size());  // 2° passo: via il segno...
            committed_.pop_back();
            if (!committedPatterns_.empty()) committedPatterns_.pop_back();
            if (!committed_.empty() && !committed_.back().empty()
                && committed_.back().back() != L' ') {
                committed_.back().push_back(L' ');   // ...e ripristina lo spazio della parola prima
                out.insert(L" ");
                trailingSpace_ = true;
            } else {
                trailingSpace_ = (!committed_.empty() && !committed_.back().empty()
                                  && committed_.back().back() == L' ');
            }
            capNext_ = false;          // rimosso il segno: non è più inizio frase
        }
        preview_.clear();
        hasWord_ = false;
        return;
    }

    // token di parola: riapri la parola meno l'ultimo carattere. Usa lo scheletro
    // originale (jolly '?' compresi), non il testo visualizzato: senza '?' il
    // dizionario non cerca piu' alternative (computeCandidates le propone solo in
    // presenza di un jolly), quindi ripartire dal testo letterale perdeva le
    // proposte del dizionario per una parola gia' confermata.
    std::wstring tok = back;
    std::wstring origPat = committedPatterns_.empty() ? L"" : committedPatterns_.back();
    committed_.pop_back();
    if (!committedPatterns_.empty()) committedPatterns_.pop_back();
    out.backspace(tok.size());                 // rimuovi tutto il token
    if (!tok.empty() && tok.back() == L' ') tok.pop_back();
    if (!origPat.empty())
        pattern_ = origPat.size() <= 1 ? L"" : origPat.substr(0, origPat.size() - 1);
    else
        pattern_ = tok.empty() ? L"" : tok.substr(0, tok.size() - 1);  // ripiego: testo letterale
    preview_.clear();
    trailingSpace_ = (!committed_.empty() && !committed_.back().empty()
                      && committed_.back().back() == L' ');
    recompute(); render(out);
}

void Engine::actDeleteWord(Out& out) {
    if (punctMode_) { cancelPunct(out); return; }
    if (!pattern_.empty()) {
        out.backspace(preview_.size());
        pattern_.clear(); cands_.clear(); idx_ = 0; preview_.clear();
        singleLetter_ = false; hasWord_ = false;
    } else if (!committed_.empty()) {
        std::wstring tok = committed_.back();
        committed_.pop_back();
        if (!committedPatterns_.empty()) committedPatterns_.pop_back();
        out.backspace(tok.size());
        trailingSpace_ = (!committed_.empty() && !committed_.back().empty()
                          && committed_.back().back() == L' ');
    }
}

void Engine::actFinalizeOnEnter(Out& out) {
    if (punctMode_) { commitPunct(out); }
    else if (!pattern_.empty()) {
        committed_.push_back(displayWord());   // resta nel campo, senza spazio
        committedPatterns_.push_back(pattern_);
        preview_.clear();
    }
    pattern_.clear(); cands_.clear(); idx_ = 0;
    singleLetter_ = false; trailingSpace_ = false; capNext_ = true;  // nuova riga
    hasWord_ = false;
}

void Engine::resetComposition() {
    pattern_.clear(); cands_.clear(); idx_ = 0; preview_.clear();
    committed_.clear(); committedPatterns_.clear(); pending_ = false; hasWord_ = false;
    previewActive_ = false; previewCands_.clear();
    capNext_ = true; trailingSpace_ = false; singleLetter_ = false;
    punctMode_ = false; punctIdx_ = 0; removedSpace_ = false;
}

// ------------------------------------------------------------------ doppia pressione
void Engine::doSingle(Out& out, KeyKind k) {
    if (k == KeyKind::Space) actWildcard(out);
    else if (k == KeyKind::Backspace) actDeleteChar(out);
}

void Engine::doDouble(Out& out, KeyKind k) {
    if (k == KeyKind::Space) actAccept(out);
    else if (k == KeyKind::Backspace) actDeleteWord(out);
}

// ------------------------------------------------------------------ popup
PopupEffect Engine::buildPopup() const {
    PopupEffect p;

    // tavolozza punteggiatura (ha la precedenza)
    if (punctMode_) {
        std::wstring t;
        for (int i = 0; i < static_cast<int>(cfg_.punctuation.size()); ++i) {
            if (i) t += L"  ";
            if (i == punctIdx_) { t += L"["; t += cfg_.punctuation[i]; t += L"]"; }
            else t += cfg_.punctuation[i];
        }
        p.visible = true;
        p.text = t;
        return p;
    }

    // mentre lo spazio (jolly) e' in attesa del doppio-tap, mostra in anteprima
    // i candidati che si otterrebbero col jolly; altrimenti le alternative reali.
    const std::vector<std::wstring>& list = previewActive_ ? previewCands_ : cands_;
    const int sel = previewActive_ ? 0 : idx_;
    if (list.size() <= 1) { p.visible = false; return p; }

    std::wstring t;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (i) t += L"   ";
        if (i == sel) { t += L"["; t += list[i]; t += L"]"; }
        else t += list[i];
    }
    p.visible = true;
    p.text = t;
    return p;
}

// ------------------------------------------------------------------ ingressi pubblici
Effects Engine::onKey(const KeyEvent& key) {
    Out out;
    Effects fx;
    previewActive_ = false;   // l'anteprima vale solo finche' lo spazio resta in attesa

    if (key.kind == KeyKind::Space || key.kind == KeyKind::Backspace) {
        if (pending_ && pendingKey_ == key.kind) {       // seconda pressione -> DOPPIA
            pending_ = false;
            fx.timer.action = TimerEffect::Action::Cancel;
            doDouble(out, key.kind);
        } else {                                         // prima pressione -> attende
            if (pending_) doSingle(out, pendingKey_);     // un altro tasto era in attesa: risolvilo
            pending_ = true;
            pendingKey_ = key.kind;
            fx.timer.action = TimerEffect::Action::Start; // Start = riavvia (il frontend kill+set)
            fx.timer.ms = cfg_.doublePressMs;
            // anteprima: i candidati del jolly, senza ancora applicarlo allo scheletro
            if (key.kind == KeyKind::Space && !pattern_.empty()) {
                previewCands_ = dict_->computeCandidates(pattern_ + L'?', cfg_.maxCandidates);
                previewActive_ = true;
            }
        }
    } else {
        if (pending_) {
            doSingle(out, pendingKey_);
            pending_ = false;
            fx.timer.action = TimerEffect::Action::Cancel;
        }
        switch (key.kind) {
            case KeyKind::Tab:    actTab(out); break;
            case KeyKind::Enter:  actFinalizeOnEnter(out); fx.passThrough = true; break;
            case KeyKind::Letter: actLiteral(out, key.letter); break;
            default: break;
        }
    }

    fx.edits = std::move(out.edits);
    fx.popup = buildPopup();
    popupText_ = fx.popup.text;
    return fx;
}

Effects Engine::onTimeout() {
    Out out;
    Effects fx;
    previewActive_ = false;   // il jolly viene applicato qui: tornano i candidati reali
    if (pending_) {
        KeyKind k = pendingKey_;
        pending_ = false;
        doSingle(out, k);
    }
    fx.edits = std::move(out.edits);
    fx.popup = buildPopup();
    popupText_ = fx.popup.text;
    // il timer e' gia' scaduto: nessuna azione sul timer
    return fx;
}

// Percorso esplicito: esegue un'azione gia' risolta dal frontend. Nessun timer
// qui (il frontend possiede il timing del doppio-tap).
Effects Engine::onAction(Action a, wchar_t letter) {
    Out out;
    Effects fx;
    previewActive_ = false;   // l'azione risolve l'eventuale attesa: via l'anteprima
    switch (a) {
        case Action::Wildcard:   actWildcard(out); break;
        case Action::Accept:     actAccept(out); break;
        case Action::Rolling:    actTab(out); break;
        case Action::DeleteChar: actDeleteChar(out); break;
        case Action::DeleteWord: actDeleteWord(out); break;
        case Action::Finalize:   actFinalizeOnEnter(out); fx.passThrough = true; break;
        case Action::Letter:     actLiteral(out, letter); break;
    }
    fx.edits = std::move(out.edits);
    fx.popup = buildPopup();
    popupText_ = fx.popup.text;
    return fx;
}

Effects Engine::previewWildcard() {
    Effects fx;
    if (!pattern_.empty()) {
        previewCands_ = dict_->computeCandidates(pattern_ + L'?', cfg_.maxCandidates);
        previewActive_ = true;
    }
    fx.popup = buildPopup();
    popupText_ = fx.popup.text;
    return fx;
}

Effects Engine::reset() {
    resetComposition();
    Effects fx;
    fx.popup.visible = false;
    fx.timer.action = TimerEffect::Action::Cancel;
    return fx;
}

} // namespace onehand
