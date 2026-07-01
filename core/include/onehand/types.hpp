// OneHand core - tipi pubblici (indipendenti dal sistema operativo).
//
// Nuova concezione (T9): tutto ruota attorno alla singola Parola. Ogni pressione
// di tasto e' un tasto del keymap, che rappresenta un GRUPPO di lettere (es. il
// tasto '2' -> {a,b,c}); il dizionario disambigua l'intera parola e il Roll cicla
// le collisioni. Gli spazi NON esistono come dato: sono derivati al render.
//
// Il motore non esegue I/O: riceve azioni e restituisce una descrizione di cosa
// fare (Effects). Il frontend traduce gli Effects nelle API native del proprio OS.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace onehand {

// ------------------------------------------------------------------ input
// Tasto semantico, gia' filtrato e tradotto dal frontend (niente VK code qui).
// Mantenuto per il percorso legacy onKey(); i frontend usano onAction().
enum class KeyKind {
    Letter,
    Space,
    Backspace,
    Tab,
    Enter,
};

struct KeyEvent {
    KeyKind kind = KeyKind::Letter;
    wchar_t letter = 0;   // valido solo se kind == Letter
};

// Azione di composizione gia' risolta dal frontend. Il motore la esegue e basta
// (Engine::onAction / onActionIndex).
//
// IMPORTANTE (stabilita' ABI): i valori numerici NON vanno mai rinumerati; il
// frontend macOS/Windows li cabla come interi. Nuove azioni si aggiungono in coda.
enum class Action {
    Letter        = 0,  // usa 'letter' come TASTO del keymap (non lettera finale)
    Wildcard      = 1,  // DEPRECATO (modello wildcard rimosso): no-op, slot riservato
    Accept        = 2,  // Conferma: chiude la parola aperta come risolta
    Rolling       = 3,  // Roll: cicla le alternative della parola aperta
    DeleteChar    = 4,  // cancella l'ultima cella (a destra) della parola aperta
    DeleteWord    = 5,  // cancella la parola aperta, apre la piu' vicina a sinistra
    Finalize      = 6,  // finalizza (Invio): conferma, passThrough, reset documento
    ConfirmNewWord= 7,  // Conferma + apre una nuova parola vuota a destra
    OpenPrevWord  = 8,  // riapre la parola a sinistra di quella aperta
    OpenNextWord  = 9,  // riapre la parola a destra di quella aperta
    OpenWordAt    = 10, // accesso casuale: riapre la parola all'indice dato (solo editor interno)
};

// ------------------------------------------------------------------ effetti
// Modifica al campo di testo con focus: prima 'backspaces' cancellazioni, poi
// l'inserimento di 'insert'. Il motore possiede il testo canonico e ne emette
// il diff minimo (prefisso comune): backspace fino al punto di modifica, poi
// reinserimento della coda.
struct EditEffect {
    int          backspaces = 0;
    std::wstring insert;
};

// Stato del popup delle alternative. Il frontend lo posiziona e lo disegna.
struct PopupEffect {
    bool         visible = false;
    std::wstring text;
};

// Richiesta sul timer del doppio-tap (usata solo dal percorso legacy onKey()).
struct TimerEffect {
    enum class Action { None, Start, Cancel };
    Action action = Action::None;
    int    ms = 0;
};

struct Effects {
    std::vector<EditEffect> edits;          // applicare in ordine
    PopupEffect             popup;
    TimerEffect             timer;
    bool                    passThrough = false;  // il tasto deve raggiungere l'app (es. Invio)
};

// ------------------------------------------------------------------ documento
// Una Parola e' una sequenza di Celle. Ogni Cella ricorda il TASTO premuto (il
// suo gruppo di lettere): cosi', riaprendo una parola risolta, il Roll ha ancora
// di che variare. 'glyph' e' la lettera attualmente mostrata per quella cella
// (deriva dal candidato selezionato).
enum class WordState { Open, Resolved };
enum class WordClass { Text /*, Punct (futuro) */ };

struct Cell {
    wchar_t key   = 0;   // tasto del keymap premuto per questa cella
    wchar_t glyph = 0;   // lettera mostrata (dal candidato selezionato)
};

struct Word {
    std::vector<Cell>         cells;
    std::vector<std::wstring> cands;              // candidati (significativi mentre Open)
    int                       realCount = 0;      // quanti, in testa a 'cands', sono parole reali
                                                  // (il resto sono alterazioni case/accento)
    int                       idx   = 0;          // candidato selezionato
    WordState                 state = WordState::Open;
    WordClass                 cls   = WordClass::Text;
    bool                      capFirst = false;   // maiuscola sulla prima lettera al render
};

struct Document {
    std::vector<Word> words;   // ordinato; NESSUNO spazio memorizzato
};

// ------------------------------------------------------------------ config
// Keymap T9: mappa un TASTO (carattere, es. L'2') al suo gruppo di lettere
// (es. L"abc"). I tasti non mappati ripiegano su se stessi (gruppo di 1),
// cosi' una tastiera piena resta un caso particolare del keymap.
struct KeyMap {
    std::map<wchar_t, std::wstring> groups;

    // Gruppo di lettere per un tasto; se non mappato, {key}.
    std::vector<wchar_t> groupOf(wchar_t key) const;
};

// Preset "numpad classico": 2->abc 3->def ... 9->wxyz.
KeyMap defaultT9KeyMap();

// Parametri del motore. Il frontend li riempie (tipicamente da config.json) e
// li passa con Engine::setConfig(). Il core non legge mai file da solo.
struct Config {
    int          maxCandidates = 8;
    int          doublePressMs = 280;              // solo percorso legacy onKey()
    std::wstring wordlistName  = L"wordlist_it.txt";
    KeyMap       keymap        = defaultT9KeyMap();
};

// Helper riusabile ma opzionale: interpreta il testo di config.json (NON apre
// file). I frontend possono usarlo o, su altri OS, ignorarlo.
Config parseConfig(const std::string& jsonText);

} // namespace onehand
