// OneHand - frontend Windows (C++/Win32).
//
// Sottile strato specifico per Windows attorno al motore portabile
// (onehand::Engine). Si occupa solo di: catturare i tasti (hook low-level),
// iniettare testo (SendInput), disegnare la finestrella Play/Stop e il popup,
// gestire il timer del doppio-tap. TUTTA la logica (dizionario, composizione,
// punteggiatura, maiuscole) vive nel core, in core/.
//
// Tasti (mentre e' attivo): vedi README. Le scorciatoie Ctrl/Alt/Win passano.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "onehand/engine.hpp"
#include "onehand/types.hpp"

#include <atomic>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// ------------------------------------------------------------------ costanti
static const ULONG_PTR INJ_SIG        = 0xC0DE5151;  // firma degli eventi iniettati
static const UINT      WM_APP_KEY      = WM_APP + 1; // tasto inoltrato dall'hook
static const UINT      WM_APP_CAPTURED = WM_APP + 2; // tasto catturato per una funzione
static const UINT_PTR  TIMER_DBL       = 1;          // timer doppia pressione

// Funzioni rimappabili. Il core esegue "azioni" (Action); il frontend decide
// quale tasto fisico (VK) e quale pressione (singola/doppia) le attiva. Ogni
// funzione e' indipendente: due funzioni possono condividere lo stesso tasto se
// hanno pressione diversa (es. wildcard=singola / conferma=doppia sullo spazio).
enum { FN_WILD = 0, FN_ROLL = 1, FN_DELC = 2, FN_DELW = 3, FN_ACCEPT = 4, FN_COUNT = 5 };
enum { MODE_SINGLE = 0, MODE_DOUBLE = 1 };

static const char* const FN_CFGKEY[FN_COUNT] =
    { "wildcard_key", "rolling_key", "delete_char_key", "delete_word_key", "accept_key" };
static const char* const FN_MODEKEY[FN_COUNT] =
    { "wildcard_mode", "rolling_mode", "delete_char_mode", "delete_word_mode", "accept_mode" };
static const wchar_t* const FN_LABEL[FN_COUNT] =
    { L"Wildcard", L"Rolling", L"Canc. lettera", L"Canc. parola", L"Accetta sugg." };
static const UINT FN_DEFAULT_VK[FN_COUNT] =
    { VK_SPACE, VK_TAB, VK_BACK, VK_BACK, VK_SPACE };
static const int FN_DEFAULT_MODE[FN_COUNT] =
    { MODE_SINGLE, MODE_SINGLE, MODE_SINGLE, MODE_DOUBLE, MODE_DOUBLE };
static const onehand::Action FN_ACTION[FN_COUNT] = {
    onehand::Action::Wildcard, onehand::Action::Rolling, onehand::Action::DeleteChar,
    onehand::Action::DeleteWord, onehand::Action::Accept
};

// ID dei controlli (ID_BTN_PLAY resta 1: gia' usato dal codice originale).
enum {
    ID_BTN_PLAY        = 1,
    ID_COMBO_HAND      = 2,
    ID_EDIT_KEYS       = 3,
    ID_COMBO_WORDLIST  = 4,
    ID_BTN_SAVE        = 5,
    ID_BTN_CAP_BASE    = 10,   // 10..15: bottone "Assegna" per funzione (base + FN_*)
    ID_COMBO_MODE_BASE = 20,   // 20..25: combo singola/doppia per funzione
};

// Preset dei tasti per una mano (layout QWERTY).
static const std::wstring PRESET_LEFT  = L"qwertasdfgzxcvb";
static const std::wstring PRESET_RIGHT = L"yuiophjklnm";

// ------------------------------------------------------------------ globali
static HHOOK             g_hook = nullptr;
static std::atomic<bool> g_active{false};
static HWND              g_mainWnd  = nullptr;
static HWND              g_btn      = nullptr;
static HWND              g_popupWnd = nullptr;
static HINSTANCE         g_inst     = nullptr;

static onehand::Engine   g_engine;       // il motore portabile
static std::wstring      g_popupText;    // testo correntemente mostrato nel popup

// controlli di configurazione (finestra principale)
static HWND              g_btnSave   = nullptr;
static HWND              g_comboHand = nullptr;
static HWND              g_editKeys  = nullptr;
static HWND              g_comboWl   = nullptr;

static onehand::Config   g_cfg;                   // config corrente (riempie la UI)
static std::string       g_cfgText;               // testo grezzo di config.json (per "Salva")
static std::wstring      g_cfgPath;               // percorso dove riscrivere config.json
static bool              g_suppressEnChange = false;  // evita il loop combo<->edit

// mapping funzione -> (tasto VK, modo singola/doppia). *Vk/*Mode = attivi (usati
// dall'hook); *Pend = in modifica nella UI, applicati solo al Salva. Valori reali
// impostati in loadConfigAndWordlist (default = comportamento storico).
static UINT g_fnVk[FN_COUNT]       = {0};
static int  g_fnMode[FN_COUNT]     = {0};
static UINT g_fnPend[FN_COUNT]     = {0};
static int  g_fnModePend[FN_COUNT] = {0};
static HWND g_fnFld[FN_COUNT]      = {0};
static HWND g_fnBtn[FN_COUNT]      = {0};
static HWND g_fnModeCbo[FN_COUNT]  = {0};

// stato del doppio-tap: la rilevazione singola/doppia e' del frontend.
static bool g_pending         = false;
static UINT g_pendingVk       = 0;
static int  g_pendingSingleFn = -1;   // funzione da eseguire su timeout (o -1)

// cattura "premi un tasto"
static HHOOK g_capHook   = nullptr;
static int   g_capturing = -1;    // -1 = nessuna; altrimenti indice FN_* in cattura

// ------------------------------------------------------------------ percorsi / file
static std::wstring exeDirW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? L"" : p.substr(0, s + 1);
}

static std::string readFileA(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool writeFileA(const std::wstring& path, const std::string& content) {
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    f.write(content.data(), (std::streamsize)content.size());
    return (bool)f;
}

// wchar_t -> UTF-8 (i valori scritti nel config sono ASCII, ma restiamo corretti).
static std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Sostituisce SOLO il valore-stringa della chiave data nel testo JSON, lasciando
// intatto tutto il resto (struttura annidata, altri campi, formattazione). Stessa
// logica tollerante di findVal() nel core. Se la chiave non c'e', torna invariato.
static std::string replaceJsonString(std::string s, const std::string& key, const std::string& val) {
    std::size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return s;
    std::size_t c = s.find(':', k);
    if (c == std::string::npos) return s;
    std::size_t i = c + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size() || s[i] != '"') return s;      // trattiamo solo valori stringa
    std::size_t e = s.find('"', i + 1);
    if (e == std::string::npos) return s;
    return s.substr(0, i + 1) + val + s.substr(e);   // rimpiazza il contenuto tra le virgolette
}

// Legge il valore-stringa di una chiave dal JSON (stessa logica di findVal nel core).
static std::string readJsonString(const std::string& s, const std::string& key) {
    std::size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    std::size_t c = s.find(':', k);
    if (c == std::string::npos) return "";
    std::size_t i = c + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size() || s[i] != '"') return "";
    std::size_t e = s.find('"', i + 1);
    if (e == std::string::npos) return "";
    return s.substr(i + 1, e - i - 1);
}

// Come replaceJsonString, ma se la chiave non esiste la inserisce (dopo la '{').
static std::string upsertJsonString(std::string s, const std::string& key, const std::string& val) {
    if (s.find("\"" + key + "\"") != std::string::npos)
        return replaceJsonString(s, key, val);
    std::size_t b = s.find('{');
    if (b == std::string::npos) return s;
    return s.substr(0, b + 1) + "\n  \"" + key + "\": \"" + val + "\"," + s.substr(b + 1);
}

// VK <-> token stabile per il config (indipendente dalla lingua di Windows):
// lettere/cifre = il carattere; tasti speciali = nome canonico; altro = numero VK.
static std::string vkToToken(UINT vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    switch (vk) {
        case VK_SPACE:  return "Space";
        case VK_TAB:    return "Tab";
        case VK_RETURN: return "Enter";
        case VK_BACK:   return "Backspace";
    }
    return std::to_string(vk);
}
static UINT tokenToVk(const std::string& t, UINT fallback) {
    if (t.empty()) return fallback;
    if (t.size() == 1) {
        char c = t[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (UINT)(unsigned char)c;
    }
    if (t == "Space")     return VK_SPACE;
    if (t == "Tab")       return VK_TAB;
    if (t == "Enter")     return VK_RETURN;
    if (t == "Backspace") return VK_BACK;
    bool num = true;
    for (char c : t) if (c < '0' || c > '9') { num = false; break; }
    if (num) { UINT v = 0; for (char c : t) v = v * 10 + (UINT)(c - '0'); return v; }
    return fallback;
}

// Nome leggibile del tasto per l'interfaccia (localizzato da Windows).
static std::wstring keyName(UINT vk) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    switch (vk) {  // tasti "estesi": serve il bit 24 per il nome corretto
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR:  case VK_NEXT:   case VK_LEFT: case VK_RIGHT:
        case VK_UP:     case VK_DOWN:   case VK_NUMLOCK: case VK_DIVIDE:
            lp |= (1 << 24);
    }
    wchar_t buf[64] = {0};
    if (GetKeyNameTextW(lp, buf, 64) > 0) return buf;
    return L"VK " + std::to_wstring(vk);
}

// ------------------------------------------------------------------ iniezione
static void sendUnicode(const std::wstring& s) {
    if (s.empty()) return;
    std::vector<INPUT> in;
    in.reserve(s.size() * 2);
    for (wchar_t c : s) {
        INPUT d{}; d.type = INPUT_KEYBOARD;
        d.ki.wScan = c; d.ki.dwFlags = KEYEVENTF_UNICODE; d.ki.dwExtraInfo = INJ_SIG;
        INPUT u = d; u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        in.push_back(d); in.push_back(u);
    }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

static void sendBackspaces(size_t n) {
    if (n == 0) return;
    std::vector<INPUT> in;
    in.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        INPUT d{}; d.type = INPUT_KEYBOARD;
        d.ki.wVk = VK_BACK; d.ki.dwExtraInfo = INJ_SIG;
        INPUT u = d; u.ki.dwFlags = KEYEVENTF_KEYUP;
        in.push_back(d); in.push_back(u);
    }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

// ------------------------------------------------------------------ popup
static void showPopup(const std::wstring& text) {
    g_popupText = text;

    // posizione: sotto il cursore di testo della finestra in primo piano
    POINT pt; bool got = false;
    HWND fg = GetForegroundWindow();
    if (fg) {
        GUITHREADINFO gti; gti.cbSize = sizeof(gti);
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        if (GetGUIThreadInfo(tid, &gti) && gti.hwndCaret) {
            POINT cp{ gti.rcCaret.left, gti.rcCaret.bottom };
            ClientToScreen(gti.hwndCaret, &cp);
            pt = cp; got = true;
        }
    }
    if (!got) { GetCursorPos(&pt); pt.y += 22; }

    HDC dc = GetDC(g_popupWnd);
    SIZE sz{ 0, 0 };
    GetTextExtentPoint32W(dc, g_popupText.c_str(), (int)g_popupText.size(), &sz);
    ReleaseDC(g_popupWnd, dc);
    int w = sz.cx + 20, h = sz.cy + 12;

    SetWindowPos(g_popupWnd, HWND_TOPMOST, pt.x, pt.y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_popupWnd, nullptr, TRUE);
}

static void hidePopup() {
    if (g_popupWnd) ShowWindow(g_popupWnd, SW_HIDE);
}

// ------------------------------------------------------------------ applica gli effetti
static void applyEffects(const onehand::Effects& fx) {
    for (const auto& e : fx.edits) {
        if (e.backspaces > 0) sendBackspaces((size_t)e.backspaces);
        if (!e.insert.empty()) sendUnicode(e.insert);
    }
    switch (fx.timer.action) {
        case onehand::TimerEffect::Action::Start:
            KillTimer(g_mainWnd, TIMER_DBL);
            SetTimer(g_mainWnd, TIMER_DBL, (UINT)fx.timer.ms, nullptr);
            break;
        case onehand::TimerEffect::Action::Cancel:
            KillTimer(g_mainWnd, TIMER_DBL);
            break;
        case onehand::TimerEffect::Action::None:
            break;
    }
    if (fx.popup.visible) showPopup(fx.popup.text);
    else                  hidePopup();
}

// Esegue un'azione risolta (il core non gestisce il timer: lo fa il frontend).
static void performAction(onehand::Action a, wchar_t letter = 0) {
    applyEffects(g_engine.onAction(a, letter));
}

// --- diagnostica temporanea: registra gli eventi tasto su un file leggibile ---
static void dbgLog(const std::string& line) {
    std::ofstream f((exeDirW() + L"onehand_debug.log").c_str(), std::ios::app);
    if (f) f << line << "\n";
}

// ------------------------------------------------------------------ hook
static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lp;
        if (p->dwExtraInfo == INJ_SIG)               // eventi nostri: lasciali passare
            return CallNextHookEx(g_hook, code, wp, lp);

        if (g_active && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
            UINT vk = p->vkCode;

            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt  = (p->flags & LLKHF_ALTDOWN) ||
                        ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
            bool win  = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
            if (ctrl || alt || win)                  // non toccare le scorciatoie
                return CallNextHookEx(g_hook, code, wp, lp);

            // il tasto attiva una funzione? (le funzioni si "ingoiano")
            bool isFunc = false;
            for (int i = 0; i < FN_COUNT; ++i) if (vk == g_fnVk[i]) { isFunc = true; break; }

            bool handled = false, swallow = false;
            if (isFunc)                      { handled = true; swallow = true;  }  // funzione: la gestiamo noi
            else if (vk == VK_RETURN)        { handled = true; swallow = false; }  // Invio: finalizza ma passa all'app
            else if (vk >= 'A' && vk <= 'Z') { handled = true; swallow = true;  }  // lettera normale

            if (handled) {
                PostMessageW(g_mainWnd, WM_APP_KEY, vk, 0);
                if (swallow) return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

// ------------------------------------------------------------------ popup paint
static LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(dc, &rc, bg); DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 220));
        rc.left += 10; rc.top += 6;
        DrawTextW(dc, g_popupText.c_str(), -1, &rc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ------------------------------------------------------------------ config UI
// Riapplica al motore una config (dopo un salvataggio): reimposta i parametri e
// ricarica il dizionario. Se la composizione e' attiva, la azzera.
static void applyConfigText(const std::string& text) {
    g_cfg = onehand::parseConfig(text);
    g_engine.setConfig(g_cfg);

    std::wstring dir = exeDirW();
    std::ifstream dict((dir + g_cfg.wordlistName).c_str(), std::ios::binary);
    if (!dict) dict.open(g_cfg.wordlistName.c_str(), std::ios::binary);
    if (dict) g_engine.loadWordlist(dict);
    else MessageBoxW(g_mainWnd, L"Dizionario non trovato.", L"OneHand", MB_ICONWARNING);

    if (g_active) applyEffects(g_engine.reset());
}

// Riempie la combo dei dizionari con i .txt presenti accanto all'exe.
static void populateWordlists() {
    SendMessageW(g_comboWl, CB_RESETCONTENT, 0, 0);
    std::wstring dir = exeDirW();
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW((dir + L"*.txt").c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do { SendMessageW(g_comboWl, CB_ADDSTRING, 0, (LPARAM)fd.cFileName); }
        while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    // assicura che il dizionario corrente sia presente e selezionato
    LRESULT idx = SendMessageW(g_comboWl, CB_FINDSTRINGEXACT, (WPARAM)-1,
                               (LPARAM)g_cfg.wordlistName.c_str());
    if (idx == CB_ERR) {
        SendMessageW(g_comboWl, CB_ADDSTRING, 0, (LPARAM)g_cfg.wordlistName.c_str());
        idx = SendMessageW(g_comboWl, CB_FINDSTRINGEXACT, (WPARAM)-1,
                           (LPARAM)g_cfg.wordlistName.c_str());
    }
    SendMessageW(g_comboWl, CB_SETCURSEL, idx, 0);
}

// Allinea combo "mano" + campo testo ai tasti correnti.
static void setHandCombo() {
    int sel = (g_cfg.availableKeys == PRESET_LEFT)  ? 0
            : (g_cfg.availableKeys == PRESET_RIGHT) ? 1 : 2;   // 2 = Personalizzato
    SendMessageW(g_comboHand, CB_SETCURSEL, sel, 0);
    g_suppressEnChange = true;                 // il SetWindowText qui non deve forzare "Personalizzato"
    SetWindowTextW(g_editKeys, g_cfg.availableKeys.c_str());
    g_suppressEnChange = false;
}

// Legge i controlli, riscrive config.json (solo i due valori) e riapplica.
static void saveConfigFromUi() {
    // tasti: minuscole, solo a-z
    wchar_t buf[256] = {0};
    GetWindowTextW(g_editKeys, buf, 256);
    std::wstring keys;
    for (wchar_t ch : std::wstring(buf)) {
        if (ch >= L'A' && ch <= L'Z') ch = (wchar_t)(ch - L'A' + L'a');
        if (ch >= L'a' && ch <= L'z') keys += ch;
    }
    if (keys.empty()) {
        MessageBoxW(g_mainWnd, L"Inserisci almeno una lettera (a-z) tra i tasti.",
                    L"OneHand", MB_ICONWARNING);
        return;
    }

    // dizionario selezionato nella combo
    std::wstring wl;
    LRESULT sel = SendMessageW(g_comboWl, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        wchar_t wbuf[260] = {0};
        SendMessageW(g_comboWl, CB_GETLBTEXT, sel, (LPARAM)wbuf);
        wl = wbuf;
    }

    // --- validazione dei tasti-funzione (valori in modifica: g_fnPend/g_fnModePend) ---
    // stesso tasto + stessa pressione su due funzioni: ambiguo, non ammesso.
    // (stesso tasto con pressioni diverse e' ok, es. spazio: singola=wildcard/doppia=conferma)
    for (int i = 0; i < FN_COUNT; ++i)
        for (int j = i + 1; j < FN_COUNT; ++j)
            if (g_fnPend[i] == g_fnPend[j] && g_fnModePend[i] == g_fnModePend[j]) {
                std::wstring msg = std::wstring(L"«") + FN_LABEL[i] + L"» e «" + FN_LABEL[j] +
                                   L"» hanno lo stesso tasto e la stessa pressione. Cambiane uno.";
                MessageBoxW(g_mainWnd, msg.c_str(), L"OneHand", MB_ICONERROR);
                return;
            }
    // avvisi (non bloccanti): funzione su una lettera fra i tasti disponibili
    std::wstring warn;
    for (int i = 0; i < FN_COUNT; ++i) {
        UINT vk = g_fnPend[i];
        if (vk >= 'A' && vk <= 'Z') {
            wchar_t low = (wchar_t)(L'a' + (vk - 'A'));
            if (keys.find(low) != std::wstring::npos)
                warn += std::wstring(L"\n• «") + FN_LABEL[i] + L"» sul tasto '" + low +
                        L"': quella lettera non sara' piu' digitabile.";
        }
    }
    if (!warn.empty() &&
        MessageBoxW(g_mainWnd, (L"Attenzione:" + warn + L"\n\nSalvare comunque?").c_str(),
                    L"OneHand", MB_ICONWARNING | MB_YESNO) != IDYES)
        return;

    // riscrive i valori nel testo esistente (preserva struttura e altri campi)
    std::string text = g_cfgText;
    text = replaceJsonString(text, "available_keys", wToUtf8(keys));
    if (!wl.empty()) text = replaceJsonString(text, "wordlist", wToUtf8(wl));
    for (int i = 0; i < FN_COUNT; ++i) {
        text = upsertJsonString(text, FN_CFGKEY[i],  vkToToken(g_fnPend[i]));
        text = upsertJsonString(text, FN_MODEKEY[i], g_fnModePend[i] == MODE_DOUBLE ? "double" : "single");
    }

    if (!writeFileA(g_cfgPath, text)) {
        MessageBoxW(g_mainWnd, L"Impossibile scrivere config.json.", L"OneHand", MB_ICONERROR);
        return;
    }
    g_cfgText = text;
    for (int i = 0; i < FN_COUNT; ++i) {   // rende attivo il nuovo mapping
        g_fnVk[i]   = g_fnPend[i];
        g_fnMode[i] = g_fnModePend[i];
    }
    applyConfigText(text);   // applica subito al motore (available_keys, dizionario)
    setHandCombo();          // riallinea la UI al nuovo stato
    MessageBoxW(g_mainWnd, L"Configurazione salvata e applicata.",
                L"OneHand", MB_ICONINFORMATION);
}

// ------------------------------------------------------------------ play/stop
static void setActive(bool on) {
    if (on && !g_active) {
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, g_inst, 0);
        if (!g_hook) {
            MessageBoxW(g_mainWnd, L"Impossibile installare l'hook tastiera.",
                        L"OneHand", MB_ICONERROR);
            return;
        }
        g_active = true;
        g_pending = false; g_pendingSingleFn = -1;
        applyEffects(g_engine.reset());
        SetWindowTextW(g_btn, L"⏹  Stop");
    } else if (!on && g_active) {
        if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        g_active = false;
        g_pending = false; g_pendingSingleFn = -1;
        KillTimer(g_mainWnd, TIMER_DBL);
        applyEffects(g_engine.reset());
        SetWindowTextW(g_btn, L"▶  Play");
    }
}

// ------------------------------------------------------------------ cattura tasto
// Hook temporaneo: cattura il prossimo tasto premuto e lo assegna alla funzione
// in cattura. Ignora i soli modificatori; Esc annulla.
static LRESULT CALLBACK CaptureProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        UINT vk = ((KBDLLHOOKSTRUCT*)lp)->vkCode;
        switch (vk) {  // aspetta un tasto "vero", non un modificatore da solo
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            case VK_MENU: case VK_LMENU: case VK_RMENU:
            case VK_LWIN: case VK_RWIN: case VK_CAPITAL:
                return CallNextHookEx(g_capHook, code, wp, lp);
        }
        PostMessageW(g_mainWnd, WM_APP_CAPTURED, (WPARAM)vk, (LPARAM)g_capturing);
        return 1;  // ingoia il tasto: non deve finire nell'app
    }
    return CallNextHookEx(g_capHook, code, wp, lp);
}

static void startCapture(int fn) {
    if (g_capturing >= 0 || fn < 0 || fn >= FN_COUNT) return;   // gia' in cattura
    g_capturing = fn;
    SetWindowTextW(g_fnBtn[fn], L"Premi un tasto… (Esc)");
    g_capHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureProc, g_inst, 0);
    if (!g_capHook) {
        g_capturing = -1;
        SetWindowTextW(g_fnBtn[fn], L"Assegna");
        MessageBoxW(g_mainWnd, L"Impossibile avviare la cattura del tasto.",
                    L"OneHand", MB_ICONERROR);
    }
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto mk = [&](LPCWSTR cls, LPCWSTR txt, DWORD style,
                      int x, int y, int w, int hh, int id) -> HWND {
            HWND c = CreateWindowW(cls, txt, WS_CHILD | WS_VISIBLE | style,
                                   x, y, w, hh, h, (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
            return c;
        };
        mk(L"STATIC", L"Mano disponibile:", 0, 15, 12, 345, 18, 0);
        g_comboHand = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                         15, 32, 345, 200, ID_COMBO_HAND);
        SendMessageW(g_comboHand, CB_ADDSTRING, 0, (LPARAM)L"Mano sinistra");
        SendMessageW(g_comboHand, CB_ADDSTRING, 0, (LPARAM)L"Mano destra");
        SendMessageW(g_comboHand, CB_ADDSTRING, 0, (LPARAM)L"Personalizzato");
        g_editKeys = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
                        15, 64, 345, 24, ID_EDIT_KEYS);
        mk(L"STATIC", L"Dizionario:", 0, 15, 98, 345, 18, 0);
        g_comboWl = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                       15, 118, 345, 200, ID_COMBO_WORDLIST);

        // sezione tasti-funzione: etichetta + campo (nome tasto) + "Assegna" + modo
        mk(L"STATIC", L"Tasti funzione (Assegna, poi premi un tasto):", 0, 15, 150, 345, 18, 0);
        for (int i = 0; i < FN_COUNT; ++i) {
            int y = 172 + i * 28;
            mk(L"STATIC", FN_LABEL[i], SS_CENTERIMAGE, 15, y, 92, 22, 0);
            g_fnFld[i] = mk(L"EDIT", L"", WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
                            110, y, 68, 22, 0);
            g_fnBtn[i] = mk(L"BUTTON", L"Assegna", BS_PUSHBUTTON,
                            183, y, 78, 22, ID_BTN_CAP_BASE + i);
            g_fnModeCbo[i] = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                266, y, 94, 120, ID_COMBO_MODE_BASE + i);
            SendMessageW(g_fnModeCbo[i], CB_ADDSTRING, 0, (LPARAM)L"Singola");
            SendMessageW(g_fnModeCbo[i], CB_ADDSTRING, 0, (LPARAM)L"Doppia");
        }

        int by = 172 + FN_COUNT * 28 + 12;
        g_btnSave = mk(L"BUTTON", L"Salva", BS_PUSHBUTTON, 15, by, 168, 34, ID_BTN_SAVE);
        g_btn     = mk(L"BUTTON", L"▶  Play", BS_PUSHBUTTON, 192, by, 168, 34, ID_BTN_PLAY);

        populateWordlists();   // riempie la combo dizionari dai .txt presenti
        setHandCombo();        // seleziona preset/mostra i tasti correnti
        for (int i = 0; i < FN_COUNT; ++i) {   // mostra tasto + modo correnti di ogni funzione
            SetWindowTextW(g_fnFld[i], keyName(g_fnPend[i]).c_str());
            SendMessageW(g_fnModeCbo[i], CB_SETCURSEL, g_fnModePend[i], 0);
        }
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_BTN_PLAY) { setActive(!g_active); return 0; }
        if (id == ID_BTN_SAVE) { saveConfigFromUi(); return 0; }
        if (id == ID_COMBO_HAND && code == CBN_SELCHANGE) {
            LRESULT s = SendMessageW(g_comboHand, CB_GETCURSEL, 0, 0);
            if (s == 0 || s == 1) {           // preset: riempi il campo testo
                g_suppressEnChange = true;
                SetWindowTextW(g_editKeys, (s == 0 ? PRESET_LEFT : PRESET_RIGHT).c_str());
                g_suppressEnChange = false;
            }
            return 0;
        }
        if (id == ID_EDIT_KEYS && code == EN_CHANGE) {
            if (!g_suppressEnChange)          // l'utente ha digitato: passa a "Personalizzato"
                SendMessageW(g_comboHand, CB_SETCURSEL, 2, 0);
            return 0;
        }
        if (id >= ID_BTN_CAP_BASE && id < ID_BTN_CAP_BASE + FN_COUNT && code == BN_CLICKED) {
            startCapture(id - ID_BTN_CAP_BASE);
            return 0;
        }
        if (id >= ID_COMBO_MODE_BASE && id < ID_COMBO_MODE_BASE + FN_COUNT && code == CBN_SELCHANGE) {
            int i = id - ID_COMBO_MODE_BASE;
            g_fnModePend[i] = (int)SendMessageW(g_fnModeCbo[i], CB_GETCURSEL, 0, 0);
            return 0;
        }
        break;
    }
    case WM_APP_CAPTURED: {
        if (g_capHook) { UnhookWindowsHookEx(g_capHook); g_capHook = nullptr; }
        int fn = (int)lp; UINT vk = (UINT)wp;
        g_capturing = -1;
        if (fn >= 0 && fn < FN_COUNT) {
            SetWindowTextW(g_fnBtn[fn], L"Assegna");
            if (vk != VK_ESCAPE) {            // Esc = annulla, lascia il valore precedente
                g_fnPend[fn] = vk;            // in attesa: diventa attivo al Salva
                SetWindowTextW(g_fnFld[fn], keyName(vk).c_str());
            }
        }
        return 0;
    }
    case WM_APP_KEY: {
        UINT vk = (UINT)wp;
        // quali funzioni sono su questo tasto? (una a pressione singola, una doppia)
        int sFn = -1, dFn = -1;
        for (int i = 0; i < FN_COUNT; ++i)
            if (g_fnVk[i] == vk) { if (g_fnMode[i] == MODE_DOUBLE) dFn = i; else sFn = i; }

        dbgLog("KEY vk=" + std::to_string(vk) + " sFn=" + std::to_string(sFn) +
               " dFn=" + std::to_string(dFn) + " pend=" + std::to_string(g_pending ? 1 : 0) +
               " pendVk=" + std::to_string(g_pendingVk));

        // seconda pressione dello stesso tasto in attesa -> azione "doppia"
        if (g_pending && vk == g_pendingVk && dFn != -1) {
            dbgLog("  -> DOPPIA fn=" + std::to_string(dFn));
            KillTimer(h, TIMER_DBL);
            g_pending = false; g_pendingSingleFn = -1;
            performAction(FN_ACTION[dFn]);
            return 0;
        }
        // un altro tasto era in attesa: risolvilo come pressione singola
        if (g_pending) {
            KillTimer(h, TIMER_DBL);
            int pf = g_pendingSingleFn;
            g_pending = false; g_pendingSingleFn = -1;
            if (pf != -1) performAction(FN_ACTION[pf]);
        }
        // gestisci il tasto corrente
        if (dFn != -1) {                     // puo' raddoppiare: aspetta il timer
            g_pending = true; g_pendingVk = vk; g_pendingSingleFn = sFn;
            SetTimer(h, TIMER_DBL, (UINT)g_cfg.doublePressMs, nullptr);
            dbgLog("  -> ATTESA (dFn=" + std::to_string(dFn) + " ms=" + std::to_string(g_cfg.doublePressMs) + ")");
            if (sFn != -1 && FN_ACTION[sFn] == onehand::Action::Wildcard)
                applyEffects(g_engine.previewWildcard());   // anteprima jolly durante l'attesa
        } else if (sFn != -1) {              // solo singola: esegui subito
            dbgLog("  -> SINGOLA-subito fn=" + std::to_string(sFn));
            performAction(FN_ACTION[sFn]);
        } else if (vk == VK_RETURN) {        // Invio: finalizza (chiude la parola, poi passa all'app)
            performAction(onehand::Action::Finalize);
        } else if (vk >= 'A' && vk <= 'Z') { // lettera normale
            performAction(onehand::Action::Letter, (wchar_t)(L'a' + (vk - 'A')));
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_DBL) {
            KillTimer(h, TIMER_DBL);
            if (g_pending) {                 // nessuna seconda pressione: azione "singola"
                int pf = g_pendingSingleFn;
                g_pending = false; g_pendingSingleFn = -1;
                dbgLog("TIMER -> SINGOLA fn=" + std::to_string(pf));
                if (pf != -1) performAction(FN_ACTION[pf]);
            } else {
                dbgLog("TIMER (nessun pending)");
            }
        }
        return 0;
    case WM_DESTROY:
        if (g_hook)    UnhookWindowsHookEx(g_hook);
        if (g_capHook) UnhookWindowsHookEx(g_capHook);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ------------------------------------------------------------------ avvio
static void loadConfigAndWordlist() {
    std::wstring dir = exeDirW();

    // config: lo legge il frontend, il core lo interpreta (mai apre file da solo).
    // Memorizzo percorso + testo grezzo: servono al bottone "Salva" per riscrivere
    // il file in-place preservandone struttura e campi non esposti nella UI.
    g_cfgPath = dir + L"config.json";
    g_cfgText = readFileA(g_cfgPath);
    if (g_cfgText.empty()) { g_cfgPath = L"config.json"; g_cfgText = readFileA(g_cfgPath); }

    g_cfg = onehand::parseConfig(g_cfgText);
    g_engine.setConfig(g_cfg);

    // mapping tasti-funzione: letto dal frontend (il core non conosce i tasti fisici).
    // Se una chiave manca nel file, resta il default storico.
    dbgLog("=== avvio: doublePressMs=" + std::to_string(g_cfg.doublePressMs) + " ===");
    for (int i = 0; i < FN_COUNT; ++i) {
        g_fnVk[i] = tokenToVk(readJsonString(g_cfgText, FN_CFGKEY[i]), FN_DEFAULT_VK[i]);
        std::string m = readJsonString(g_cfgText, FN_MODEKEY[i]);
        g_fnMode[i] = (m == "double") ? MODE_DOUBLE : (m == "single") ? MODE_SINGLE : FN_DEFAULT_MODE[i];
        g_fnPend[i]     = g_fnVk[i];
        g_fnModePend[i] = g_fnMode[i];
        dbgLog(std::string("  fn ") + FN_CFGKEY[i] + " vk=" + std::to_string(g_fnVk[i]) +
               " mode=" + (g_fnMode[i] == MODE_DOUBLE ? "double" : "single"));
    }

    // dizionario: file esterno, aperto dal frontend
    std::wstring wlPath = dir + g_cfg.wordlistName;
    std::ifstream dict(wlPath.c_str(), std::ios::binary);
    if (!dict) dict.open(g_cfg.wordlistName.c_str(), std::ios::binary);
    if (!dict) {
        MessageBoxW(nullptr, L"Dizionario non trovato (wordlist_it.txt).",
                    L"OneHand", MB_ICONWARNING);
        return;
    }
    g_engine.loadWordlist(dict);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    loadConfigAndWordlist();

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"OneHandMain";
    RegisterClassW(&wc);

    WNDCLASSW pc{};
    pc.lpfnWndProc = PopupProc;
    pc.hInstance = inst;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pc.lpszClassName = L"OneHandPopup";
    RegisterClassW(&pc);

    g_mainWnd = CreateWindowW(L"OneHandMain", L"OneHand",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 392, 420,
                              nullptr, nullptr, inst, nullptr);

    g_popupWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"OneHandPopup", L"", WS_POPUP,
        0, 0, 10, 10, nullptr, nullptr, inst, nullptr);

    ShowWindow(g_mainWnd, show);
    UpdateWindow(g_mainWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
