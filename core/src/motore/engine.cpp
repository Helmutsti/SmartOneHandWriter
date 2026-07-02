#include "motore/engine.hpp"
#include "utf8.hpp"   // onehand::utf8ToW / wToUtf8 (via sohw_core -> onehand_core)

namespace motore {

// ------------------------------------------------------------------ classificazione
static bool isSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}
// Punteggiatura trattata come token a sé (coerente con il CORE e il piano §1.1).
static bool isPunctSep(wchar_t c) {
    switch (c) {
        case L',': case L'.': case L';': case L':': case L'!': case L'?':
        case L'"': case L'(': case L')': case L'-':
        case L'«': case L'»':   // « »
            return true;
        default: return false;
    }
}
// Apostrofo: dritto (') o tipografico (’ U+2019).
static bool isApostrophe(wchar_t c) { return c == L'\'' || c == L'’'; }

// ------------------------------------------------------------------ helper di spaziatura
static bool endsWithApostrophe(const std::string& t) {
    if (!t.empty() && t.back() == '\'') return true;
    if (t.size() >= 3 && t.compare(t.size() - 3, 3, "\xE2\x80\x99") == 0) return true;  // ’
    return false;
}
static bool isClosingPunct(const std::string& t) {
    return t == "." || t == "," || t == ";" || t == ":" || t == "!" || t == "?" ||
           t == ")" || t == "\xC2\xBB" /* » */;
}
static bool isOpeningPunct(const std::string& t) {
    return t == "(" || t == "\xC2\xAB" /* « */;
}

// Serve uno spazio tra 'prev' e 'cur'? (regole §1.1)
static bool needsSpace(const Word& prev, const Word& cur) {
    if (endsWithApostrophe(prev.text)) return false;             // elisione: dell'aria
    if (cur.cls == WordClass::Punct && isClosingPunct(cur.text)) return false;  // "ciao,"
    if (prev.cls == WordClass::Punct && isOpeningPunct(prev.text)) return false; // "(ciao"
    return true;
}

// ------------------------------------------------------------------ API
void Engine::clear() { words_.clear(); sel_ = -1; open_ = -1; }

void Engine::loadResolved(const std::string& utf8Text) {
    clear();
    const std::wstring w = onehand::utf8ToW(utf8Text);

    std::wstring cur;
    auto flushText = [&]() {
        if (cur.empty()) return;
        Word wd;
        wd.text   = onehand::wToUtf8(cur);
        wd.cls    = WordClass::Text;
        wd.state  = WordState::Resolved;
        wd.origin = WordOrigin::Loaded;
        words_.push_back(std::move(wd));
        cur.clear();
    };

    for (wchar_t c : w) {
        if (isSpace(c)) {
            flushText();
        } else if (isPunctSep(c)) {
            flushText();
            Word p;
            p.text   = onehand::wToUtf8(std::wstring(1, c));
            p.cls    = WordClass::Punct;
            p.state  = WordState::Resolved;
            p.origin = WordOrigin::Loaded;
            words_.push_back(std::move(p));
        } else if (isApostrophe(c)) {
            cur.push_back(c);
            flushText();                  // Strategia A: l'apostrofo chiude il token, restando attaccato
        } else {
            cur.push_back(c);             // lettere (accenti inclusi) e cifre; caso preservato
        }
    }
    flushText();

    sel_  = words_.empty() ? -1 : static_cast<int>(words_.size()) - 1;
    open_ = -1;
}

void Engine::select(int index) {
    if (words_.empty()) { sel_ = -1; return; }
    if (index < 0) index = 0;
    if (index >= static_cast<int>(words_.size())) index = static_cast<int>(words_.size()) - 1;
    sel_ = index;
}

void Engine::openSelected() {
    if (sel_ < 0 || sel_ >= static_cast<int>(words_.size())) return;
    open_ = sel_;
    words_[sel_].state = WordState::Open;
}

void Engine::closeOpen() {
    if (open_ < 0) return;
    words_[open_].state = WordState::Resolved;
    open_ = -1;
}

RenderModel Engine::render() const {
    RenderModel r;
    r.selection = sel_;
    r.open = open_;
    for (std::size_t i = 0; i < words_.size(); ++i) {
        RenderSpan s;
        s.text = words_[i].text;
        if (static_cast<int>(i) == open_)      s.hl = Highlight::Open;
        else if (static_cast<int>(i) == sel_)  s.hl = Highlight::Selected;
        else                                   s.hl = Highlight::None;
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
