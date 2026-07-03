// SmartOneHandWriter - MOTORE (macchina a stati): tipi del documento e del render.
//
// Il MOTORE è lo strato condiviso sopra il CORE (sohw::Core). Possiede il testo
// come lista di parole (nessuno spazio memorizzato: gli spazi sono derivati al
// render dalle regole di spaziatura, Strategia A apostrofi). Vedi docs/ARCHITETTURA.md.
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
    int         typedCount = 0;   // n. di lettere/celle effettivamente digitate (solo parola aperta;
                                  // il FE ci sottolinea il prefisso: il resto è completamento)
};

// Quali azioni-comando hanno senso nello stato corrente del documento. Il FE le
// usa per attivare/disattivare i bottoni del pannello ("attiva i bottoni durante
// la digitazione"): un'azione a false sarebbe un no-op se invocata. La logica di
// disponibilità vive qui nel MOTORE (condivisa fra i frontend), non nel FE.
struct Availability {
    bool navPrev      = false;   // ◀ Naviga: c'è una parola a sinistra
    bool navNext      = false;   // Naviga ▶: c'è una parola a destra
    bool open         = false;   // Apri/Edit: c'è una selezione non già aperta
    bool roll         = false;   // Roll: ≥2 candidati (parola aperta) o ≥2 next-word
    bool confirm      = false;   // Conferma: c'è una parola aperta o un next-word evidenziato
    bool advance      = true;    // Avanti / Conf. continua: "nuova parola" sempre possibile
    bool deleteLetter = false;   // Canc. lettera: c'è una parola aperta
    bool deleteWord   = false;   // Canc. parola: c'è una parola selezionata
    bool punct        = true;    // . , ? ! : sempre inseribile
    bool read         = true;    // Read (clipboard): sempre
    bool write        = false;   // Write: il documento non è vuoto
    bool discard      = false;   // Scarta: il documento non è vuoto
};

// Modello di render completo per l'overlay: testo intero + span con evidenziazioni
// + indici di selezione/aperta.
struct RenderModel {
    std::string             fullText;
    std::vector<RenderSpan> spans;
    int selection = -1;   // indice parola selezionata (-1 se documento vuoto)
    int open      = -1;   // indice parola aperta (-1 se nessuna)

    // Riga di suggerimenti sotto il testo. Se una parola aperta ha lettere, sono i
    // suoi candidati (scegliere = quel candidato); se non c'è parola aperta o è vuota,
    // sono le predizioni della parola successiva (scegliere = inserisce + avanza).
    std::vector<std::string> suggestions;
    int  suggestionSel     = -1;      // voce evidenziata nella riga (-1 = nessuna)
    bool suggestionsAreNext = false;  // true = next-word; false = candidati della parola aperta

    // Azioni valide adesso: il FE ci attiva/disattiva i bottoni corrispondenti.
    Availability actions;
};

} // namespace motore
