// Implementazione della C ABI: avvolge onehand::Engine e conserva gli effetti
// dell'ultimo evento per la lettura "pull".
#include "onehand/onehand_c.h"
#include "onehand/engine.hpp"

#include <fstream>

struct OnehandEngine {
    onehand::Engine engine;
    onehand::Effects last;
    onehand::Config  cfg;   // ultima configurazione applicata (per i getter onehand_config_*)
};

extern "C" {

OnehandEngine* onehand_create(void) { return new OnehandEngine(); }
void           onehand_destroy(OnehandEngine* e) { delete e; }

void onehand_set_config(OnehandEngine* e, const OnehandConfig* c) {
    if (!e || !c) return;
    onehand::Config cfg;
    if (c->available_keys) cfg.availableKeys = c->available_keys;
    cfg.wildcardAny   = c->wildcard_any != 0;
    cfg.maxCandidates = c->max_candidates;
    cfg.doublePressMs = c->double_press_ms;
    if (c->punctuation)   cfg.punctuation  = c->punctuation;
    if (c->wordlist_name) cfg.wordlistName = c->wordlist_name;
    e->engine.setConfig(cfg);
}

int onehand_load_wordlist_file(OnehandEngine* e, const char* path) {
    if (!e || !path) return 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    e->engine.loadWordlist(f);
    return 1;
}

void onehand_apply_config_json(OnehandEngine* e, const char* json) {
    if (!e) return;
    e->cfg = onehand::parseConfig(json ? json : "");
    e->engine.setConfig(e->cfg);
}
const wchar_t* onehand_config_available_keys(OnehandEngine* e) {
    return e ? e->cfg.availableKeys.c_str() : L"";
}
const wchar_t* onehand_config_wordlist_name(OnehandEngine* e) {
    return e ? e->cfg.wordlistName.c_str() : L"";
}
int onehand_config_double_press_ms(OnehandEngine* e) {
    return e ? e->cfg.doublePressMs : 280;
}

void onehand_on_key(OnehandEngine* e, int kind, wchar_t letter) {
    if (!e) return;
    onehand::KeyEvent k;
    k.kind = static_cast<onehand::KeyKind>(kind);
    k.letter = letter;
    e->last = e->engine.onKey(k);
}

void onehand_on_timeout(OnehandEngine* e) { if (e) e->last = e->engine.onTimeout(); }
void onehand_reset(OnehandEngine* e)      { if (e) e->last = e->engine.reset(); }

void onehand_on_action(OnehandEngine* e, int action, wchar_t letter) {
    if (!e) return;
    e->last = e->engine.onAction(static_cast<onehand::Action>(action), letter);
}
void onehand_preview_wildcard(OnehandEngine* e) {
    if (e) e->last = e->engine.previewWildcard();
}

int onehand_edit_count(OnehandEngine* e) {
    return e ? static_cast<int>(e->last.edits.size()) : 0;
}
int onehand_edit_backspaces(OnehandEngine* e, int i) {
    if (!e || i < 0 || i >= static_cast<int>(e->last.edits.size())) return 0;
    return e->last.edits[i].backspaces;
}
const wchar_t* onehand_edit_insert(OnehandEngine* e, int i) {
    if (!e || i < 0 || i >= static_cast<int>(e->last.edits.size())) return L"";
    return e->last.edits[i].insert.c_str();
}
int            onehand_popup_visible(OnehandEngine* e) { return e && e->last.popup.visible ? 1 : 0; }
const wchar_t* onehand_popup_text(OnehandEngine* e)    { return e ? e->last.popup.text.c_str() : L""; }
int            onehand_timer_action(OnehandEngine* e)  { return e ? static_cast<int>(e->last.timer.action) : 0; }
int            onehand_timer_ms(OnehandEngine* e)      { return e ? e->last.timer.ms : 0; }
int            onehand_pass_through(OnehandEngine* e)  { return e && e->last.passThrough ? 1 : 0; }

} // extern "C"
