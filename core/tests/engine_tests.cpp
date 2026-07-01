// Test del motore senza alcuna dipendenza dal sistema operativo.
// Simula un campo di testo applicando gli EditEffect prodotti dal motore,
// esattamente come farebbe un frontend, e verifica il testo risultante.
#include "onehand/engine.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

using onehand::Engine;
using onehand::Effects;
using onehand::KeyEvent;
using onehand::KeyKind;

namespace {

int g_failures = 0;

// Campo di testo finto: applica gli effetti (backspaces + insert) come farebbe
// il frontend iniettando nel campo con focus.
struct Field {
    Engine       engine;
    std::wstring text;

    void apply(const Effects& fx) {
        for (const auto& e : fx.edits) {
            for (int i = 0; i < e.backspaces && !text.empty(); ++i) text.pop_back();
            text += e.insert;
        }
    }
    void key(KeyKind k, wchar_t ch = 0) { apply(engine.onKey({k, ch})); }
    void action(onehand::Action a, wchar_t ch = 0) { apply(engine.onAction(a, ch)); }
    void letter(wchar_t ch)             { key(KeyKind::Letter, ch); }
    void space()                        { key(KeyKind::Space); }
    void backspace()                    { key(KeyKind::Backspace); }
    void tab()                          { key(KeyKind::Tab); }
    void enter()                        { key(KeyKind::Enter); }
    void timeout()                      { apply(engine.onTimeout()); }
    void reset()                        { apply(engine.reset()); }
};

// dizionario minimale in memoria (il core non apre file)
void loadDict(Field& f) {
    std::istringstream dict(
        "donna\t100\n"
        "cosa\t90\n"
        "casa\t80\n"
        "cane\t70\n"
        "sole\t60\n"
        "a\t5000\n");
    f.engine.setConfig(onehand::Config{});  // default: mano sinistra, jolly = lettere non disponibili
    f.engine.loadWordlist(dict);
}

void check(const char* name, const std::wstring& got, const std::wstring& want) {
    if (got == want) {
        std::printf("  OK  %s\n", name);
    } else {
        std::string g(got.begin(), got.end());
        std::string w(want.begin(), want.end());
        std::printf("FAIL  %s: atteso \"%s\", ottenuto \"%s\"\n", name, w.c_str(), g.c_str());
        ++g_failures;
    }
}

// "donna": d, spazio(jolly per 'o' tramite flush sul tasto successivo),
// n, n, a, poi spazio x2 per accettare. Maiuscola iniziale a inizio frase.
void test_donna() {
    Field f; loadDict(f);
    f.letter(L'd');
    f.space();          // diventa jolly quando arriva la lettera dopo
    f.letter(L'n');
    f.letter(L'n');
    f.letter(L'a');
    f.space(); f.space();   // doppio spazio = accetta + spazio
    check("donna", f.text, L"Donna ");
}

// "cosa" con jolly confermato dal timeout (spazio singolo isolato).
void test_cosa_timeout() {
    Field f; loadDict(f);
    f.letter(L'c');
    f.space();          // spazio isolato...
    f.timeout();        // ...il timer scade -> jolly
    f.letter(L's');
    f.letter(L'a');
    f.space(); f.space();
    check("cosa-timeout", f.text, L"Cosa ");
}

// Maiuscola automatica di una sola lettera a inizio frase ('a' -> 'A').
void test_single_letter_caps() {
    Field f; loadDict(f);
    f.letter(L'a');
    f.space(); f.space();   // accetta
    check("single-letter-caps", f.text, L"A ");
}

// Backspace doppio cancella l'intera parola in corso.
void test_delete_word() {
    Field f; loadDict(f);
    f.letter(L'c'); f.letter(L'a'); f.letter(L'n'); f.letter(L'e');  // "Cane" in anteprima
    f.backspace(); f.backspace();   // doppio backspace = cancella parola
    check("delete-word", f.text, L"");
}

// reset() ripulisce lo stato (Stop). L'anteprima resta nel campo finche' non
// la si cancella manualmente, ma il motore riparte pulito.
void test_reset() {
    Field f; loadDict(f);
    f.letter(L'c'); f.letter(L'a');
    f.reset();
    f.text.clear();             // come se l'utente avesse pulito il campo
    f.letter(L's'); f.letter(L'o'); f.letter(L'l'); f.letter(L'e');
    f.space(); f.space();
    check("reset", f.text, L"Sole ");
}

// Cancellazione della punteggiatura a due passi: prima lo spazio, poi il segno
// (ripristinando lo spazio della parola precedente).
void test_delete_punct_two_steps() {
    Field f; loadDict(f);
    f.letter(L'a');
    f.space(); f.space();       // "A " (accetta la singola lettera)
    f.tab();                    // apre la punteggiatura: ','
    f.tab(); f.tab();           // scorre: '.' -> '?'
    f.space(); f.space();       // accetta il segno -> "A? "
    check("punct-commit", f.text, L"A? ");
    f.backspace(); f.timeout(); // backspace singolo (via timeout) -> 1° passo: via lo spazio
    check("punct-del-space", f.text, L"A?");
    f.backspace(); f.timeout(); // backspace singolo -> 2° passo: via il segno, torna lo spazio
    check("punct-del-mark", f.text, L"A ");
}

// Percorso "azioni" (quello usato dal frontend Windows dopo aver risolto
// singola/doppia). Verifica che Accept e DeleteWord facciano la loro parte.
void test_action_accept_and_delete_word() {
    using onehand::Action;
    Field f; loadDict(f);
    f.action(Action::Letter, L'c');
    f.action(Action::Letter, L'a');
    f.action(Action::Letter, L'n');
    f.action(Action::Letter, L'e');       // "Cane" in anteprima
    f.action(Action::DeleteWord);         // cancella-parola sulla parola in corso
    check("action-delword-live", f.text, L"");

    f.action(Action::Letter, L'c');
    f.action(Action::Letter, L'a');
    f.action(Action::Letter, L'n');
    f.action(Action::Letter, L'e');
    f.action(Action::Accept);             // conferma -> "cane "
    f.action(Action::DeleteWord);         // cancella-parola sulla parola confermata
    check("action-delword-committed", f.text, L"");
}

// Cancella-parola con un "?" (carattere jolly) presente nel testo: verifica che
// non blocchi nulla (caso segnalato dall'utente).
void test_delword_with_question_mark() {
    using onehand::Action;
    Field f; loadDict(f);
    f.action(Action::Letter, L'c');
    f.action(Action::Letter, L'a');
    f.action(Action::Letter, L'n');
    f.action(Action::Letter, L'e');
    f.action(Action::Accept);            // "Cane "
    f.action(Action::Rolling);           // apre punteggiatura -> ","
    f.action(Action::Rolling);           // "."
    f.action(Action::Rolling);           // "?"
    f.action(Action::Accept);            // conferma segno -> "Cane? "
    f.action(Action::DeleteChar);        // toglie lo spazio -> "Cane?"
    check("delword-q-delchar", f.text, L"Cane?");
    f.action(Action::DeleteWord);        // cancella-parola -> toglie "?"
    check("delword-q-mark", f.text, L"Cane");
    f.action(Action::DeleteWord);        // cancella-parola -> toglie "Cane"
    check("delword-q-word", f.text, L"");
}

} // namespace

int main() {
    std::printf("engine_tests:\n");
    test_donna();
    test_cosa_timeout();
    test_single_letter_caps();
    test_delete_word();
    test_reset();
    test_delete_punct_two_steps();
    test_action_accept_and_delete_word();
    test_delword_with_question_mark();
    if (g_failures) { std::printf("%d test FALLITI\n", g_failures); return 1; }
    std::printf("tutti i test OK\n");
    return 0;
}
