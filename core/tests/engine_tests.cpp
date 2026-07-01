// Test del motore (modello T9) senza dipendenze dal sistema operativo.
// Simula un campo di testo applicando gli EditEffect prodotti dal motore,
// esattamente come farebbe un frontend, e verifica il testo risultante.
//
// Nel modello T9 ogni pressione e' un TASTO del keymap (una cifra 2..9) che
// rappresenta un gruppo di lettere; il dizionario disambigua la parola.
#include "onehand/engine.hpp"
#include "onehand/predictor.hpp"

#include <cstdio>
#include <sstream>
#include <string>

using onehand::Action;
using onehand::Effects;
using onehand::Engine;

namespace {

int g_failures = 0;

// Campo di testo finto: applica gli effetti (backspaces + insert) come farebbe
// il frontend iniettando nel campo con focus.
struct Field {
    Engine       engine;
    std::wstring text;
    Effects      lastFx;

    void apply(const Effects& fx) {
        lastFx = fx;
        for (const auto& e : fx.edits) {
            for (int i = 0; i < e.backspaces && !text.empty(); ++i) text.pop_back();
            text += e.insert;
        }
    }
    void action(Action a, wchar_t key = 0) { apply(engine.onAction(a, key)); }
    // Digita una sequenza di tasti (es. L"2263"): ogni carattere e' un tasto.
    void type(const std::wstring& code) { for (wchar_t k : code) action(Action::Letter, k); }
};

// Dizionario T9 minimale. Codici (2=abc 3=def 4=ghi 5=jkl 6=mno 7=pqrs 8=tuv 9=wxyz):
//   cane=2263  band=2263 (COLLISIONE)  casa=2272  sole=7653  a=2
void loadDict(Field& f) {
    std::istringstream dict(
        "cane\t100\n"
        "band\t90\n"
        "casa\t80\n"
        "sole\t60\n"
        "a\t5000\n");
    f.engine.setConfig(onehand::Config{});   // keymap default: numpad T9
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

void checkInt(const char* name, int got, int want) {
    if (got == want) { std::printf("  OK  %s\n", name); }
    else { std::printf("FAIL  %s: atteso %d, ottenuto %d\n", name, want, got); ++g_failures; }
}

// Parola + Conferma+spazio + seconda parola + Conferma. Prima parola maiuscola
// (inizio frase), seconda minuscola. Spazio derivato tra le due.
void test_word_space_word() {
    Field f; loadDict(f);
    f.type(L"2263");                 // cane -> "Cane"
    check("t9-cane", f.text, L"Cane");
    f.action(Action::ConfirmNewWord); // conferma + nuova parola: spazio derivato
    check("t9-cane-space", f.text, L"Cane ");
    f.type(L"7653");                 // sole
    f.action(Action::Accept);        // conferma
    check("t9-two-words", f.text, L"Cane sole");
}

// Roll cicla le parole in collisione (stesso codice 2263: cane/band).
void test_roll_collision() {
    Field f; loadDict(f);
    f.type(L"2263");
    check("roll-first", f.text, L"Cane");
    f.action(Action::Rolling);
    check("roll-second", f.text, L"Band");   // capFirst a inizio frase
}

// Cancella-lettera su parola di una sola cella: svuota -> cancella la parola.
void test_delete_char_empties() {
    Field f; loadDict(f);
    f.type(L"2");                    // 'a' -> "A"
    check("del-a", f.text, L"A");
    f.action(Action::DeleteChar);    // toglie l'unica cella -> parola vuota -> cancella parola
    check("del-empty", f.text, L"");
}

// Cancella-parola a cascata: apre sempre la parola a sinistra.
void test_delete_word_cascade() {
    Field f; loadDict(f);
    f.type(L"2263"); f.action(Action::ConfirmNewWord);   // Cane _
    f.type(L"7653"); f.action(Action::ConfirmNewWord);   // Cane sole _
    f.type(L"2");                                         // Cane sole a
    check("cascade-3", f.text, L"Cane sole a");
    f.action(Action::DeleteWord);                        // via 'a', apre 'sole'
    check("cascade-2", f.text, L"Cane sole");
    f.action(Action::DeleteWord);                        // via 'sole', apre 'Cane'
    check("cascade-1", f.text, L"Cane");
    f.action(Action::DeleteWord);                        // via 'Cane'
    check("cascade-0", f.text, L"");
}

// Accesso casuale: riapre una parola precedente e la ri-rolla (le celle
// ricordano i tasti, quindi il dizionario torna a proporre alternative).
void test_random_access_reopen_roll() {
    Field f; loadDict(f);
    f.type(L"2263"); f.action(Action::ConfirmNewWord);   // Cane _
    f.type(L"7653"); f.action(Action::Accept);           // Cane sole
    check("ra-before", f.text, L"Cane sole");
    f.action(Action::OpenWordAt, 0);                     // riapre "Cane"
    checkInt("ra-open-index", f.engine.openIndex(), 0);
    f.action(Action::Rolling);                           // Roll: cane -> band
    check("ra-after", f.text, L"Band sole");
}

// Il predittore riordina i candidati (senza inventarne): band prima di cane.
struct BandFirstPredictor final : public onehand::Predictor {
    std::vector<std::wstring> rankCandidates(
        const onehand::PredictContext&, const std::vector<std::wstring>& c) override {
        std::vector<std::wstring> out;
        for (const auto& w : c) if (w == L"band") out.push_back(w);
        for (const auto& w : c) if (w != L"band") out.push_back(w);
        return out;
    }
};

void test_predictor_reranks() {
    Field f; loadDict(f);
    f.engine.setPredictor(std::unique_ptr<onehand::Predictor>(new BandFirstPredictor()));
    f.type(L"2263");
    check("rerank", f.text, L"Band");   // band promosso a idx 0
}

// Diff minimale: aggiungere una lettera non ridigita tutto il prefisso.
void test_minimal_diff() {
    Field f; loadDict(f);
    f.type(L"22");                       // porta il campo a "Aa" (nessun match len2)
    check("diff-prefix", f.text, L"Aa");
    f.action(Action::Letter, L'6');      // "226" -> segnaposto "Aam"
    // l'ultimo effetto deve solo aggiungere 'm' (nessun backspace, insert breve)
    int totalBs = 0; std::wstring ins;
    for (const auto& e : f.lastFx.edits) { totalBs += e.backspaces; ins += e.insert; }
    checkInt("diff-backspaces", totalBs, 0);
    check("diff-insert", ins, L"m");
    check("diff-text", f.text, L"Aam");
}

// reset() ripulisce lo stato del motore.
void test_reset() {
    Field f; loadDict(f);
    f.type(L"2263");
    f.apply(f.engine.reset());
    f.text.clear();
    f.type(L"7653"); f.action(Action::Accept);
    check("reset", f.text, L"Sole");     // ora e' di nuovo inizio frase -> maiuscola
}

} // namespace

int main() {
    std::printf("engine_tests (T9):\n");
    test_word_space_word();
    test_roll_collision();
    test_delete_char_empties();
    test_delete_word_cascade();
    test_random_access_reopen_roll();
    test_predictor_reranks();
    test_minimal_diff();
    test_reset();
    if (g_failures) { std::printf("%d test FALLITI\n", g_failures); return 1; }
    std::printf("tutti i test OK\n");
    return 0;
}
