// OneHand - frontend Windows (C++/Win32), modello T9.
//
// Sottile strato specifico per Windows attorno al motore portabile
// (onehand::Engine). Si occupa solo di: catturare i tasti (hook low-level),
// iniettare testo (SendInput), disegnare la finestrella Play/Stop e il popup.
// TUTTA la logica (dizionario T9, composizione, alterazioni) vive nel core.
//
// Input T9: ogni pressione di un tasto "lettera" e' un TASTO del keymap (una
// cifra 2..9 sul tastierino, oppure una lettera per la digitazione diretta) che
// il core traduce nel gruppo di lettere e disambigua. Niente doppio-tap: ogni
// funzione e' una singola pressione, rimappabile.
//
// NB: l'accesso casuale (OpenWordAt via click) e' riservato all'editor interno;
// qui, scrivendo in app esterne via SendInput, restiamo lineari (Open prev/next).

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

// Funzioni rimappabili (una singola pressione ciascuna). Il core esegue "azioni"
// (Action); il frontend decide quale tasto fisico (VK) le attiva.
enum { FN_ROLL = 0, FN_DELC = 1, FN_DELW = 2, FN_CONFIRM_SP = 3, FN_CONFIRM = 4,
       FN_OPEN_PREV = 5, FN_OPEN_NEXT = 6, FN_COUNT = 7 };

static const char* const FN_CFGKEY[FN_COUNT] = {
    "roll_key", "delete_char_key", "delete_word_key", "confirm_space_key",
    "confirm_key", "open_prev_key", "open_next_key" };
static const wchar_t* const FN_LABEL[FN_COUNT] = {
    L"Roll (alterna)", L"Canc. lettera", L"Canc. parola", L"Conferma + spazio",
    L"Conferma", L"Apri prec.", L"Apri succ." };
static const UINT FN_DEFAULT_VK[FN_COUNT] = {
    VK_TAB, VK_BACK, VK_DELETE, VK_SPACE, VK_RSHIFT, VK_LEFT, VK_RIGHT };
static const onehand::Action FN_ACTION[FN_COUNT] = {
    onehand::Action::Rolling, onehand::Action::DeleteChar, onehand::Action::DeleteWord,
    onehand::Action::ConfirmNewWord, onehand::Action::Accept,
    onehand::Action::OpenPrevWord, onehand::Action::OpenNextWord };

// ID dei controlli.
enum {
    ID_BTN_PLAY       = 1,
    ID_COMBO_WORDLIST = 4,
    ID_BTN_SAVE       = 5,
    ID_BTN_CAP_BASE   = 10,   // 10.. : bottone "Assegna" per funzione (base + FN_*)
};

// ------------------------------------------------------------------ globali
static HHOOK             g_hook = nullptr;
static std::atomic<bool> g_active{false};
static HWND              g_mainWnd  = nullptr;
static HWND              g_btn      = nullptr;
static HWND              g_popupWnd = nullptr;
static HINSTANCE         g_inst     = nullptr;

static onehand::Engine   g_engine;       // il motore portabile
static std::wstring      g_popupText;    // testo correntemente mostrato nel popup

static HWND              g_btnSave   = nullptr;
static HWND              g_comboWl   = nullptr;

static onehand::Config   g_cfg;                   // config corrente
static std::string       g_cfgText;               // testo grezzo di config.json (per "Salva")
static std::wstring      g_cfgPath;               // percorso dove riscrivere config.json

// mapping funzione -> tasto VK. *Vk = attivo (hook); *Pend = in modifica nella UI.
static UINT g_fnVk[FN_COUNT]   = {0};
static UINT g_fnPend[FN_COUNT] = {0};
static HWND g_fnFld[FN_COUNT]  = {0};
static HWND g_fnBtn[FN_COUNT]  = {0};

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

static std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::string replaceJsonString(std::string s, const std::string& key, const std::string& val) {
    std::size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return s;
    std::size_t c = s.find(':', k);
    if (c == std::string::npos) return s;
    std::size_t i = c + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size() || s[i] != '"') return s;
    std::size_t e = s.find('"', i + 1);
    if (e == std::string::npos) return s;
    return s.substr(0, i + 1) + val + s.substr(e);
}

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

static std::string upsertJsonString(std::string s, const std::string& key, const std::string& val) {
    if (s.find("\"" + key + "\"") != std::string::npos)
        return replaceJsonString(s, key, val);
    std::size_t b = s.find('{');
    if (b == std::string::npos) return s;
    return s.substr(0, b + 1) + "\n  \"" + key + "\": \"" + val + "\"," + s.substr(b + 1);
}

// VK <-> token stabile per il config (indipendente dalla lingua di Windows).
static std::string vkToToken(UINT vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    switch (vk) {
        case VK_SPACE:  return "Space";
        case VK_TAB:    return "Tab";
        case VK_RETURN: return "Enter";
        case VK_BACK:   return "Backspace";
        case VK_DELETE: return "Delete";
        case VK_LEFT:   return "Left";
        case VK_RIGHT:  return "Right";
        case VK_RSHIFT: return "RShift";
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
    if (t == "Delete")    return VK_DELETE;
    if (t == "Left")      return VK_LEFT;
    if (t == "Right")     return VK_RIGHT;
    if (t == "RShift")    return VK_RSHIFT;
    bool num = true;
    for (char c : t) if (c < '0' || c > '9') { num = false; break; }
    if (num) { UINT v = 0; for (char c : t) v = v * 10 + (UINT)(c - '0'); return v; }
    return fallback;
}

static std::wstring keyName(UINT vk) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    switch (vk) {
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
    if (fx.popup.visible) showPopup(fx.popup.text);
    else                  hidePopup();
}

static void performAction(onehand::Action a, wchar_t key = 0) {
    applyEffects(g_engine.onAction(a, key));
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
            if (ctrl || alt || win)
                return CallNextHookEx(g_hook, code, wp, lp);

            bool isFunc = false;
            for (int i = 0; i < FN_COUNT; ++i) if (vk == g_fnVk[i]) { isFunc = true; break; }

            // tasto "lettera": cifre T9 (riga o tastierino) e lettere per la
            // digitazione diretta. Il core mappa il tasto sul suo gruppo.
            bool isDigit  = (vk >= '0' && vk <= '9') || (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9);
            bool isLetter = (vk >= 'A' && vk <= 'Z');

            bool handled = false, swallow = false;
            if (isFunc)               { handled = true; swallow = true;  }
            else if (vk == VK_RETURN) { handled = true; swallow = false; }  // Invio: finalizza ma passa all'app
            else if (isDigit || isLetter) { handled = true; swallow = true; }

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

// ------------------------------------------------------------------ config
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
    LRESULT idx = SendMessageW(g_comboWl, CB_FINDSTRINGEXACT, (WPARAM)-1,
                               (LPARAM)g_cfg.wordlistName.c_str());
    if (idx == CB_ERR) {
        SendMessageW(g_comboWl, CB_ADDSTRING, 0, (LPARAM)g_cfg.wordlistName.c_str());
        idx = SendMessageW(g_comboWl, CB_FINDSTRINGEXACT, (WPARAM)-1,
                           (LPARAM)g_cfg.wordlistName.c_str());
    }
    SendMessageW(g_comboWl, CB_SETCURSEL, idx, 0);
}

static void saveConfigFromUi() {
    // dizionario selezionato
    std::wstring wl;
    LRESULT sel = SendMessageW(g_comboWl, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        wchar_t wbuf[260] = {0};
        SendMessageW(g_comboWl, CB_GETLBTEXT, sel, (LPARAM)wbuf);
        wl = wbuf;
    }

    // stesso tasto su due funzioni: ambiguo, non ammesso.
    for (int i = 0; i < FN_COUNT; ++i)
        for (int j = i + 1; j < FN_COUNT; ++j)
            if (g_fnPend[i] == g_fnPend[j]) {
                std::wstring msg = std::wstring(L"«") + FN_LABEL[i] + L"» e «" + FN_LABEL[j] +
                                   L"» hanno lo stesso tasto. Cambiane uno.";
                MessageBoxW(g_mainWnd, msg.c_str(), L"OneHand", MB_ICONERROR);
                return;
            }

    std::string text = g_cfgText;
    if (!wl.empty()) text = replaceJsonString(text, "wordlist", wToUtf8(wl));
    for (int i = 0; i < FN_COUNT; ++i)
        text = upsertJsonString(text, FN_CFGKEY[i], vkToToken(g_fnPend[i]));

    if (!writeFileA(g_cfgPath, text)) {
        MessageBoxW(g_mainWnd, L"Impossibile scrivere config.json.", L"OneHand", MB_ICONERROR);
        return;
    }
    g_cfgText = text;
    for (int i = 0; i < FN_COUNT; ++i) g_fnVk[i] = g_fnPend[i];
    applyConfigText(text);
    populateWordlists();
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
        applyEffects(g_engine.reset());
        SetWindowTextW(g_btn, L"⏹  Stop");
    } else if (!on && g_active) {
        if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        g_active = false;
        applyEffects(g_engine.reset());
        SetWindowTextW(g_btn, L"▶  Play");
    }
}

// ------------------------------------------------------------------ cattura tasto
static LRESULT CALLBACK CaptureProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        UINT vk = ((KBDLLHOOKSTRUCT*)lp)->vkCode;
        switch (vk) {
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            case VK_MENU: case VK_LMENU: case VK_RMENU:
            case VK_LWIN: case VK_RWIN: case VK_CAPITAL:
                if (vk != VK_RSHIFT) return CallNextHookEx(g_capHook, code, wp, lp);
        }
        PostMessageW(g_mainWnd, WM_APP_CAPTURED, (WPARAM)vk, (LPARAM)g_capturing);
        return 1;
    }
    return CallNextHookEx(g_capHook, code, wp, lp);
}

static void startCapture(int fn) {
    if (g_capturing >= 0 || fn < 0 || fn >= FN_COUNT) return;
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
        HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto mk = [&](LPCWSTR cls, LPCWSTR txt, DWORD style,
                      int x, int y, int w, int hh, int id) -> HWND {
            HWND c = CreateWindowW(cls, txt, WS_CHILD | WS_VISIBLE | style,
                                   x, y, w, hh, h, (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
            return c;
        };
        mk(L"STATIC", L"Dizionario:", 0, 15, 12, 438, 18, 0);
        g_comboWl = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                       15, 32, 438, 200, ID_COMBO_WORDLIST);

        mk(L"STATIC", L"Tasti funzione (Assegna, poi premi un tasto):", 0, 15, 66, 438, 18, 0);
        for (int i = 0; i < FN_COUNT; ++i) {
            int y = 88 + i * 28;
            mk(L"STATIC", FN_LABEL[i], SS_CENTERIMAGE, 15, y, 130, 22, 0);
            g_fnFld[i] = mk(L"EDIT", L"", WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
                            153, y, 190, 22, 0);
            g_fnBtn[i] = mk(L"BUTTON", L"Assegna", BS_PUSHBUTTON,
                            351, y, 102, 22, ID_BTN_CAP_BASE + i);
        }

        int by = 88 + FN_COUNT * 28 + 12;
        g_btnSave = mk(L"BUTTON", L"Salva", BS_PUSHBUTTON, 15, by, 210, 34, ID_BTN_SAVE);
        g_btn     = mk(L"BUTTON", L"▶  Play", BS_PUSHBUTTON, 243, by, 210, 34, ID_BTN_PLAY);

        populateWordlists();
        for (int i = 0; i < FN_COUNT; ++i)
            SetWindowTextW(g_fnFld[i], keyName(g_fnPend[i]).c_str());
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_BTN_PLAY) { setActive(!g_active); return 0; }
        if (id == ID_BTN_SAVE) { saveConfigFromUi(); return 0; }
        if (id >= ID_BTN_CAP_BASE && id < ID_BTN_CAP_BASE + FN_COUNT && code == BN_CLICKED) {
            startCapture(id - ID_BTN_CAP_BASE);
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
            if (vk != VK_ESCAPE) {
                g_fnPend[fn] = vk;
                SetWindowTextW(g_fnFld[fn], keyName(vk).c_str());
            }
        }
        return 0;
    }
    case WM_APP_KEY: {
        UINT vk = (UINT)wp;
        for (int i = 0; i < FN_COUNT; ++i)
            if (g_fnVk[i] == vk) { performAction(FN_ACTION[i]); return 0; }

        if (vk == VK_RETURN) {
            performAction(onehand::Action::Finalize);
        } else if (vk >= '0' && vk <= '9') {
            performAction(onehand::Action::Letter, (wchar_t)vk);            // cifra riga
        } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
            performAction(onehand::Action::Letter, (wchar_t)(L'0' + (vk - VK_NUMPAD0)));  // tastierino
        } else if (vk >= 'A' && vk <= 'Z') {
            performAction(onehand::Action::Letter, (wchar_t)(L'a' + (vk - 'A')));         // lettera diretta
        }
        return 0;
    }
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

    g_cfgPath = dir + L"config.json";
    g_cfgText = readFileA(g_cfgPath);
    if (g_cfgText.empty()) { g_cfgPath = L"config.json"; g_cfgText = readFileA(g_cfgPath); }

    g_cfg = onehand::parseConfig(g_cfgText);
    g_engine.setConfig(g_cfg);

    for (int i = 0; i < FN_COUNT; ++i) {
        g_fnVk[i]   = tokenToVk(readJsonString(g_cfgText, FN_CFGKEY[i]), FN_DEFAULT_VK[i]);
        g_fnPend[i] = g_fnVk[i];
    }

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

    int winH = 88 + FN_COUNT * 28 + 12 + 34 + 60;
    g_mainWnd = CreateWindowW(L"OneHandMain", L"OneHand",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 485, winH,
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
