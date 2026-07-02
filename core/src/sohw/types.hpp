// SmartOneHandWriter - CORE "nuova concezione": tipi pubblici del CORE stateless.
//
// Il CORE riceve un contesto (dall'esterno, non possiede il documento) e una
// parola codificata, e produce: (a) i match decodificati/ordinati e (b) un
// ventaglio di parole successive per ciascun match. Tutto in UTF-8.
#pragma once

#include <string>
#include <vector>

namespace sohw {

// Contesto della parola da decodificare: testo a sinistra e a destra dello slot
// (slot implicito tra i due). UTF-8. left/right possono essere vuoti (inizio/fine).
struct Context {
    std::string left;
    std::string right;
};

// Modalita' di interpretazione dell'input codificato.
//  - T9:      i simboli sono tasti/gruppi di lettere (matching attivo).
//  - Literal: l'input e' gia' lettere reali (digitazione classica, completamento).
enum class InputMode {
    T9 = 0,
    Literal = 1,
};

// Una proposta (parola reale) con il suo punteggio contestuale.
struct Suggestion {
    std::string word;
    float       score = 0.0f;
};

// Risultato di una elaborazione del CORE.
struct CoreResult {
    // (3) candidati per la parola corrente, ordinati per plausibilita' contestuale.
    std::vector<Suggestion> matches;
    // (4) per ogni match in 'matches' (stesso indice), il ventaglio di parole
    // successive proposte dal predittore con il contesto completo di quello step.
    std::vector<std::vector<Suggestion>> nextByMatch;
};

} // namespace sohw
