#include "motore/engine.hpp"
#include "utf8.hpp"   // onehand::utf8ToW / wToUtf8 (via sohw_core -> onehand_core)

#include <algorithm>

namespace motore {

// ------------------------------------------------------------------ classificazione
static bool isSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}
static bool isPunctSep(wchar_t c) {
    switch (c) {
        case L',': case L'.': case L';': case L':': case L'!': case L'?':
        case L'"': case L'(': case L')': case L'-':
        case L'«': case L'»':
            return true;
        default: return false;
    }
}
static bool isApostrophe(wchar_t c) { return c == L'\'' || c == L'’'; }

// ------------------------------------------------------------------ spaziatura (§1.1)
static bool endsWithApostrophe(const std::string& t) {
    if (!t.empty() && t.back() == '\'') return true;
    if (t.size() >= 3 && t.compare(t.size() - 3, 3, "\xE2\x80\x99") == 0) return true;
    return false;
}
static bool isClosingPunct(const std::string& t) {
    return t == "." || t == "," || t == ";" || t == ":" || t == "!" || t == "?" ||
           t == ")" || t == "\xC2\xBB";
}
static bool isOpeningPunct(const std::string& t) {
    return t == "(" || t == "\xC2\xAB";
}
static bool needsSpace(const Word& prev, const Word& cur) {
    if (endsWithApostrophe(prev.text)) return false;
    if (cur.cls == WordClass::Punct && isClosingPunct(cur.text)) return false;
    if (prev.cls == WordClass::Punct && isOpeningPunct(prev.text)) return false;
    return true;
}

// Unisce le parole in [begin,end) applicando le regole di spaziatura (senza spazio
// iniziale). Usato per costruire il contesto sinistro/destro per il CORE.
static std::string joinRange(const std::vector<Word>& w, int begin, int end) {
    std::string out;
    for (int i = begin; i < end; ++i) {
        if (i > begin && needsSpace(w[i - 1], w[i])) out.push_back(' ');
        out += w[i].text;
    }
    return out;
}

// ------------------------------------------------------------------ risorse / modalità
void Engine::setConfig(const onehand::Config& cfg) {
    core_ = sohw::Core(cfg);
    core_.setMode(assisted_ ? sohw::InputMode::T9 : sohw::InputMode::Literal);
}
void Engine::loadWordlist(std::istream& in) { core_.loadWordlist(in); }
void Engine::loadBigramModel(const std::string& binPath) { core_.loadBigramModel(binPath); }
void Engine::setMode(bool assisted) {
    assisted_ = assisted;
    core_.setMode(assisted ? sohw::InputMode::T9 : sohw::InputMode::Literal);
}

// ------------------------------------------------------------------ documento (M1)
void Engine::clear() { words_.clear(); sel_ = -1; open_ = -1; nextSel_ = -1; }

void Engine::loadResolved(const std::string& utf8Text) {
    clear();
    const std::wstring w = onehand::utf8ToW(utf8Text);
    std::wstring cur;
    auto flushText = [&]() {
        if (cur.empty()) return;
        Word wd;
        wd.text = onehand::wToUtf8(cur);
        wd.cls = WordClass::Text; wd.state = WordState::Resolved; wd.origin = WordOrigin::Loaded;
        words_.push_back(std::move(wd));
        cur.clear();
    };
    for (wchar_t c : w) {
        if (isSpace(c)) { flushText(); }
        else if (isPunctSep(c)) {
            flushText();
            Word p;
            p.text = onehand::wToUtf8(std::wstring(1, c));
            p.cls = WordClass::Punct; p.state = WordState::Resolved; p.origin = WordOrigin::Loaded;
            words_.push_back(std::move(p));
        } else if (isApostrophe(c)) { cur.push_back(c); flushText(); }
        else { cur.push_back(c); }
    }
    flushText();
    sel_  = words_.empty() ? -1 : static_cast<int>(words_.size()) - 1;
    open_ = -1;
}

// ------------------------------------------------------------------ cursori / azioni
void Engine::select(int index) {  // dichiarata in M1; qui la teniamo per navigazione
    nextSel_ = -1;
    if (words_.empty()) { sel_ = -1; return; }
    if (index < 0) index = 0;
    const int last = static_cast<int>(words_.size()) - 1;
    if (index > last) index = last;
    sel_ = index;
}

void Engine::closeOpen() {
    if (open_ < 0) return;
    const int i = open_;
    open_ = -1;
    Word& wd = words_[i];
    // Uno slot rimasto vuoto non deve "colare" nel documento come token vuoto:
    // lasciare la parola aperta = scartarla.
    if (wd.cells.empty() && wd.text.empty()) removeWordAt(i);
    else wd.state = WordState::Resolved;
}

void Engine::navigatePrev() {
    // Se l'aperta è uno slot vuoto, closeOpen la rimuove e sposta già la selezione
    // sulla parola a sinistra: non decrementare oltre.
    const bool emptySlot = (open_ >= 0 && words_[open_].cells.empty() && words_[open_].text.empty());
    closeOpen();
    if (!emptySlot) select(sel_ - 1);
}
void Engine::navigateNext() { closeOpen(); select(sel_ + 1); }

void Engine::openSelected() {
    nextSel_ = -1;
    if (sel_ < 0 || sel_ >= static_cast<int>(words_.size())) return;
    open_ = sel_;
    words_[open_].state = WordState::Open;
    recomputeOpen();   // se Typed con celle, ripristina i candidati; su Loaded è no-op
}

void Engine::roll() {
    // Parola aperta con candidati: cicla il candidato mostrato.
    if (!nextWordActive()) {
        Word& wd = words_[open_];
        if (wd.cands.size() < 2) return;
        wd.idx = (wd.idx + 1) % static_cast<int>(wd.cands.size());
        wd.text = wd.cands[wd.idx];
        return;
    }
    // Altrimenti: scorre l'evidenziatore sulla riga next-word.
    const int n = static_cast<int>(computeNextWords(maxSug_).size());
    nextSel_ = (n <= 0) ? -1 : (nextSel_ + 1) % n;
}

void Engine::typeKey(const std::string& sym) {
    nextSel_ = -1;
    // Nessuna parola aperta → apri una nuova parola Typed subito dopo la selezione.
    if (open_ < 0) {
        Word wd;
        wd.cls = WordClass::Text; wd.state = WordState::Open; wd.origin = WordOrigin::Typed;
        int at = (sel_ < 0) ? 0 : sel_ + 1;
        words_.insert(words_.begin() + at, std::move(wd));
        sel_ = open_ = at;
    }
    Word& wd = words_[open_];
    if (wd.origin == WordOrigin::Loaded) return;   // D11-ii: non si digita dentro una Loaded
    wd.origin = WordOrigin::Typed;
    wd.cells.push_back(sym);
    recomputeOpen();
}

// ------------------------------------------------------------------ composizione (M3)
void Engine::removeWordAt(int i) {
    if (i < 0 || i >= static_cast<int>(words_.size())) return;
    words_.erase(words_.begin() + i);
    const int n = static_cast<int>(words_.size());
    if (open_ == i) open_ = -1; else if (open_ > i) --open_;
    if (sel_ > i) --sel_; else if (sel_ == i) sel_ = i - 1;   // vai alla precedente
    if (n == 0) { sel_ = -1; open_ = -1; }
    else if (sel_ < 0) sel_ = 0;
    else if (sel_ >= n) sel_ = n - 1;
}

void Engine::confirm() {
    // Riga suggerimenti in modo next-word: se una voce è evidenziata, Conferma la inserisce;
    // se è aperto uno slot vuoto senza scelta, lo scarta.
    if (nextWordActive()) {
        if (nextSel_ >= 0) { acceptSuggestion(nextSel_); return; }
        if (open_ >= 0) { const int i = open_; open_ = -1; removeWordAt(i); }
        return;
    }
    // Parola aperta con contenuto: chiude al candidato corrente (già in wd.text) e resta.
    const int i = open_;
    open_ = -1;
    words_[i].state = WordState::Resolved;
}

void Engine::advance() {
    nextSel_ = -1;                               // "avanti" non accetta un next-word
    confirm();                                   // chiude l'eventuale parola aperta
    Word wd;
    wd.cls = WordClass::Text; wd.state = WordState::Open; wd.origin = WordOrigin::Typed;
    const int at = (sel_ < 0) ? 0 : sel_ + 1;
    words_.insert(words_.begin() + at, std::move(wd));
    sel_ = open_ = at;
}

void Engine::confirmContinue() { advance(); }    // = conferma + avanti

void Engine::punct(const std::string& sym) {
    nextSel_ = -1;
    confirm();
    Word p;
    p.cls = WordClass::Punct; p.state = WordState::Resolved; p.origin = WordOrigin::Typed; p.text = sym;
    const int at = (sel_ < 0) ? 0 : sel_ + 1;
    words_.insert(words_.begin() + at, std::move(p));
    sel_ = at;
    if (sym == "." || sym == "!" || sym == "?") advance();   // fine frase -> conferma continua automatica
}

void Engine::deleteLetter() {
    nextSel_ = -1;
    if (open_ < 0) return;
    Word& wd = words_[open_];
    const bool hasCells = !wd.cells.empty();
    const bool hasText  = !wd.text.empty();

    // Slot GIÀ vuoto (secondo Canc.): rimuove la parola e seleziona la precedente
    // (chiusa: non la riapre). È l'unico modo per attraversare il confine di parola.
    if (!hasCells && !hasText) { const int i = open_; open_ = -1; removeWordAt(i); return; }

    if (hasCells) {
        wd.cells.pop_back();
        if (!wd.cells.empty()) { recomputeOpen(); return; }
    } else {
        // Parola senza celle (Loaded aperta): rimuove l'ultimo code point dal testo.
        std::wstring w = onehand::utf8ToW(wd.text);
        if (!w.empty()) w.pop_back();
        wd.text = onehand::wToUtf8(w);
        if (!wd.text.empty()) return;
    }
    // Appena svuotata: NON si rimuove né si chiude. Resta APERTA e VUOTA, pronta a
    // ridigitare; una Loaded svuotata diventa uno slot Typed riscrivibile (D-decisione 2).
    wd.origin = WordOrigin::Typed;
    wd.cands.clear();
    wd.idx = 0;
    wd.text.clear();
}

void Engine::deleteWord() {
    nextSel_ = -1;
    if (sel_ < 0) return;
    removeWordAt(sel_);
}

// ------------------------------------------------------------------ suggerimenti / next-word
bool Engine::nextWordActive() const {
    // Riga in modo next-word quando non c'è parola aperta o è uno slot vuoto.
    return open_ < 0 || (words_[open_].cells.empty() && words_[open_].text.empty());
}

std::vector<std::string> Engine::computeNextWords(int n) const {
    // Punto d'inserimento: lo slot aperto se c'è, altrimenti subito dopo la selezione.
    int at = (open_ >= 0) ? open_ : (sel_ + 1);
    if (at < 0) at = 0;
    if (at > static_cast<int>(words_.size())) at = static_cast<int>(words_.size());
    sohw::Context ctx{ joinRange(words_, 0, at),
                       joinRange(words_, at, static_cast<int>(words_.size())) };
    std::vector<std::string> out;
    for (const auto& s : core_.nextWords(ctx, n)) out.push_back(s.word);
    return out;
}

void Engine::acceptSuggestion(int k) {
    // Candidati della parola aperta: scegli k e conferma (resta sul posto).
    if (open_ >= 0 && !words_[open_].cands.empty()) {
        Word& wd = words_[open_];
        if (k < 0 || k >= static_cast<int>(wd.cands.size())) return;
        wd.idx = k;
        wd.text = wd.cands[k];
        confirm();
        return;
    }
    // Next-word: inserisci la parola scelta come risolta, poi apri una nuova a destra.
    std::vector<std::string> nx = computeNextWords(maxSug_);
    if (k < 0 || k >= static_cast<int>(nx.size())) return;
    nextSel_ = -1;
    if (open_ >= 0) {
        // Riempi lo slot vuoto aperto con la parola scelta.
        Word& wd = words_[open_];
        wd.cells.clear(); wd.cands.clear(); wd.idx = 0;
        wd.text = nx[k];
        wd.cls = WordClass::Text; wd.origin = WordOrigin::Typed; wd.state = WordState::Resolved;
        sel_ = open_; open_ = -1;
    } else {
        Word wd;
        wd.cls = WordClass::Text; wd.state = WordState::Resolved; wd.origin = WordOrigin::Typed;
        wd.text = nx[k];
        const int at = (sel_ < 0) ? 0 : sel_ + 1;
        words_.insert(words_.begin() + at, std::move(wd));
        sel_ = at;
    }
    advance();   // apre una nuova parola vuota a destra, pronta per il prossimo next-word
}

// ------------------------------------------------------------------ disponibilità azioni
Availability Engine::availability(const std::vector<std::string>& sugg,
                                  bool suggAreNext) const {
    Availability a;
    const int n = static_cast<int>(words_.size());
    // Slot aperto vuoto: navigarci "indietro" lo scarta e sposta la selezione a sinistra.
    const bool emptyOpen = (open_ >= 0 &&
                            words_[open_].cells.empty() && words_[open_].text.empty());

    a.navNext      = (sel_ >= 0) && (sel_ < n - 1);
    a.navPrev      = (n > 0) && (sel_ > 0 || emptyOpen);
    a.open         = (sel_ >= 0) && (open_ != sel_);
    // Roll: se la riga è next-word cicla l'evidenziatore (serve ≥2 voci); altrimenti
    // cicla i candidati della parola aperta (serve ≥2 candidati).
    a.roll         = suggAreNext ? (static_cast<int>(sugg.size()) >= 2)
                                 : (open_ >= 0 && words_[open_].cands.size() >= 2);
    // Conferma: chiude la parola aperta (anche uno slot vuoto -> lo scarta) oppure
    // accetta il next-word evidenziato.
    a.confirm      = (open_ >= 0) || (suggAreNext && nextSel_ >= 0);
    a.advance      = true;                 // "nuova parola" (anche Conf. continua): sempre
    a.deleteLetter = (open_ >= 0);
    a.deleteWord   = (sel_ >= 0);
    a.punct        = true;
    a.read         = true;
    a.write        = (n > 0);
    a.discard      = (n > 0);
    return a;
}

// ------------------------------------------------------------------ integrazione CORE
void Engine::recomputeOpen() {
    if (open_ < 0) return;
    Word& wd = words_[open_];
    if (wd.cells.empty()) return;   // niente da decodificare (es. parola Loaded)

    std::string encoded;
    for (const auto& s : wd.cells) encoded += s;

    sohw::Context ctx{ joinRange(words_, 0, open_),
                       joinRange(words_, open_ + 1, static_cast<int>(words_.size())) };
    sohw::CoreResult res = core_.process(ctx, encoded, maxCands_, /*nextN=*/0);

    std::vector<std::string> cands;
    for (const auto& m : res.matches) cands.push_back(m.word);

    if (!assisted_) {
        // Classica: il testo letterale è sempre il primo candidato (B6).
        std::vector<std::string> out;
        out.push_back(encoded);
        for (const auto& c : cands) if (c != encoded) out.push_back(c);
        cands.swap(out);
    }

    wd.cands = std::move(cands);
    wd.idx = 0;
    // Fallback: se nessun candidato (T9 senza match), mostra il codice digitato.
    wd.text = wd.cands.empty() ? encoded : wd.cands[0];
}

// ------------------------------------------------------------------ read / write (M4)
std::string Engine::currentText() const { return render().fullText; }

std::string Engine::write() {
    if (open_ >= 0) confirm();
    std::string t = render().fullText;
    clear();
    return t;
}

// ------------------------------------------------------------------ render (M1)
RenderModel Engine::render() const {
    RenderModel r;
    r.selection = sel_; r.open = open_;
    for (std::size_t i = 0; i < words_.size(); ++i) {
        RenderSpan s;
        s.text = words_[i].text;
        if (static_cast<int>(i) == open_)     s.hl = Highlight::Open;
        else if (static_cast<int>(i) == sel_) s.hl = Highlight::Selected;
        else                                  s.hl = Highlight::None;
        s.spaceBefore = (i > 0) && needsSpace(words_[i - 1], words_[i]);
        // Lettere effettivamente digitate (solo per la parola aperta): il FE ne
        // sottolinea il prefisso, così si distingue dal completamento del dizionario.
        s.typedCount = (static_cast<int>(i) == open_)
                           ? static_cast<int>(words_[i].cells.size()) : 0;
        r.spans.push_back(std::move(s));
    }
    for (const auto& s : r.spans) {
        if (s.spaceBefore) r.fullText.push_back(' ');
        r.fullText += s.text;
    }

    // Riga suggerimenti: candidati della parola aperta, oppure next-word dal contesto.
    if (nextWordActive()) {
        r.suggestions = computeNextWords(maxSug_);
        r.suggestionSel = nextSel_;
        r.suggestionsAreNext = true;
    } else {
        const Word& wd = words_[open_];
        const int lim = std::min<int>(maxSug_, static_cast<int>(wd.cands.size()));
        r.suggestions.assign(wd.cands.begin(), wd.cands.begin() + lim);
        r.suggestionSel = (wd.idx < lim) ? wd.idx : (lim > 0 ? 0 : -1);
        r.suggestionsAreNext = false;
    }

    // Disponibilità dei comandi nello stato corrente (riusa i suggerimenti appena
    // calcolati per il caso Roll/next-word).
    r.actions = availability(r.suggestions, r.suggestionsAreNext);
    return r;
}

} // namespace motore
