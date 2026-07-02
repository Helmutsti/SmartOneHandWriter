// SmartOneHandWriter - MOTORE (macchina a stati sopra il CORE).
//
// M1: modello dati + cursori + render + tokenizzazione (Strategia A).
// M2: navigazione / apertura / Roll + integrazione con sohw::Core per i candidati
//     contestuali della parola aperta; digitazione (typeKey) e regola "letterale
//     primo" in modalità classica.
// M3 (futuro): Confirm / Advance / ConfirmContinue / Punct / cancellazioni.
// Vedi docs/plans/04-engine-state-improvements.md.
#pragma once

#include "motore/types.hpp"
#include "sohw/core.hpp"

#include <istream>
#include <string>
#include <vector>

namespace motore {

class Engine {
public:
    Engine() = default;

    // --- risorse / modalità (delegano al CORE) -----------------------------
    void loadWordlist(std::istream& in);
    void loadBigramModel(const std::string& binPath);
    void setMode(bool assisted);          // true = T9 (assistita), false = classica
    bool assisted() const { return assisted_; }

    // --- documento ---------------------------------------------------------
    // Popola da testo GIÀ COMPLETATO (parole Resolved/Loaded; Strategia A apostrofi).
    void loadResolved(const std::string& utf8Text);
    void clear();

    int wordCount() const { return static_cast<int>(words_.size()); }
    int selection() const { return sel_; }
    int openIndex() const { return open_; }

    // --- azioni ------------------------------------------------------------
    void select(int index);   // sposta la selezione (clamp); primitiva usata dalla navigazione
    void navigatePrev();   // auto-conferma l'aperta (senza creare) e sposta la selezione
    void navigateNext();
    void openSelected();   // apre la selezionata; se Typed con celle, ricalcola i candidati
    void closeOpen();      // chiude l'eventuale parola aperta (Resolved)
    void roll();           // cicla i candidati della parola aperta
    // Digita un simbolo: se nessuna parola è aperta ne apre una nuova; ignora se la
    // parola aperta è Loaded (D11-ii). Ricalcola i candidati via CORE.
    void typeKey(const std::string& sym);

    // --- composizione (M3) -------------------------------------------------
    void confirm();          // chiude l'aperta e resta (rimuove la parola se vuota)
    void advance();          // (conferma l'aperta) e apre una nuova parola vuota a destra
    void confirmContinue();  // combo conferma+avanti (auto a fine frase; anche esplicita)
    void punct(const std::string& sym);  // conferma + inserisce token Punct; se terminale (. ! ?) -> conferma continua
    void deleteLetter();     // parola aperta: rimuove l'ultima lettera/cella (se vuota rimuove la parola)
    void deleteWord();       // rimuove l'intera parola selezionata

    // --- render ------------------------------------------------------------
    RenderModel render() const;

    const std::vector<Word>& words() const { return words_; }

private:
    void recomputeOpen();                 // interroga il CORE e aggiorna cands/idx/text della parola aperta
    void removeWordAt(int index);         // rimuove una parola aggiustando sel_/open_

    sohw::Core        core_;
    bool              assisted_ = true;
    int               maxCands_ = 8;

    std::vector<Word> words_;
    int sel_  = -1;
    int open_ = -1;
};

} // namespace motore
