// SmartOneHandWriter - Assistente di digitazione (frontend Windows).
//
// FE sottile (adattatore di solo I/O) sopra il MOTORE condiviso (motore::Engine),
// che a sua volta sta sopra il CORE (sohw::Core). Vedi docs/ARCHITETTURA.md.
//
//  - Pannello: un bottone per funzione + Play/Pause + toggle Assistita/Classica.
//  - Overlay: box colorato senza bordi, riga singola, che mostra il buffer con
//    parola selezionata/aperta evidenziate; sparisce a buffer vuoto. Sta FERMO e si
//    può TRASCINARE col mouse (non insegue più il cursore).
//  - Hook tastiera globale (WH_KEYBOARD_LL): in assistita w e a s d z x c = gruppi
//    T9 come su un cellulare (w=2 e=3 a=4 s=5 d=6 z=7 x=8 c=9). Le FUNZIONI (Roll,
//    Conferma, Avanti, Apri, Naviga, cancella, punteggiatura, Read/Write, Scarta,
//    Conferma continua) sono mappate ai tasti dal file di conf  data\tasti.conf
//    (letto all'avvio; default di fabbrica se assente). Il FE traduce il tasto nel
//    comando e lo invia al MOTORE. Vedi loadBindings()/defaultBindings().
//  - Tre modalità (bottone Modalità): Assistita (T9), Classica (lettere dirette),
//    Multi-tap (scorrimento lettere: ripremi lo stesso tasto per ciclare le lettere
//    del gruppo, es. w,w,w = 'c'; per parole fuori dizionario). Multi-tap è Literal
//    con il ciclo lettere gestito nel FE.
//  - Read/Write via clipboard (Read = legge; Write = incolla con Ctrl+V).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "motore/engine.hpp"
#include "onehand/types.hpp"   // onehand::Config / KeyMap

#include <cctype>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// ------------------------------------------------------------------ util UTF-8/UTF-16
static std::wstring u8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string wToU8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// ------------------------------------------------------------------ stato globale
static motore::Engine g_engine;
static bool           g_active = false;   // Play/Pause (intercettazione attiva)
static HHOOK          g_hook   = nullptr;
static HWND           g_panel  = nullptr;
static HWND           g_overlay = nullptr;
static HWND           g_status = nullptr; // etichetta stato nel pannello
static std::map<int, HWND> g_btn;         // bottoni del pannello per id (per abilitarli/disabilitarli)
static HFONT          g_font   = nullptr;
static HFONT          g_overlayFont = nullptr;

// Modalità d'ingresso del FE. Il MOTORE conosce solo T9 vs Literal: il Multi-tap è
// Literal (lettere reali) con il ciclo delle lettere fatto qui nel FE.
enum Mode { MODE_T9 = 0, MODE_CLASSIC, MODE_MULTITAP };
static Mode            g_mode = MODE_T9;
static onehand::KeyMap g_keymap;          // gruppi di lettere (serve al multi-tap)
// Stato del multi-tap (scorrimento lettere): ripremere lo stesso tasto entro la
// finestra cicla le lettere del gruppo (es. w,w,w = 'c'); dopo la finestra o con un
// altro tasto la lettera è "fissata" e si passa a una nuova cella.
static DWORD           g_mtKey  = 0;      // ultimo tasto-gruppo del ciclo (0 = nessuno)
static int             g_mtIdx  = 0;      // indice lettera corrente nel gruppo
static DWORD           g_mtTime = 0;      // tick dell'ultima pressione
static const DWORD     kMultiTapMs = 900; // finestra per ciclare la stessa lettera

struct PaintSpan { std::wstring text; int hl; bool spaceBefore; int typed; };
static std::vector<PaintSpan> g_spans;

// Riga suggerimenti (candidati della parola aperta o next-word).
static std::vector<std::wstring> g_sugg;
static int  g_sugSel  = -1;      // voce evidenziata
static bool g_sugNext = false;   // true = next-word
static HFONT g_sugFont = nullptr;
// Aree cliccabili delle voci della riga suggerimenti (x0..x1 -> indice).
struct SugHit { int x0, x1, idx; };
static std::vector<SugHit> g_sugHit;

// geometria overlay: riga testo in alto, riga suggerimenti sotto
static const int kSugBandTop = 38;   // sopra = testo (trascinabile), sotto = suggerimenti (cliccabili)

// colori
static const COLORREF kBg     = RGB(43, 43, 43);
static const COLORREF kFg     = RGB(240, 240, 240);
static const COLORREF kSelBg  = RGB(58, 110, 165);   // azzurro
static const COLORREF kOpenBg = RGB(217, 164, 65);   // ambra
static const COLORREF kTyped  = RGB(30, 30, 30);     // sottolineatura del prefisso digitato
static const COLORREF kTail   = RGB(90, 74, 40);     // completamento (coda) attenuato, su ambra
static const COLORREF kSugFg  = RGB(150, 150, 150);  // suggerimenti non evidenziati
static const COLORREF kSugSel = RGB(58, 110, 165);   // sfondo del suggerimento evidenziato

// ------------------------------------------------------------------ azioni
enum Act {
    A_NONE = 0, A_NAV_PREV, A_NAV_NEXT, A_OPEN, A_ROLL, A_CONFIRM, A_ADVANCE,
    A_CONTINUE, A_DEL_LETTER, A_DEL_WORD, A_DOT, A_COMMA, A_QUES, A_EXCL,
    A_READ, A_WRITE, A_DISCARD
};

// Mappa TASTO-fisico (VK) -> funzione, caricata dal file di conf (data/tasti.conf)
// con fallback ai default. È qui che il FE traduce un tasto nel comando da inviare
// al MOTORE. Popolata da loadBindings()/defaultBindings().
static std::map<DWORD, Act> g_keyToAct;

static void refreshOverlay();
static void updateButtonStates(const motore::Availability& a);

// legge/scrive la clipboard (CF_UNICODETEXT)
static std::wstring clipboardGet() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) { if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { out = p; GlobalUnlock(h); } }
    CloseClipboard();
    return out;
}
static void clipboardSet(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        memcpy(GlobalLock(h), text.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
// simula Ctrl+V nell'app attiva
static void sendPaste() {
    INPUT in[4] = {};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'V';
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'V';  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

static void doWrite() {
    std::string text = g_engine.write();
    if (text.empty()) { refreshOverlay(); return; }
    std::wstring saved = clipboardGet();
    clipboardSet(u8ToW(text));
    sendPaste();
    // ripristina la clipboard dell'utente dopo un attimo (best-effort, sincrono qui)
    Sleep(80);
    clipboardSet(saved);
    refreshOverlay();
}
static void doRead() {
    std::wstring w = clipboardGet();
    if (!w.empty()) g_engine.loadResolved(wToU8(w));
    refreshOverlay();
}

static void resetMultiTap() { g_mtKey = 0; }   // interrompe il ciclo del multi-tap

static void performAction(Act a) {
    resetMultiTap();   // qualunque comando chiude il ciclo del multi-tap in corso
    switch (a) {
        case A_NAV_PREV:  g_engine.navigatePrev(); break;
        case A_NAV_NEXT:  g_engine.navigateNext(); break;
        case A_OPEN:      g_engine.openSelected(); break;
        case A_ROLL:      g_engine.roll(); break;
        case A_CONFIRM:   g_engine.confirm(); break;
        case A_ADVANCE:   g_engine.advance(); break;
        case A_CONTINUE:  g_engine.confirmContinue(); break;
        case A_DEL_LETTER:g_engine.deleteLetter(); break;
        case A_DEL_WORD:  g_engine.deleteWord(); break;
        case A_DOT:       g_engine.punct("."); break;
        case A_COMMA:     g_engine.punct(","); break;
        case A_QUES:      g_engine.punct("?"); break;
        case A_EXCL:      g_engine.punct("!"); break;
        case A_READ:      doRead(); return;   // già refresha
        case A_WRITE:     doWrite(); return;  // già refresha
        case A_DISCARD:   g_engine.clear(); break;   // butta via il buffer (l'overlay sparisce)
        default: break;
    }
    refreshOverlay();
}

// ------------------------------------------------------------------ overlay
static void refreshOverlay() {
    motore::RenderModel r = g_engine.render();
    g_spans.clear();
    for (const auto& s : r.spans)
        g_spans.push_back({ u8ToW(s.text), (int)s.hl, s.spaceBefore, s.typedCount });

    g_sugg.clear();
    for (const auto& w : r.suggestions) g_sugg.push_back(u8ToW(w));
    g_sugSel  = r.suggestionSel;
    g_sugNext = r.suggestionsAreNext;

    // Attiva/disattiva i bottoni anche a documento vuoto (quasi tutti grigi finché
    // non si inizia a scrivere), perciò prima dell'eventuale nascondere l'overlay.
    updateButtonStates(r.actions);

    if (g_engine.empty()) { ShowWindow(g_overlay, SW_HIDE); return; }

    // resta dove l'utente l'ha lasciato (trascinabile): mostra senza spostare né ridimensionare
    SetWindowPos(g_overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    InvalidateRect(g_overlay, nullptr, TRUE);
}

static void paintOverlay(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(hdc, &rc, bg); DeleteObject(bg);

    HFONT old = (HFONT)SelectObject(hdc, g_overlayFont);
    SetBkMode(hdc, TRANSPARENT);

    // misura larghezza totale per lo scorrimento (tiene visibile la parte finale)
    int spaceW = 0; { SIZE sz; GetTextExtentPoint32W(hdc, L" ", 1, &sz); spaceW = sz.cx; }
    int caretH = 0; { SIZE sz; GetTextExtentPoint32W(hdc, L"M", 1, &sz); caretH = sz.cy; }
    const int kCaretW = 3, kCaretAdv = 7;   // cursore dello slot vuoto in modifica
    int total = 0;
    for (const auto& s : g_spans) {
        if (s.spaceBefore) total += spaceW;
        if (s.hl == (int)motore::Highlight::Open && s.text.empty()) { total += kCaretAdv; continue; }
        SIZE sz; GetTextExtentPoint32W(hdc, s.text.c_str(), (int)s.text.size(), &sz);
        total += sz.cx;
    }
    int scroll = (total > rc.right - 12) ? (total - (rc.right - 12)) : 0;

    int x = 6 - scroll;
    const int y = 8;
    for (const auto& s : g_spans) {
        if (s.spaceBefore) x += spaceW;
        // Slot vuoto in modifica: disegna solo un cursore ambra (nessun testo).
        if (s.hl == (int)motore::Highlight::Open && s.text.empty()) {
            RECT car = { x, y, x + kCaretW, y + caretH };
            HBRUSH cb = CreateSolidBrush(kOpenBg);
            FillRect(hdc, &car, cb); DeleteObject(cb);
            x += kCaretAdv;
            continue;
        }
        SIZE sz; GetTextExtentPoint32W(hdc, s.text.c_str(), (int)s.text.size(), &sz);
        if (s.hl == (int)motore::Highlight::Selected || s.hl == (int)motore::Highlight::Open) {
            RECT hr = { x - 2, y - 2, x + sz.cx + 2, y + sz.cy + 2 };
            HBRUSH hb = CreateSolidBrush(s.hl == (int)motore::Highlight::Open ? kOpenBg : kSelBg);
            FillRect(hdc, &hr, hb); DeleteObject(hb);
        }
        // Parola aperta: distingui il prefisso DIGITATO (bianco + sottolineato) dalla
        // coda di COMPLETAMENTO del dizionario (attenuata), così si vede quante lettere
        // si sono premute. Le altre parole: testo pieno.
        if (s.hl == (int)motore::Highlight::Open && s.typed > 0) {
            int n = s.typed; if (n > (int)s.text.size()) n = (int)s.text.size();
            SIZE pre; GetTextExtentPoint32W(hdc, s.text.c_str(), n, &pre);
            SetTextColor(hdc, kFg);
            TextOutW(hdc, x, y, s.text.c_str(), n);
            if (n < (int)s.text.size()) {
                SetTextColor(hdc, kTail);
                TextOutW(hdc, x + pre.cx, y, s.text.c_str() + n, (int)s.text.size() - n);
            }
            RECT ul = { x, y + sz.cy, x + pre.cx, y + sz.cy + 2 };   // sottolineatura del prefisso
            HBRUSH ub = CreateSolidBrush(kTyped);
            FillRect(hdc, &ul, ub); DeleteObject(ub);
        } else {
            SetTextColor(hdc, kFg);
            TextOutW(hdc, x, y, s.text.c_str(), (int)s.text.size());
        }
        x += sz.cx;
    }

    // --- riga suggerimenti (sotto, font piccolo) --------------------------------
    g_sugHit.clear();
    SelectObject(hdc, g_sugFont ? g_sugFont : g_overlayFont);
    int sx = 6; const int sy = kSugBandTop + 3;
    for (int i = 0; i < (int)g_sugg.size(); ++i) {
        const std::wstring& t = g_sugg[i];
        SIZE sz; GetTextExtentPoint32W(hdc, t.c_str(), (int)t.size(), &sz);
        if (sx + sz.cx > rc.right - 6) break;   // troncamento se non ci stanno
        if (i == g_sugSel) {
            RECT hr = { sx - 3, sy - 1, sx + sz.cx + 3, sy + sz.cy + 1 };
            HBRUSH hb = CreateSolidBrush(kSugSel);
            FillRect(hdc, &hr, hb); DeleteObject(hb);
        }
        SetTextColor(hdc, i == g_sugSel ? kFg : kSugFg);
        TextOutW(hdc, sx, sy, t.c_str(), (int)t.size());
        g_sugHit.push_back({ sx - 3, sx + sz.cx + 3, i });
        sx += sz.cx + 14;
    }

    SelectObject(hdc, old);
    EndPaint(hwnd, &ps);
}

static void overlayClick(int x, int y) {
    if (y < kSugBandTop) return;                 // banda del testo: gestita come drag
    for (const auto& h : g_sugHit) {
        if (x >= h.x0 && x <= h.x1) {
            resetMultiTap();
            g_engine.acceptSuggestion(h.idx);    // scegli dalla lista visibile
            refreshOverlay();
            return;
        }
    }
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) { paintOverlay(hwnd); return 0; }
    // Banda del testo (in alto) = trascinabile; banda dei suggerimenti (in basso) = cliccabile.
    if (msg == WM_NCHITTEST) {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };   // coord. schermo
        ScreenToClient(hwnd, &pt);
        return (pt.y >= kSugBandTop) ? HTCLIENT : HTCAPTION;
    }
    if (msg == WM_LBUTTONDOWN) { overlayClick((short)LOWORD(lp), (short)HIWORD(lp)); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ hook tastiera
static bool handleKeyDown(DWORD vk);
static bool isMapped(DWORD vk);

static LRESULT CALLBACK KeyProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_active) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (!(p->flags & LLKHF_INJECTED)) {   // ignora gli eventi che iniettiamo noi (Ctrl+V)
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (handleKeyDown(p->vkCode)) return 1;   // consumato
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                // consuma anche il keyup dei tasti che gestiamo, per non farli arrivare all'app
                if (isMapped(p->vkCode)) return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// mappa un tasto "gruppo" (assistita) o lettera (classica) al simbolo da digitare
static bool groupSymbol(DWORD vk, std::string& out) {
    // w e a s d z x c -> 8 gruppi (assistita); in classica tutte le lettere sono letterali
    if (vk >= 'A' && vk <= 'Z') { out = std::string(1, (char)('a' + (vk - 'A'))); return true; }
    return false;
}
static bool isGroupKey(DWORD vk) {
    switch (vk) { case 'W': case 'E': case 'A': case 'S': case 'D':
                  case 'Z': case 'X': case 'C': return true; default: return false; }
}

// ------------------------------------------------------------------ mappatura tasti (file di conf)
// Nome-funzione usato nel file di conf -> azione interna. A_NONE = nome sconosciuto.
static Act funcNameToAct(const std::string& name) {
    struct M { const char* n; Act a; };
    static const M tbl[] = {
        { "conferma_continua", A_CONTINUE }, { "cancella_parola", A_DEL_WORD },
        { "cancella_lettera",  A_DEL_LETTER }, { "scarta", A_DISCARD },
        { "punto", A_DOT }, { "virgola", A_COMMA }, { "domanda", A_QUES }, { "esclamativo", A_EXCL },
        { "write", A_WRITE }, { "read", A_READ },
        { "roll", A_ROLL }, { "conferma", A_CONFIRM }, { "avanti", A_ADVANCE },
        { "apri", A_OPEN }, { "naviga_prev", A_NAV_PREV }, { "naviga_next", A_NAV_NEXT },
    };
    for (const auto& m : tbl) if (name == m.n) return m.a;
    return A_NONE;
}

// Nome-tasto del file di conf -> Virtual-Key. 0 = nome sconosciuto. Case-insensitive.
static DWORD keyNameToVk(const std::string& raw) {
    std::string s;
    for (char c : raw) s.push_back((char)std::tolower((unsigned char)c));
    if (s.size() == 1) {
        char c = s[0];
        if (c >= 'a' && c <= 'z') return (DWORD)('A' + (c - 'a'));
        if (c >= '0' && c <= '9') return (DWORD)c;
        if (c == '`')             return VK_OEM_3;
    }
    if (s == "spazio" || s == "space")                       return VK_SPACE;
    if (s == "tab")                                          return VK_TAB;
    if (s == "backspace" || s == "back")                     return VK_BACK;
    if (s == "blocmaiusc" || s == "capslock" || s == "caps") return VK_CAPITAL;
    if (s == "esc" || s == "escape")                         return VK_ESCAPE;
    if (s == "backtick" || s == "grave" || s == "apice")     return VK_OEM_3;
    return 0;
}

// Binding di fabbrica (usati se il file di conf manca o è illeggibile): identici al
// vecchio cablaggio, così il deploy self-contained funziona anche senza il file.
static void defaultBindings() {
    g_keyToAct.clear();
    g_keyToAct[VK_SPACE]   = A_CONTINUE;
    g_keyToAct[VK_TAB]     = A_DEL_WORD;
    g_keyToAct[VK_BACK]    = A_DEL_LETTER;
    g_keyToAct[VK_CAPITAL] = A_DEL_LETTER;
    g_keyToAct[VK_ESCAPE]  = A_DISCARD;
    g_keyToAct['1'] = A_DOT;   g_keyToAct['2'] = A_COMMA;
    g_keyToAct['3'] = A_QUES;  g_keyToAct['4'] = A_EXCL;
    g_keyToAct['5'] = A_WRITE; g_keyToAct[VK_OEM_3] = A_READ;
    g_keyToAct['F'] = A_ROLL;    g_keyToAct['G'] = A_CONFIRM;
    g_keyToAct['R'] = A_ADVANCE; g_keyToAct['T'] = A_OPEN;
    g_keyToAct['V'] = A_NAV_PREV; g_keyToAct['B'] = A_NAV_NEXT;
}

// Carica i binding dal file di conf. Formato per riga: "funzione = tasto [tasto...]".
// '#' avvia un commento. Più tasti (separati da spazio/virgola) mappano la stessa
// funzione. La mappa è ricostruita interamente dal file (nessun merge coi default):
// così rimappare una funzione ne SPOSTA il tasto invece di aggiungerne uno.
// Ritorna false (→ usa i default) se il file manca o non contiene binding validi.
static bool loadBindings(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    auto trim = [](std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { s.clear(); return; }
        size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
    };
    std::map<DWORD, Act> parsed;
    std::string line;
    while (std::getline(f, line)) {
        size_t h = line.find('#');
        if (h != std::string::npos) line.erase(h);        // togli il commento
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string fname = line.substr(0, eq);
        std::string keys  = line.substr(eq + 1);
        trim(fname);
        for (char& c : fname) c = (char)std::tolower((unsigned char)c);
        Act a = funcNameToAct(fname);
        if (a == A_NONE) continue;
        std::string tok;
        auto flush = [&]() {
            if (tok.empty()) return;
            DWORD vk = keyNameToVk(tok);
            if (vk) parsed[vk] = a;
            tok.clear();
        };
        for (char c : keys) {
            if (c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n') flush();
            else tok.push_back(c);
        }
        flush();
    }
    if (parsed.empty()) return false;
    g_keyToAct.swap(parsed);
    return true;
}

static bool isMapped(DWORD vk) {
    // In Classica ogni lettera si digita; le funzioni su tasti non-lettera restano attive.
    if (g_mode == MODE_CLASSIC) {
        if (vk >= 'A' && vk <= 'Z') return true;
        return g_keyToAct.count(vk) > 0;
    }
    // T9 / Multi-tap: funzioni mappate + tasti-gruppo.
    if (g_keyToAct.count(vk)) return true;
    if (isGroupKey(vk)) return true;
    return false;
}

// Multi-tap: ripremere lo stesso tasto-gruppo entro la finestra cicla le lettere del
// gruppo sostituendo l'ultima; un tasto diverso o la scadenza della finestra fissano
// la lettera e aprono una nuova cella. Alimenta il MOTORE con lettere reali (Literal).
static void multiTapKey(DWORD vk) {
    const wchar_t keych = (wchar_t)('a' + (vk - 'A'));       // 'W' -> 'w' (vk è A..Z)
    auto it = g_keymap.groups.find(keych);
    const std::wstring grp = (it != g_keymap.groups.end()) ? it->second : std::wstring(1, keych);
    if (grp.empty()) return;
    const DWORD now = GetTickCount();
    if (vk == g_mtKey && (now - g_mtTime) < kMultiTapMs) {
        // stessa lettera in corso: cicla al glifo successivo (sostituisce l'ultima cella)
        g_mtIdx = (g_mtIdx + 1) % (int)grp.size();
        g_engine.deleteLetter();
        g_engine.typeKey(wToU8(std::wstring(1, grp[(size_t)g_mtIdx])));
    } else {
        // nuova lettera: primo glifo del gruppo
        g_mtIdx = 0;
        g_engine.typeKey(wToU8(std::wstring(1, grp[0])));
        g_mtKey = vk;
    }
    g_mtTime = now;
    refreshOverlay();
}

static bool handleKeyDown(DWORD vk) {
    const bool letterKey = (vk >= 'A' && vk <= 'Z');

    // 1) Funzione mappata al tasto (dal file di conf) -> invia il comando al MOTORE.
    //    In Classica i tasti-LETTERA servono a digitare, quindi lì una funzione
    //    assegnata a una lettera è inattiva (la lettera si scrive).
    auto it = g_keyToAct.find(vk);
    if (it != g_keyToAct.end() && !(letterKey && g_mode == MODE_CLASSIC)) {
        performAction(it->second);
        return true;
    }

    // 2) Digitazione di lettere / gruppi.
    if (g_mode == MODE_CLASSIC) {
        if (letterKey) {
            std::string sym; groupSymbol(vk, sym);
            g_engine.typeKey(sym); refreshOverlay(); return true;
        }
        return false;
    }
    // T9 e Multi-tap: i tasti-gruppo alimentano il dizionario / lo scorrimento lettere.
    if (isGroupKey(vk)) {
        if (g_mode == MODE_MULTITAP) {
            multiTapKey(vk);                 // scorrimento lettere del gruppo
        } else {                             // MODE_T9: il tasto-gruppo va al dizionario
            std::string sym; groupSymbol(vk, sym);
            g_engine.typeKey(sym); refreshOverlay();
        }
        return true;
    }
    return false;   // altri tasti non usati: lasciali passare all'app
}

// ------------------------------------------------------------------ pannello
enum {
    IDB_PLAY = 1000, IDB_MODE, IDB_PREV, IDB_NEXT, IDB_OPEN, IDB_ROLL, IDB_CONFIRM,
    IDB_ADVANCE, IDB_CONTINUE, IDB_DELL, IDB_DELW, IDB_DOT, IDB_COMMA, IDB_QUES,
    IDB_EXCL, IDB_READ, IDB_WRITE, IDB_DISCARD
};

static void setStatus() {
    std::wstring s = L"Stato: ";
    s += g_active ? L"ATTIVO" : L"in pausa";
    s += L"  |  Modalità: ";
    s += (g_mode == MODE_T9)      ? L"Assistita (T9)"
       : (g_mode == MODE_CLASSIC) ? L"Classica"
                                  : L"Multi-tap (scorrimento lettere)";
    SetWindowTextW(g_status, s.c_str());
}
static void togglePlay() {
    g_active = !g_active;
    if (g_active && !g_hook)
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyProc, GetModuleHandleW(nullptr), 0);
    else if (!g_active && g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
    setStatus();
    refreshOverlay();
}

static HWND mkButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (g_font) SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    g_btn[id] = b;
    return b;
}

// Attiva/disattiva i bottoni del pannello secondo le azioni valide adesso (calcolate
// dal MOTORE). "Attivare i bottoni durante la digitazione": man mano che il testo
// cambia, i comandi senza senso diventano grigi. Play/Modalità restano sempre attivi.
static void updateButtonStates(const motore::Availability& a) {
    auto en = [](int id, bool on) {
        auto it = g_btn.find(id);
        if (it != g_btn.end() && it->second) EnableWindow(it->second, on ? TRUE : FALSE);
    };
    en(IDB_PREV,     a.navPrev);
    en(IDB_NEXT,     a.navNext);
    en(IDB_OPEN,     a.open);
    en(IDB_ROLL,     a.roll);
    en(IDB_CONFIRM,  a.confirm);
    en(IDB_ADVANCE,  a.advance);
    en(IDB_CONTINUE, a.advance);   // Conf. continua = Avanti nel MOTORE
    en(IDB_DELL,     a.deleteLetter);
    en(IDB_DELW,     a.deleteWord);
    en(IDB_DOT,      a.punct);
    en(IDB_COMMA,    a.punct);
    en(IDB_QUES,     a.punct);
    en(IDB_EXCL,     a.punct);
    en(IDB_READ,     a.read);
    en(IDB_WRITE,    a.write);
    en(IDB_DISCARD,  a.discard);
}

// Nome breve del tasto da mostrare sul bottone (VK -> testo).
static std::wstring vkDisplay(DWORD vk) {
    if (vk == VK_SPACE)   return L"Spazio";
    if (vk == VK_TAB)     return L"Tab";
    if (vk == VK_BACK)    return L"Backspace";
    if (vk == VK_CAPITAL) return L"BlocMaiusc";
    if (vk == VK_ESCAPE)  return L"Esc";
    if (vk == VK_OEM_3)   return L"`";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, (wchar_t)vk);
    return L"";
}
// Primo tasto (per ordine di VK) mappato a un'azione; L"" se nessuno.
static std::wstring keyForAct(Act a) {
    for (const auto& kv : g_keyToAct)
        if (kv.second == a) return vkDisplay(kv.first);
    return L"";
}
// Etichetta del bottone: testo + "(tasto)" su una seconda riga (BS_MULTILINE).
// Se l'azione non ha un tasto mappato, resta solo il testo.
static std::wstring withKey(const std::wstring& base, Act a) {
    const std::wstring k = keyForAct(a);
    return k.empty() ? base : (base + L"\n(" + k + L")");
}

static void createPanel(HWND hwnd) {
    const int W = 96, H = 42, PAD = 6;
    int x = PAD, y = PAD;
    auto row = [&](int col) { return PAD + col * (W + PAD); };
    mkButton(hwnd, L"Play/Pause", IDB_PLAY, row(0), y, W, H);
    mkButton(hwnd, L"Modalità",   IDB_MODE, row(1), y, W, H);
    g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                             row(2), y + 4, 3 * (W + PAD), 22, hwnd, nullptr, nullptr, nullptr);
    if (g_font) SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);

    y += H + PAD;
    mkButton(hwnd, withKey(L"◀ Naviga",  A_NAV_PREV).c_str(), IDB_PREV,    row(0), y, W, H);
    mkButton(hwnd, withKey(L"Naviga ▶",  A_NAV_NEXT).c_str(), IDB_NEXT,    row(1), y, W, H);
    mkButton(hwnd, withKey(L"Apri/Edit", A_OPEN).c_str(),     IDB_OPEN,    row(2), y, W, H);
    mkButton(hwnd, withKey(L"Roll",      A_ROLL).c_str(),     IDB_ROLL,    row(3), y, W, H);
    mkButton(hwnd, withKey(L"Conferma",  A_CONFIRM).c_str(),  IDB_CONFIRM, row(4), y, W, H);

    y += H + PAD;
    mkButton(hwnd, withKey(L"Avanti",         A_ADVANCE).c_str(),   IDB_ADVANCE, row(0), y, W, H);
    mkButton(hwnd, withKey(L"Conf. continua", A_CONTINUE).c_str(),  IDB_CONTINUE,row(1), y, W, H);
    mkButton(hwnd, withKey(L"Canc. lettera",  A_DEL_LETTER).c_str(),IDB_DELL,    row(2), y, W, H);
    mkButton(hwnd, withKey(L"Canc. parola",   A_DEL_WORD).c_str(),  IDB_DELW,    row(3), y, W, H);

    y += H + PAD;
    mkButton(hwnd, withKey(L".", A_DOT).c_str(),   IDB_DOT,   row(0), y, W / 2, H);
    mkButton(hwnd, withKey(L",", A_COMMA).c_str(), IDB_COMMA, row(0) + W / 2, y, W / 2, H);
    mkButton(hwnd, withKey(L"?", A_QUES).c_str(),  IDB_QUES,  row(1), y, W / 2, H);
    mkButton(hwnd, withKey(L"!", A_EXCL).c_str(),  IDB_EXCL,  row(1) + W / 2, y, W / 2, H);
    mkButton(hwnd, withKey(L"Read",  A_READ).c_str(),  IDB_READ,  row(2), y, W, H);
    mkButton(hwnd, withKey(L"Write", A_WRITE).c_str(), IDB_WRITE, row(3), y, W, H);
    mkButton(hwnd, withKey(L"Scarta", A_DISCARD).c_str(), IDB_DISCARD, row(4), y, W, H);

    setStatus();
}

static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: createPanel(hwnd); return 0;
        case WM_COMMAND: {
            switch (LOWORD(wp)) {
                case IDB_PLAY: togglePlay(); break;
                case IDB_MODE:
                    g_mode = (Mode)((g_mode + 1) % 3);   // T9 -> Classica -> Multi-tap
                    g_engine.setMode(g_mode == MODE_T9); // T9 vs Literal (Classica/Multi-tap)
                    resetMultiTap();
                    setStatus();
                    break;
                case IDB_PREV:    performAction(A_NAV_PREV); break;
                case IDB_NEXT:    performAction(A_NAV_NEXT); break;
                case IDB_OPEN:    performAction(A_OPEN); break;
                case IDB_ROLL:    performAction(A_ROLL); break;
                case IDB_CONFIRM: performAction(A_CONFIRM); break;
                case IDB_ADVANCE: performAction(A_ADVANCE); break;
                case IDB_CONTINUE:performAction(A_CONTINUE); break;
                case IDB_DELL:    performAction(A_DEL_LETTER); break;
                case IDB_DELW:    performAction(A_DEL_WORD); break;
                case IDB_DOT:     performAction(A_DOT); break;
                case IDB_COMMA:   performAction(A_COMMA); break;
                case IDB_QUES:    performAction(A_QUES); break;
                case IDB_EXCL:    performAction(A_EXCL); break;
                case IDB_READ:    performAction(A_READ); break;
                case IDB_WRITE:   performAction(A_WRITE); break;
                case IDB_DISCARD: performAction(A_DISCARD); break;
            }
            return 0;
        }
        case WM_DESTROY:
            if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ dati
// Cartella dell'eseguibile (ANSI, per coerenza con std::ifstream narrow di MSVC).
static std::string exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    int len = WideCharToMultiByte(CP_ACP, 0, dir.c_str(), (int)dir.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, dir.c_str(), (int)dir.size(),
                        out.data(), len, nullptr, nullptr);
    return out;
}

static std::string dataPath(const char* name) {
    // 1) Pacchetto ricollocabile: <cartella-exe>/data/<name>.
    std::string p = exeDir() + "\\data\\" + name;
    { std::ifstream f(p, std::ios::binary); if (f.good()) return p; }
    // 2) Fallback build-tree per lo sviluppo (path assoluto compilato).
#ifdef SOHW_DATA_DIR
    return std::string(SOHW_DATA_DIR) + "/" + name;
#else
    return std::string("data/") + name;
#endif
}

// keymap T9 stile cellulare: w e a s d z x c = tasti 2..9 (Spazio = 0 = conferma).
//   w=2=abc  e=3=def  a=4=ghi  s=5=jkl  d=6=mno  z=7=pqrs  x=8=tuv  c=9=wxyz
static onehand::KeyMap buildKeymap() {
    onehand::KeyMap km;
    km.groups[L'w'] = L"abc";  km.groups[L'e'] = L"def";  km.groups[L'a'] = L"ghi";
    km.groups[L's'] = L"jkl";  km.groups[L'd'] = L"mno";  km.groups[L'z'] = L"pqrs";
    km.groups[L'x'] = L"tuv";  km.groups[L'c'] = L"wxyz";
    return km;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    // --- MOTORE ---
    onehand::Config cfg;
    g_keymap = buildKeymap();      // tenuto anche nel FE per il multi-tap
    cfg.keymap = g_keymap;
    cfg.maxCandidates = 8;
    g_engine.setConfig(cfg);                    // PRIMA di caricare i dati
    { std::ifstream wl(dataPath("wordlist_it.txt"), std::ios::binary);
      if (wl) g_engine.loadWordlist(wl); }
    g_engine.loadBigramModel(dataPath("it.bigrams.bin"));
    g_engine.setMode(true);                     // assistita

    // --- mappatura tasti -> funzioni (dal file di conf; default se assente) ---
    if (!loadBindings(dataPath("tasti.conf"))) defaultBindings();

    // --- font ---
    g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_overlayFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    g_sugFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // --- overlay window ---
    WNDCLASSEXW oc = { sizeof(oc) };
    oc.lpfnWndProc = OverlayProc; oc.hInstance = inst;
    oc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    oc.lpszClassName = L"SohwOverlay";
    RegisterClassExW(&oc);
    // Niente WS_EX_TRANSPARENT: la finestra riceve il mouse ed è trascinabile.
    // WS_EX_NOACTIVATE: trascinarla non ruba il focus all'app in cui si scrive.
    const int OVW = 900, OVH = 66;   // riga testo + riga suggerimenti
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED |
                                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                oc.lpszClassName, L"", WS_POPUP,
                                (sw - OVW) / 2, sh - 160, OVW, OVH, nullptr, nullptr, inst, nullptr);
    SetLayeredWindowAttributes(g_overlay, 0, 235, LWA_ALPHA);

    // --- panel window ---
    WNDCLASSEXW pc = { sizeof(pc) };
    pc.lpfnWndProc = PanelProc; pc.hInstance = inst;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    pc.lpszClassName = L"SohwPanel";
    RegisterClassExW(&pc);
    RECT r = { 0, 0, 530, 216 };   // più alto: bottoni a due righe (testo + tasto)
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
    g_panel = CreateWindowExW(0, pc.lpszClassName,
                              L"SmartOneHandWriter — Assistente",
                              WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, inst, nullptr);
    ShowWindow(g_panel, show);
    UpdateWindow(g_panel);
    refreshOverlay();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_hook) UnhookWindowsHookEx(g_hook);
    if (g_font) DeleteObject(g_font);
    if (g_overlayFont) DeleteObject(g_overlayFont);
    if (g_sugFont) DeleteObject(g_sugFont);
    return 0;
}
