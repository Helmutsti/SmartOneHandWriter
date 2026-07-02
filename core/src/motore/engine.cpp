#include "motore/engine.hpp"
#include "utf8.hpp"   // onehand::utf8ToW / wToUtf8 (via sohw_core -> onehand_core)

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
void Engine::clear() { words_.clear(); sel_ = -1; open_ = -1; }

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
    if (words_.empty()) { sel_ = -1; return; }
    if (index < 0) index = 0;
    const int last = static_cast<int>(words_.size()) - 1;
    if (index > last) index = last;
    sel_ = index;
}

void Engine::closeOpen() {
    if (open_ < 0) return;
    words_[open_].state = WordState::Resolved;
    open_ = -1;
}

void Engine::navigatePrev() { closeOpen(); select(sel_ - 1); }
void Engine::navigateNext() { closeOpen(); select(sel_ + 1); }

void Engine::openSelected() {
    if (sel_ < 0 || sel_ >= static_cast<int>(words_.size())) return;
    open_ = sel_;
    words_[open_].state = WordState::Open;
    recomputeOpen();   // se Typed con celle, ripristina i candidati; su Loaded è no-op
}

void Engine::roll() {
    if (open_ < 0) return;
    Word& wd = words_[open_];
    if (wd.cands.size() < 2) return;
    wd.idx = (wd.idx + 1) % static_cast<int>(wd.cands.size());
    wd.text = wd.cands[wd.idx];
}

void Engine::typeKey(const std::string& sym) {
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
    if (open_ < 0) return;
    const int i = open_;
    open_ = -1;
    Word& wd = words_[i];
    if (wd.cells.empty() && wd.text.empty()) removeWordAt(i);   // parola vuota: rimuovi
    else wd.state = WordState::Resolved;                         // altrimenti chiudi e resta
}

void Engine::advance() {
    confirm();                                   // chiude l'eventuale parola aperta
    Word wd;
    wd.cls = WordClass::Text; wd.state = WordState::Open; wd.origin = WordOrigin::Typed;
    const int at = (sel_ < 0) ? 0 : sel_ + 1;
    words_.insert(words_.begin() + at, std::move(wd));
    sel_ = open_ = at;
}

void Engine::confirmContinue() { advance(); }    // = conferma + avanti

void Engine::punct(const std::string& sym) {
    confirm();
    Word p;
    p.cls = WordClass::Punct; p.state = WordState::Resolved; p.origin = WordOrigin::Typed; p.text = sym;
    const int at = (sel_ < 0) ? 0 : sel_ + 1;
    words_.insert(words_.begin() + at, std::move(p));
    sel_ = at;
    if (sym == "." || sym == "!" || sym == "?") advance();   // fine frase -> conferma continua automatica
}

void Engine::deleteLetter() {
    if (open_ < 0) return;
    Word& wd = words_[open_];
    if (!wd.cells.empty()) {
        wd.cells.pop_back();
        if (wd.cells.empty()) { const int i = open_; open_ = -1; removeWordAt(i); }
        else recomputeOpen();
    } else {
        // Parola senza celle (es. Loaded aperta): rimuove l'ultimo code point dal testo.
        std::wstring w = onehand::utf8ToW(wd.text);
        if (!w.empty()) w.pop_back();
        wd.text = onehand::wToUtf8(w);
        if (wd.text.empty()) { const int i = open_; open_ = -1; removeWordAt(i); }
    }
}

void Engine::deleteWord() {
    if (sel_ < 0) return;
    removeWordAt(sel_);
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
        r.spans.push_back(std::move(s));
    }
    for (const auto& s : r.spans) {
        if (s.spaceBefore) r.fullText.push_back(' ');
        r.fullText += s.text;
    }
    return r;
}

} // namespace motore
