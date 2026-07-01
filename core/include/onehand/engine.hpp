// OneHand core - il motore di composizione (modello T9).
//
// Macchina a stati pura: (stato, azione) -> (nuovo stato, effetti). Nessuna
// dipendenza dal sistema operativo. Il frontend la pilota con onAction() /
// onActionIndex(); il motore possiede il testo canonico e ne emette il diff.
#pragma once

#include "onehand/types.hpp"
#include "onehand/predictor.hpp"

#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace onehand {

class Dictionary;  // dettaglio interno (core/src/dictionary.hpp)

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Configurazione e dizionario: passati dal frontend, mai letti da file qui.
    void setConfig(const Config& cfg);
    void loadWordlist(std::istream& in);
    // Predittore per il ranking (default: frequenza). Iniettabile dal frontend.
    void setPredictor(std::unique_ptr<Predictor> p);

    // Percorso legacy (adattatore sottile): il modello T9 non usa il doppio-tap.
    Effects onKey(const KeyEvent& key);
    Effects onTimeout();
    Effects reset();

    // Percorso principale: azione gia' risolta dal frontend.
    Effects onAction(Action a, wchar_t letter = 0);
    // Azione con indice (per OpenWordAt: riapertura ad accesso casuale).
    Effects onActionIndex(Action a, int index);
    // Deprecato (modello wildcard rimosso): no-op, resta per compat ABI.
    Effects previewWildcard();

    // Introspezione (per l'editor interno: click-to-open, posizione del caret).
    bool         hasWord() const;
    int          wordCount() const { return static_cast<int>(doc_.words.size()); }
    int          openIndex() const { return openIndex_; }
    int          caret() const;
    std::wstring renderText() const;

private:
    // Raccoglitore di modifiche (backspaces + insert).
    struct Out {
        std::vector<EditEffect> edits;
        void backspace(std::size_t n);
        void insert(const std::wstring& s);
    };

    // Stato del documento.
    Document     doc_;
    int          openIndex_    = -1;    // parola aperta, o -1 (caret in coda)
    std::wstring lastRender_;           // testo canonico emesso finora (base del diff)
    bool         sentenceStart_ = true; // maiuscola iniziale sulla prima parola

    Config                      cfg_;
    std::unique_ptr<Dictionary> dict_;
    std::unique_ptr<Predictor>  predictor_;

    // Helper interni.
    std::vector<wchar_t> groupFor(wchar_t key) const;
    void          recompute(Word& w);
    void          syncGlyphs(Word& w) const;
    std::wstring  displayOf(const Word& w) const;
    std::wstring  renderWithCaret(int* caretOut) const;
    void          emit(Out& out);
    PredictContext buildContext() const;

    void ensureOpenAtTail();
    int  resolveCurrent();          // risolve la parola aperta; ritorna l'indice eventualmente rimosso, o -1
    void gotoWord(int target);      // riapre la parola 'target' (accesso casuale)

    void actLetter(wchar_t key);
    void actRoll();
    void actConfirm();
    void actConfirmNewWord();
    void actDeleteChar();
    void actDeleteWord();

    PopupEffect buildPopup() const;
};

} // namespace onehand
