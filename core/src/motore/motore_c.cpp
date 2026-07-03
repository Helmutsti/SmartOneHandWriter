// SmartOneHandWriter - implementazione della C ABI del MOTORE (motore_c.h).
//
// Wrapper sottile su motore::Engine. Memoria posseduta dall'oggetto mo_engine:
// il render (span/suggerimenti/availability) e il testo di Write sono cachati e
// i getter restituiscono puntatori nella cache, validi fino alla prossima
// mo_render()/mo_write()/mo_destroy().

#include "motore/motore_c.h"
#include "motore/engine.hpp"
#include "onehand/types.hpp"   // onehand::Config / KeyMap
#include "utf8.hpp"            // onehand::utf8ToW

#include <fstream>
#include <string>

struct mo_engine {
    motore::Engine     engine;
    onehand::KeyMap    keymap;          // accumulata da mo_keymap_* prima di set_config
    motore::RenderModel render;         // snapshot dell'ultimo mo_render
    std::string        writeBuf;        // testo dell'ultimo mo_write
};

extern "C" {

mo_engine* mo_create(void) { return new mo_engine(); }
void       mo_destroy(mo_engine* mo) { delete mo; }

void mo_keymap_clear(mo_engine* mo) {
    if (mo) mo->keymap.groups.clear();
}

void mo_keymap_add(mo_engine* mo, const char* key_utf8, const char* letters_utf8) {
    if (!mo || !key_utf8 || !letters_utf8) return;
    std::wstring k = onehand::utf8ToW(key_utf8);
    if (k.empty()) return;
    mo->keymap.groups[k[0]] = onehand::utf8ToW(letters_utf8);
}

void mo_set_config(mo_engine* mo, int max_candidates) {
    if (!mo) return;
    onehand::Config cfg;
    if (!mo->keymap.groups.empty()) cfg.keymap = mo->keymap;
    if (max_candidates > 0) cfg.maxCandidates = max_candidates;
    mo->engine.setConfig(cfg);   // azzera dizionario/modello
}

int mo_load_wordlist(mo_engine* mo, const char* path) {
    if (!mo || !path) return 0;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    mo->engine.loadWordlist(in);
    return 1;
}

int mo_load_bigrams(mo_engine* mo, const char* bin_path) {
    if (!mo || !bin_path) return 0;
    mo->engine.loadBigramModel(bin_path);
    return 1;
}

void mo_set_mode(mo_engine* mo, int assisted) {
    if (mo) mo->engine.setMode(assisted != 0);
}

int mo_assisted(const mo_engine* mo) {
    return (mo && mo->engine.assisted()) ? 1 : 0;
}

void mo_load_resolved(mo_engine* mo, const char* utf8_text) {
    if (mo && utf8_text) mo->engine.loadResolved(utf8_text);
}

void mo_clear(mo_engine* mo) { if (mo) mo->engine.clear(); }

int mo_empty(const mo_engine* mo) {
    return (!mo || mo->engine.empty()) ? 1 : 0;
}

void mo_type_key(mo_engine* mo, const char* sym_utf8) {
    if (mo && sym_utf8) mo->engine.typeKey(sym_utf8);
}
void mo_navigate_prev(mo_engine* mo)    { if (mo) mo->engine.navigatePrev(); }
void mo_navigate_next(mo_engine* mo)    { if (mo) mo->engine.navigateNext(); }
void mo_open_selected(mo_engine* mo)    { if (mo) mo->engine.openSelected(); }
void mo_roll(mo_engine* mo)             { if (mo) mo->engine.roll(); }
void mo_confirm(mo_engine* mo)          { if (mo) mo->engine.confirm(); }
void mo_advance(mo_engine* mo)          { if (mo) mo->engine.advance(); }
void mo_confirm_continue(mo_engine* mo) { if (mo) mo->engine.confirmContinue(); }
void mo_punct(mo_engine* mo, const char* sym_utf8) {
    if (mo && sym_utf8) mo->engine.punct(sym_utf8);
}
void mo_delete_letter(mo_engine* mo)    { if (mo) mo->engine.deleteLetter(); }
void mo_delete_word(mo_engine* mo)      { if (mo) mo->engine.deleteWord(); }
void mo_accept_suggestion(mo_engine* mo, int k) {
    if (mo) mo->engine.acceptSuggestion(k);
}

const char* mo_write(mo_engine* mo) {
    if (!mo) return "";
    mo->writeBuf = mo->engine.write();
    return mo->writeBuf.c_str();
}

void mo_render(mo_engine* mo) {
    if (mo) mo->render = mo->engine.render();
}

size_t mo_span_count(const mo_engine* mo) {
    return mo ? mo->render.spans.size() : 0;
}
const char* mo_span_text(const mo_engine* mo, size_t i) {
    if (!mo || i >= mo->render.spans.size()) return "";
    return mo->render.spans[i].text.c_str();
}
int mo_span_hl(const mo_engine* mo, size_t i) {
    if (!mo || i >= mo->render.spans.size()) return MO_HL_NONE;
    return static_cast<int>(mo->render.spans[i].hl);
}
int mo_span_space_before(const mo_engine* mo, size_t i) {
    if (!mo || i >= mo->render.spans.size()) return 0;
    return mo->render.spans[i].spaceBefore ? 1 : 0;
}
int mo_span_typed(const mo_engine* mo, size_t i) {
    if (!mo || i >= mo->render.spans.size()) return 0;
    return mo->render.spans[i].typedCount;
}

size_t mo_suggestion_count(const mo_engine* mo) {
    return mo ? mo->render.suggestions.size() : 0;
}
const char* mo_suggestion_text(const mo_engine* mo, size_t i) {
    if (!mo || i >= mo->render.suggestions.size()) return "";
    return mo->render.suggestions[i].c_str();
}
int mo_suggestion_sel(const mo_engine* mo) {
    return mo ? mo->render.suggestionSel : -1;
}
int mo_suggestions_are_next(const mo_engine* mo) {
    return (mo && mo->render.suggestionsAreNext) ? 1 : 0;
}

void mo_availability(const mo_engine* mo, mo_avail* out) {
    if (!out) return;
    motore::Availability a;
    if (mo) a = mo->render.actions;
    out->navPrev      = a.navPrev;
    out->navNext      = a.navNext;
    out->open         = a.open;
    out->roll         = a.roll;
    out->confirm      = a.confirm;
    out->advance      = a.advance;
    out->deleteLetter = a.deleteLetter;
    out->deleteWord   = a.deleteWord;
    out->punct        = a.punct;
    out->read         = a.read;
    out->write        = a.write;
    out->discard      = a.discard;
}

} // extern "C"
