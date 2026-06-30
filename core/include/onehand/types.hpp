// OneHand core - tipi pubblici (indipendenti dal sistema operativo).
//
// Il motore non esegue I/O: riceve eventi (KeyEvent) e restituisce una
// descrizione di cosa fare (Effects). Il frontend traduce gli Effects nelle
// API native del proprio OS.
#pragma once

#include <string>
#include <vector>

namespace onehand {

// ------------------------------------------------------------------ input
// Tasto semantico, gia' filtrato e tradotto dal frontend (niente VK code qui).
enum class KeyKind {
    Letter,     // usa 'letter' (lettera minuscola a-z)
    Space,
    Backspace,
    Tab,
    Enter,
};

struct KeyEvent {
    KeyKind kind = KeyKind::Letter;
    wchar_t letter = 0;   // valido solo se kind == Letter
};

// ------------------------------------------------------------------ effetti
// Modifica al campo di testo con focus: prima 'backspaces' cancellazioni, poi
// l'inserimento di 'insert'. Una sequenza di EditEffect rappresenta anche le
// modifiche piu' complesse (es. cancella token + riscrivi parola riaperta).
struct EditEffect {
    int          backspaces = 0;
    std::wstring insert;
};

// Stato del popup delle alternative / tavolozza punteggiatura. Il frontend si
// occupa di posizionarlo e disegnarlo; qui c'e' solo testo e visibilita'.
struct PopupEffect {
    bool         visible = false;
    std::wstring text;
};

// Richiesta sul timer del doppio-tap. Il motore possiede la *policy* (i ms),
// il frontend possiede il *meccanismo* (es. SetTimer) e richiama onTimeout().
struct TimerEffect {
    enum class Action { None, Start, Cancel };
    Action action = Action::None;
    int    ms = 0;            // valido se action == Start (avvio/riavvio)
};

struct Effects {
    std::vector<EditEffect> edits;          // applicare in ordine
    PopupEffect             popup;
    TimerEffect             timer;
    bool                    passThrough = false;  // il tasto deve raggiungere l'app (es. Invio)
};

// ------------------------------------------------------------------ config
// Parametri del motore. Il frontend li riempie (tipicamente da config.json) e
// li passa con Engine::setConfig(). Il core non legge mai file da solo.
struct Config {
    std::wstring availableKeys = L"qwertasdfgzxcvb";
    bool         wildcardAny   = false;
    int          maxCandidates = 8;
    int          doublePressMs = 280;
    std::wstring punctuation   = L",.?!:;'()-";
    std::wstring wordlistName  = L"wordlist_it.txt";
};

// Helper riusabile ma opzionale: interpreta il testo di config.json (NON apre
// file). I frontend possono usarlo o, su altri OS, ignorarlo (es. un .plist).
Config parseConfig(const std::string& jsonText);

} // namespace onehand
