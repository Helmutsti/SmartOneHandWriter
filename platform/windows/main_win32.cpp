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
static const ULONG_PTR INJ_SIG    = 0xC0DE5151;  // firma degli eventi iniettati
static const UINT      WM_APP_KEY = WM_APP + 1;  // tasto inoltrato dall'hook
static const UINT_PTR  TIMER_DBL  = 1;           // timer doppia pressione

// ------------------------------------------------------------------ globali
static HHOOK             g_hook = nullptr;
static std::atomic<bool> g_active{false};
static HWND              g_mainWnd  = nullptr;
static HWND              g_btn      = nullptr;
static HWND              g_popupWnd = nullptr;
static HINSTANCE         g_inst     = nullptr;

static onehand::Engine   g_engine;       // il motore portabile
static std::wstring      g_popupText;    // testo correntemente mostrato nel popup

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

// ------------------------------------------------------------------ traduzione tasti
static bool vkToKey(UINT vk, onehand::KeyEvent& k) {
    if (vk >= 'A' && vk <= 'Z') { k.kind = onehand::KeyKind::Letter; k.letter = (wchar_t)(L'a' + (vk - 'A')); return true; }
    if (vk == VK_SPACE)  { k.kind = onehand::KeyKind::Space;     return true; }
    if (vk == VK_BACK)   { k.kind = onehand::KeyKind::Backspace; return true; }
    if (vk == VK_TAB)    { k.kind = onehand::KeyKind::Tab;       return true; }
    if (vk == VK_RETURN) { k.kind = onehand::KeyKind::Enter;     return true; }
    return false;
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

            bool handled = false, swallow = false;
            if (vk >= 'A' && vk <= 'Z')               { handled = true; swallow = true; }
            else if (vk == VK_SPACE || vk == VK_BACK) { handled = true; swallow = true; }
            else if (vk == VK_TAB)                    { handled = true; swallow = true; }
            else if (vk == VK_RETURN)                 { handled = true; swallow = false; } // passa l'Invio

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

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        g_btn = CreateWindowW(L"BUTTON", L"▶  Play",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              20, 20, 200, 60, h, (HMENU)1, g_inst, nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 1) { setActive(!g_active); return 0; }
        break;
    case WM_APP_KEY: {
        onehand::KeyEvent k;
        if (vkToKey((UINT)wp, k)) applyEffects(g_engine.onKey(k));
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_DBL) {
            KillTimer(h, TIMER_DBL);
            applyEffects(g_engine.onTimeout());
        }
        return 0;
    case WM_DESTROY:
        if (g_hook) UnhookWindowsHookEx(g_hook);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ------------------------------------------------------------------ avvio
static void loadConfigAndWordlist() {
    std::wstring dir = exeDirW();

    // config: lo legge il frontend, il core lo interpreta (mai apre file da solo)
    std::string cfgText = readFileA(dir + L"config.json");
    if (cfgText.empty()) cfgText = readFileA(L"config.json");
    onehand::Config cfg = onehand::parseConfig(cfgText);
    g_engine.setConfig(cfg);

    // dizionario: file esterno, aperto dal frontend
    std::wstring wlPath = dir + cfg.wordlistName;
    std::ifstream dict(wlPath.c_str(), std::ios::binary);
    if (!dict) dict.open(cfg.wordlistName.c_str(), std::ios::binary);
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
                              CW_USEDEFAULT, CW_USEDEFAULT, 260, 140,
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
