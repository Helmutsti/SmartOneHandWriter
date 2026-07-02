// SmartOneHandWriter - MOTORE (macchina a stati): tipi del documento e del render.
//
// Il MOTORE è lo strato condiviso sopra il CORE (sohw::Core). Possiede il testo
// come lista di parole (nessuno spazio memorizzato: gli spazi sono derivati al
// render dalle regole §1.1 del piano 04). Vedi docs/plans/04-engine-state-improvements.md.
#pragma once

#include <string>
#include <vector>

namespace motore {

enum class WordState  { Open, Resolved };
enum class WordOrigin { Typed, Loaded };
enum class WordClass  { Text, Punct };

// Evidenziazione di una parola nel render (per l'overlay del FE).
enum class Highlight { None, Selected, Open };

// Una parola (o token di punteggiatura) del documento.
struct Word {
    std::string              text;    // forma mostrata (UTF-8)
    std::vector<std::string> cells;   // simboli/tasti digitati (solo Typed; vuoto per Loaded)
    std::vector<std::string> cands;   // candidati per il Roll (parola Open)
    int                      idx = 0; // candidato selezionato (indice in cands)
    WordState  state  = WordState::Resolved;
    WordOrigin origin = WordOrigin::Typed;
    WordClass  cls    = WordClass::Text;
};

// Uno span del render: testo della parola, evidenziazione e se è preceduto da uno
// spazio (le regole di spaziatura vivono nel MOTORE, non nel FE).
struct RenderSpan {
    std::string text;
    Highlight   hl = Highlight::None;
    bool        spaceBefore = false;
};

// Modello di render completo per l'overlay: testo intero + span con evidenziazioni
// + indici di selezione/aperta.
struct RenderModel {
    std::string             fullText;
    std::vector<RenderSpan> spans;
    int selection = -1;   // indice parola selezionata (-1 se documento vuoto)
    int open      = -1;   // indice parola aperta (-1 se nessuna)
};

} // namespace motore
