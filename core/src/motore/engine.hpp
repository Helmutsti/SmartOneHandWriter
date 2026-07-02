// SmartOneHandWriter - MOTORE (macchina a stati sopra il CORE).
//
// MILESTONE 1: modello dati + cursori (sel/open) + render model + tokenizzazione
// (Strategia A degli apostrofi) e regole di spaziatura. Le azioni complete
// (Roll/Confirm/Advance/ConfirmContinue/Punct/TypeKey/Write, integrazione con
// sohw::Core) arrivano nelle milestone successive. Vedi
// docs/plans/04-engine-state-improvements.md.
#pragma once

#include "motore/types.hpp"

#include <string>
#include <vector>

namespace motore {

class Engine {
public:
    // Popola il documento da testo GIÀ COMPLETATO: tokenizza in parole (Text) e
    // punteggiatura (Punct) come token distinti; l'apostrofo termina il token di
    // sinistra e ci resta attaccato (Strategia A). Le parole sono Resolved/Loaded.
    void loadResolved(const std::string& utf8Text);
    void clear();

    int wordCount() const { return static_cast<int>(words_.size()); }
    int selection() const { return sel_; }   // -1 se vuoto
    int openIndex() const { return open_; }   // -1 se nessuna aperta

    // Primitive minime di cursore/stato (M2 completerà navigazione ed editing con
    // auto-conferma, integrazione CORE, ecc.).
    void select(int index);   // sposta la selezione (clamp ai limiti)
    void openSelected();      // apre la parola selezionata (state=Open)
    void closeOpen();         // chiude l'eventuale parola aperta (state=Resolved)

    // Modello di render per l'overlay: testo intero + span evidenziati.
    RenderModel render() const;

    // Accesso in sola lettura (test/diagnostica).
    const std::vector<Word>& words() const { return words_; }

private:
    std::vector<Word> words_;
    int sel_  = -1;
    int open_ = -1;
};

} // namespace motore
