// OneHand core - il motore di composizione.
//
// Macchina a stati pura: (stato, evento) -> (nuovo stato, effetti). Nessuna
// dipendenza dal sistema operativo. Il frontend la pilota chiamando onKey()
// per ogni tasto e onTimeout() allo scadere del timer del doppio-tap.
#pragma once

#include "onehand/types.hpp"

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

    // Eventi: ognuno restituisce gli effetti da applicare al campo/popup/timer.
    Effects onKey(const KeyEvent& key);   // tasto gia' filtrato dal frontend
    Effects onTimeout();                  // scadenza del timer doppio-tap
    Effects reset();                      // su Play/Stop: azzera la composizione

    bool hasWord() const { return hasWord_; }

private:
    // Raccoglitore di modifiche: accumula la sequenza minima di EditEffect,
    // creando una nuova coppia (backspaces, insert) quando un backspace segue
    // un inserimento (cosi' anche gli interleaving sono rappresentati).
    struct Out {
        std::vector<EditEffect> edits;
        void backspace(std::size_t n);
        void insert(const std::wstring& s);
    };

    // stato di composizione (ex globali g_*)
    std::wstring              pattern_;     // scheletro corrente (lettere + '?')
    std::vector<std::wstring> cands_;       // candidati per la parola corrente
    int                       idx_ = 0;     // candidato selezionato
    std::wstring              preview_;     // testo gia' iniettato per la parola corrente
    std::vector<std::wstring> committed_;   // parole confermate (ognuna col suo spazio)
    std::wstring              popupText_;   // ultimo testo del popup

    // doppia pressione
    bool    pending_ = false;
    KeyKind pendingKey_ = KeyKind::Space;

    // spaziatura / maiuscole / punteggiatura
    bool capNext_       = true;
    bool trailingSpace_ = false;
    bool singleLetter_  = false;
    bool punctMode_     = false;
    int  punctIdx_      = 0;
    bool removedSpace_  = false;
    bool hasWord_       = false;

    Config                      cfg_;
    std::unique_ptr<Dictionary> dict_;

    // logica interna (ex funzioni globali del motore)
    void         recompute();
    std::wstring currentWord() const;
    std::wstring displayWord() const;
    void         render(Out& out);
    PopupEffect  buildPopup() const;
    void         resetComposition();

    void enterPunctMode(Out& out);
    void cyclePunct(Out& out);
    void cancelPunct(Out& out);
    void commitPunct(Out& out);

    void actLiteral(Out& out, wchar_t ch);
    void actWildcard(Out& out);
    void actTab(Out& out);
    void actAccept(Out& out);
    void actDeleteChar(Out& out);
    void actDeleteWord(Out& out);
    void actFinalizeOnEnter(Out& out);

    void doSingle(Out& out, KeyKind k);
    void doDouble(Out& out, KeyKind k);
};

} // namespace onehand
