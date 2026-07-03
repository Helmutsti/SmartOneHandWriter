// Mini-driver del C ABI del MOTORE: esercita motore_c usando SOLO l'header C,
// per validare il layer FFI che il frontend macOS (Swift) usa al posto del C++
// diretto. Scrive una piccola wordlist su file temporaneo (l'ABI fa I/O lato C).
#include "motore/motore_c.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

static bool suggHas(mo_engine* mo, const char* w) {
    for (size_t i = 0; i < mo_suggestion_count(mo); ++i)
        if (std::strcmp(mo_suggestion_text(mo, i), w) == 0) return true;
    return false;
}

// Testo concatenato di tutti gli span (per verificare il render del documento).
static std::string spansText(mo_engine* mo) {
    std::string out;
    for (size_t i = 0; i < mo_span_count(mo); ++i) {
        if (mo_span_space_before(mo, i)) out += ' ';
        out += mo_span_text(mo, i);
    }
    return out;
}

int main() {
    const char* wl = "motore_c_test_wl.txt";
    { std::ofstream o(wl); o << "sole\t100\nsono\t90\nsala\t80\ncasa\t70\ncasetta\t60\n"; }

    mo_engine* mo = mo_create();
    assert(mo);
    assert(mo_empty(mo) == 1);
    mo_set_config(mo, 8);                       // keymap di default (cifre): PRIMA del load
    assert(mo_load_wordlist(mo, wl) == 1);

    // --- Classica (Literal): "cas" -> casa, casetta (come abi_tests) ----------
    mo_set_mode(mo, 0);
    assert(mo_assisted(mo) == 0);
    mo_type_key(mo, "c");
    mo_type_key(mo, "a");
    mo_type_key(mo, "s");
    mo_render(mo);
    assert(mo_empty(mo) == 0);
    assert(mo_span_count(mo) >= 1);
    assert(spansText(mo).rfind("cas", 0) == 0);   // il documento inizia con "cas"
    assert(suggHas(mo, "casa"));                   // candidati della parola aperta

    // Availability coerente con "parola aperta, documento non vuoto".
    mo_avail av;
    mo_availability(mo, &av);
    assert(av.deleteLetter == 1);
    assert(av.write == 1);
    assert(av.discard == 1);
    assert(av.confirm == 1);

    // Uno span deve essere evidenziato come "aperto".
    bool hasOpen = false;
    for (size_t i = 0; i < mo_span_count(mo); ++i)
        if (mo_span_hl(mo, i) == MO_HL_OPEN) hasOpen = true;
    assert(hasOpen);

    // Scarta -> documento vuoto.
    mo_clear(mo);
    mo_render(mo);
    assert(mo_empty(mo) == 1);
    assert(mo_span_count(mo) == 0);

    // --- Assistita (T9) con keymap a cifre: "7","6" -> sole, sono ------------
    mo_set_mode(mo, 1);
    assert(mo_assisted(mo) == 1);
    mo_type_key(mo, "7");
    mo_type_key(mo, "6");
    mo_render(mo);
    assert(mo_empty(mo) == 0);
    assert(suggHas(mo, "sole"));
    assert(suggHas(mo, "sono"));

    // Write: ritorna il testo e svuota il buffer.
    const char* written = mo_write(mo);
    assert(written && written[0] != '\0');
    mo_render(mo);
    assert(mo_empty(mo) == 1);

    mo_destroy(mo);

    // --- keymap del FE (lettere w e a s d z x c) via mo_keymap_* ------------
    mo_engine* mo2 = mo_create();
    mo_keymap_clear(mo2);
    mo_keymap_add(mo2, "w", "abc"); mo_keymap_add(mo2, "e", "def");
    mo_keymap_add(mo2, "a", "ghi"); mo_keymap_add(mo2, "s", "jkl");
    mo_keymap_add(mo2, "d", "mno"); mo_keymap_add(mo2, "z", "pqrs");
    mo_keymap_add(mo2, "x", "tuv"); mo_keymap_add(mo2, "c", "wxyz");
    mo_set_config(mo2, 8);
    assert(mo_load_wordlist(mo2, wl) == 1);
    mo_set_mode(mo2, 1);
    mo_type_key(mo2, "w");            // gruppo "abc": apre una parola
    mo_render(mo2);
    assert(mo_empty(mo2) == 0);       // il keymap del FE alimenta il motore
    mo_destroy(mo2);

    std::remove(wl);
    std::puts("motore_c_tests: OK (C ABI motore_c)");
    return 0;
}
