// Mini-driver del C ABI (M6): esercita smartcore_c usando SOLO l'header C,
// per validare il layer FFI oltre alla GUI. Scrive una piccola wordlist su file
// temporaneo (l'ABI fa I/O lato C) e verifica match e modalita'.
#include "sohw/smartcore_c.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

static bool matchHas(sc_result* r, const char* w) {
    for (size_t i = 0; i < sc_match_count(r); ++i)
        if (std::strcmp(sc_match_word(r, i), w) == 0) return true;
    return false;
}

int main() {
    const char* wl = "abi_test_wl.txt";
    { std::ofstream o(wl); o << "sole\t100\nsono\t90\nsala\t80\ncasa\t70\ncasetta\t60\n"; }

    sc_core* c = sc_create(nullptr);
    assert(c);
    assert(sc_get_mode(c) == 0);                 // default T9
    assert(sc_load_wordlist(c, wl) == 1);

    // T9 "76" -> sole, sono.
    sc_result* r = sc_process(c, "", "", "76", 8, 5);
    assert(r);
    assert(sc_match_count(r) == 2);
    assert(std::strcmp(sc_match_word(r, 0), "sole") == 0);
    assert(sc_match_score(r, 0) == 0.0f);        // nessun modello: score 0
    assert(matchHas(r, "sono"));
    assert(std::strcmp(sc_match_word(r, 99), "") == 0);   // out of range sicuro
    sc_free_result(r);

    // Toggle a Literal: "cas" -> casa, casetta.
    sc_set_mode(c, 1);
    assert(sc_get_mode(c) == 1);
    sc_result* r2 = sc_process(c, "", "", "cas", 8, 5);
    assert(sc_match_count(r2) == 2);
    assert(std::strcmp(sc_match_word(r2, 0), "casa") == 0);
    sc_free_result(r2);

    sc_destroy(c);
    std::remove(wl);
    std::puts("abi_tests: OK (C ABI smartcore_c)");
    return 0;
}
