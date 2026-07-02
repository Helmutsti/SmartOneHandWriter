#include "sohw/smartcore_c.h"
#include "sohw/core.hpp"
#include "onehand/types.hpp"   // onehand::parseConfig

#include <fstream>
#include <string>

// Wrapper C ABI sottile sopra sohw::Core. Memoria posseduta dal core.

struct sc_core {
    sohw::Core core;
    explicit sc_core(const onehand::Config& c) : core(c) {}
};

struct sc_result {
    sohw::CoreResult r;
};

extern "C" {

sc_core* sc_create(const char* config_json) {
    onehand::Config cfg = config_json ? onehand::parseConfig(config_json) : onehand::Config{};
    return new sc_core(cfg);
}

void sc_destroy(sc_core* core) { delete core; }

void sc_set_mode(sc_core* core, int mode) {
    if (core) core->core.setMode(mode == 1 ? sohw::InputMode::Literal : sohw::InputMode::T9);
}

int sc_get_mode(const sc_core* core) {
    if (!core) return 0;
    return core->core.mode() == sohw::InputMode::Literal ? 1 : 0;
}

int sc_load_wordlist(sc_core* core, const char* path) {
    if (!core || !path) return 0;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    core->core.loadWordlist(in);
    return 1;
}

int sc_load_bigrams(sc_core* core, const char* bin_path) {
    if (!core || !bin_path) return 0;
    core->core.loadBigramModel(bin_path);
    return 1;
}

sc_result* sc_process(sc_core* core, const char* left, const char* right,
                      const char* encoded, int topK, int nextN) {
    if (!core) return nullptr;
    sohw::Context ctx{ left ? left : "", right ? right : "" };
    sc_result* out = new sc_result();
    out->r = core->core.process(ctx, encoded ? encoded : "", topK, nextN);
    return out;
}

void sc_free_result(sc_result* res) { delete res; }

size_t sc_match_count(const sc_result* res) {
    return res ? res->r.matches.size() : 0;
}

const char* sc_match_word(const sc_result* res, size_t i) {
    if (!res || i >= res->r.matches.size()) return "";
    return res->r.matches[i].word.c_str();
}

float sc_match_score(const sc_result* res, size_t i) {
    if (!res || i >= res->r.matches.size()) return 0.0f;
    return res->r.matches[i].score;
}

size_t sc_next_count(const sc_result* res, size_t matchIdx) {
    if (!res || matchIdx >= res->r.nextByMatch.size()) return 0;
    return res->r.nextByMatch[matchIdx].size();
}

const char* sc_next_word(const sc_result* res, size_t matchIdx, size_t j) {
    if (!res || matchIdx >= res->r.nextByMatch.size()) return "";
    const auto& v = res->r.nextByMatch[matchIdx];
    if (j >= v.size()) return "";
    return v[j].word.c_str();
}

float sc_next_score(const sc_result* res, size_t matchIdx, size_t j) {
    if (!res || matchIdx >= res->r.nextByMatch.size()) return 0.0f;
    const auto& v = res->r.nextByMatch[matchIdx];
    if (j >= v.size()) return 0.0f;
    return v[j].score;
}

} // extern "C"
