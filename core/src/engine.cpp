// OneHand core - motore di composizione, modello T9.
//
// Tutto ruota attorno alla singola Parola. Ogni pressione di tasto aggiunge una
// Cella (il tasto premuto = un gruppo di lettere); il dizionario disambigua e il
// Roll cicla le collisioni. Gli spazi non esistono come dato: sono derivati al
// render, che unisce le parole con un separatore. Il motore possiede il testo
// canonico ed emette il diff minimo (prefisso comune) verso il campo con focus.
#include "onehand/engine.hpp"
#include "dictionary.hpp"
#include "alterations.hpp"

#include <algorithm>

namespace onehand {

// ------------------------------------------------------------------ Out
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
Engine::Engine() : dict_(new Dictionary()), predictor_(makeFrequencyPredictor()) {}
Engine::~Engine() = default;

void Engine::setConfig(const Config& cfg) { cfg_ = cfg; }
void Engine::loadWordlist(std::istream& in) { dict_->load(in); }
void Engine::setPredictor(std::unique_ptr<Predictor> p) {
    if (p) predictor_ = std::move(p);
}

// ------------------------------------------------------------------ helper
std::vector<wchar_t> Engine::groupFor(wchar_t key) const { return cfg_.keymap.groupOf(key); }

PredictContext Engine::buildContext() const {
    PredictContext ctx;
    const int n = static_cast<int>(doc_.words.size());
    const int o = (openIndex_ >= 0) ? openIndex_ : n;
    for (int i = 0; i < n; ++i) {
        if (i == openIndex_) continue;
        if (i < o) ctx.leftWords.push_back(displayOf(doc_.words[i]));
        else       ctx.rightWords.push_back(displayOf(doc_.words[i]));
    }
    ctx.sentenceStart = sentenceStart_ && (openIndex_ <= 0);
    return ctx;
}

// Ricostruisce i candidati della parola aperta dai tasti delle sue celle, li
// riordina col predittore e appende le alterazioni (case/accento) della parola
// reale selezionata, in coda alle reali.
void Engine::recompute(Word& w) {
    std::vector<std::vector<wchar_t>> groups;
    groups.reserve(w.cells.size());
    for (const Cell& c : w.cells) groups.push_back(groupFor(c.key));

    std::vector<std::wstring> base = dict_->computeCandidates(groups, cfg_.maxCandidates);
    base = predictor_->rankCandidates(buildContext(), base);
    if (base.empty()) {
        std::wstring ph;                      // nessun match: segnaposto = prima lettera di ogni gruppo
        for (const Cell& c : w.cells) { auto g = groupFor(c.key); ph.push_back(g.empty() ? c.key : g[0]); }
        base.push_back(ph);
    }

    w.cands = base;
    w.realCount = static_cast<int>(base.size());
    w.idx = 0;
    for (const std::wstring& a : alterationsOf(base[0]))
        if (std::find(w.cands.begin(), w.cands.end(), a) == w.cands.end())
            w.cands.push_back(a);
    syncGlyphs(w);
}

void Engine::syncGlyphs(Word& w) const {
    const std::wstring& sel = (w.idx >= 0 && w.idx < static_cast<int>(w.cands.size()))
                                  ? w.cands[w.idx] : std::wstring();
    for (std::size_t i = 0; i < w.cells.size(); ++i)
        w.cells[i].glyph = (i < sel.size()) ? sel[i] : w.cells[i].key;
}

std::wstring Engine::displayOf(const Word& w) const {
    std::wstring s;
    if (w.idx >= 0 && w.idx < static_cast<int>(w.cands.size())) {
        s = w.cands[w.idx];
    } else {
        for (const Cell& c : w.cells) { auto g = groupFor(c.key); s.push_back(g.empty() ? c.key : g[0]); }
    }
    if (w.capFirst) s = capitalizeFirst(s);
    return s;
}

// Testo dell'intero documento con gli spazi derivati; opzionalmente riporta la
// posizione del caret (coda della parola aperta, o fine testo).
std::wstring Engine::renderWithCaret(int* caretOut) const {
    std::wstring t;
    int caret = 0;
    for (std::size_t i = 0; i < doc_.words.size(); ++i) {
        if (i) t += L" ";                                   // separatore derivato (Text|Text)
        std::wstring d = displayOf(doc_.words[i]);
        if (static_cast<int>(i) == openIndex_) caret = static_cast<int>(t.size() + d.size());
        t += d;
    }
    if (openIndex_ < 0) caret = static_cast<int>(t.size());
    if (caretOut) *caretOut = caret;
    return t;
}

std::wstring Engine::renderText() const { return renderWithCaret(nullptr); }
int          Engine::caret() const { int c = 0; renderWithCaret(&c); return c; }

void Engine::emit(Out& out) {
    std::wstring cur = renderWithCaret(nullptr);
    std::size_t p = 0;
    const std::size_t lim = std::min(lastRender_.size(), cur.size());
    while (p < lim && lastRender_[p] == cur[p]) ++p;
    out.backspace(lastRender_.size() - p);
    out.insert(cur.substr(p));
    lastRender_ = cur;
}

bool Engine::hasWord() const {
    return openIndex_ >= 0 && !doc_.words[openIndex_].cells.empty();
}

// ------------------------------------------------------------------ struttura documento
void Engine::ensureOpenAtTail() {
    if (openIndex_ >= 0) return;
    Word w;
    w.capFirst = sentenceStart_;
    doc_.words.push_back(w);
    openIndex_ = static_cast<int>(doc_.words.size()) - 1;
}

int Engine::resolveCurrent() {
    if (openIndex_ < 0) return -1;
    int erased = -1;
    Word& w = doc_.words[openIndex_];
    if (w.cells.empty()) {
        erased = openIndex_;
        doc_.words.erase(doc_.words.begin() + openIndex_);
    } else {
        w.state = WordState::Resolved;
    }
    openIndex_ = -1;
    return erased;
}

void Engine::gotoWord(int target) {
    int erased = resolveCurrent();
    if (erased >= 0 && erased < target) --target;   // la rimozione ha spostato il target
    if (target < 0 || target >= static_cast<int>(doc_.words.size())) return;  // niente da aprire
    openIndex_ = target;
    Word& w = doc_.words[target];
    w.state = WordState::Open;
    if (w.cands.empty()) recompute(w);
}

// ------------------------------------------------------------------ azioni
void Engine::actLetter(wchar_t key) {
    ensureOpenAtTail();
    Word& w = doc_.words[openIndex_];
    w.cells.push_back({key, 0});
    recompute(w);
}

void Engine::actRoll() {
    if (openIndex_ < 0) return;
    Word& w = doc_.words[openIndex_];
    if (w.cands.size() <= 1) return;
    w.idx = (w.idx + 1) % static_cast<int>(w.cands.size());
    // se la selezione e' su una parola reale, rigenera le alterazioni perche' la
    // seguano (regola: le alterazioni seguono la reale a idx 0 o quella rollata).
    if (w.idx < w.realCount) {
        std::vector<std::wstring> reals(w.cands.begin(), w.cands.begin() + w.realCount);
        w.cands = reals;
        for (const std::wstring& a : alterationsOf(reals[w.idx]))
            if (std::find(w.cands.begin(), w.cands.end(), a) == w.cands.end())
                w.cands.push_back(a);
    }
    syncGlyphs(w);
}

void Engine::actConfirm() {
    if (openIndex_ < 0) return;
    resolveCurrent();
    sentenceStart_ = false;
}

void Engine::actConfirmNewWord() {
    actConfirm();
    ensureOpenAtTail();   // apre una nuova parola vuota a destra (spazio derivato)
}

void Engine::actDeleteChar() {
    if (openIndex_ < 0) {
        if (doc_.words.empty()) return;
        openIndex_ = static_cast<int>(doc_.words.size()) - 1;   // riapre l'ultima
        Word& lw = doc_.words[openIndex_];
        lw.state = WordState::Open;
        if (lw.cands.empty()) recompute(lw);
    }
    Word& w = doc_.words[openIndex_];
    if (w.cells.empty()) { actDeleteWord(); return; }
    w.cells.pop_back();
    if (w.cells.empty()) { actDeleteWord(); return; }
    recompute(w);
}

void Engine::actDeleteWord() {
    int target = (openIndex_ >= 0) ? openIndex_ : static_cast<int>(doc_.words.size()) - 1;
    if (target < 0) return;
    doc_.words.erase(doc_.words.begin() + target);
    int left = target - 1;
    if (left >= 0) {
        openIndex_ = left;
        Word& w = doc_.words[left];
        w.state = WordState::Open;
        if (w.cands.empty()) recompute(w);
    } else {
        openIndex_ = -1;
    }
}

// Inserisce una parola suggerita come parola risolta e apre una nuova parola
// vuota a destra per continuare. Le celle usano la lettera stessa come "tasto"
// (gruppo di 1), cosi', se riaperta, resta coerente col resto.
void Engine::actAcceptSuggestion(int index) {
    if (index < 0 || index >= static_cast<int>(nextWords_.size())) return;
    std::wstring w = nextWords_[index];
    if (w.empty()) return;

    int at;
    if (openIndex_ >= 0 && doc_.words[openIndex_].cells.empty()) {
        at = openIndex_;                                  // rimpiazza la parola vuota aperta
        doc_.words.erase(doc_.words.begin() + at);
    } else {
        resolveCurrent();                                 // chiudi la parola in corso
        at = static_cast<int>(doc_.words.size());         // inserisci in coda
    }

    Word nw;
    for (wchar_t ch : w) nw.cells.push_back({ch, ch});
    nw.cands.push_back(w);
    nw.realCount = 1;
    nw.idx = 0;
    nw.state = WordState::Resolved;
    nw.capFirst = sentenceStart_;

    doc_.words.insert(doc_.words.begin() + at, nw);
    sentenceStart_ = false;

    Word empty;                                           // nuova parola vuota per continuare
    doc_.words.insert(doc_.words.begin() + at + 1, empty);
    openIndex_ = at + 1;
}

// Ricalcola i suggerimenti di parola successiva: solo quando la parola aperta e'
// vuota o assente (a inizio parola); mentre si compone, i chip spariscono e il
// popup mostra i candidati della parola in corso.
void Engine::recomputeNextWords() {
    if (openIndex_ >= 0 && !doc_.words[openIndex_].cells.empty()) { nextWords_.clear(); return; }
    nextWords_ = predictor_->predictNextWord(buildContext(), cfg_.maxCandidates);
}

// ------------------------------------------------------------------ popup
PopupEffect Engine::buildPopup() const {
    PopupEffect p;
    if (openIndex_ < 0) return p;
    const Word& w = doc_.words[openIndex_];
    if (w.cands.size() <= 1) return p;
    std::wstring t;
    for (int i = 0; i < static_cast<int>(w.cands.size()); ++i) {
        if (i) t += L"   ";
        std::wstring c = w.cands[i];
        if (w.capFirst) c = capitalizeFirst(c);
        if (i == w.idx) { t += L"["; t += c; t += L"]"; }
        else t += c;
    }
    p.visible = true;
    p.text = t;
    return p;
}

// ------------------------------------------------------------------ ingressi pubblici
Effects Engine::onActionIndex(Action a, int index) {
    Out out;
    Effects fx;

    if (a == Action::Finalize) {
        resolveCurrent();
        doc_.words.clear();
        openIndex_ = -1;
        sentenceStart_ = true;
        lastRender_.clear();          // il campo va a capo: la nostra riga riparte da zero
        recomputeNextWords();
        fx.passThrough = true;
        fx.popup = buildPopup();
        return fx;
    }

    switch (a) {
        case Action::Letter:         actLetter(static_cast<wchar_t>(index)); break;
        case Action::Wildcard:       break;   // deprecato: no-op
        case Action::Accept:         actConfirm(); break;
        case Action::Rolling:        actRoll(); break;
        case Action::DeleteChar:     actDeleteChar(); break;
        case Action::DeleteWord:     actDeleteWord(); break;
        case Action::ConfirmNewWord: actConfirmNewWord(); break;
        case Action::OpenPrevWord: {
            int base = (openIndex_ >= 0) ? openIndex_ : static_cast<int>(doc_.words.size());
            gotoWord(base - 1);
            break;
        }
        case Action::OpenNextWord:
            if (openIndex_ >= 0) gotoWord(openIndex_ + 1);
            break;
        case Action::OpenWordAt:     gotoWord(index); break;
        case Action::AcceptSuggestion: actAcceptSuggestion(index); break;
        default: break;
    }

    emit(out);
    recomputeNextWords();
    fx.edits = std::move(out.edits);
    fx.popup = buildPopup();
    return fx;
}

Effects Engine::onAction(Action a, wchar_t letter) {
    // Per Letter, 'letter' e' il TASTO premuto (chiave del keymap).
    return onActionIndex(a, static_cast<int>(letter));
}

Effects Engine::onKey(const KeyEvent& key) {
    switch (key.kind) {
        case KeyKind::Letter:    return onAction(Action::Letter, key.letter);
        case KeyKind::Space:     return onAction(Action::ConfirmNewWord);
        case KeyKind::Backspace: return onAction(Action::DeleteChar);
        case KeyKind::Tab:       return onAction(Action::Rolling);
        case KeyKind::Enter:     return onAction(Action::Finalize);
    }
    return Effects{};
}

Effects Engine::onTimeout() { return Effects{}; }   // nessun doppio-tap nel modello T9

Effects Engine::previewWildcard() {                 // deprecato
    Effects fx;
    fx.popup = buildPopup();
    return fx;
}

Effects Engine::reset() {
    doc_.words.clear();
    openIndex_ = -1;
    lastRender_.clear();
    sentenceStart_ = true;
    nextWords_.clear();
    Effects fx;
    fx.popup.visible = false;
    fx.timer.action = TimerEffect::Action::Cancel;
    return fx;
}

} // namespace onehand
