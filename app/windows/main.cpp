// SmartOneHandWriter - Assistente di digitazione (frontend Windows).
//
// FE sottile (adattatore di solo I/O) sopra il MOTORE condiviso (motore::Engine),
// che a sua volta sta sopra il CORE (sohw::Core). Vedi docs/ARCHITETTURA.md.
//
//  - Pannello: un bottone per funzione + Play/Pause + toggle Assistita/Classica.
//  - Overlay: box colorato senza bordi, riga singola, vicino al mouse, che mostra
//    il buffer con parola selezionata/aperta evidenziate; sparisce a buffer vuoto.
//  - Hook tastiera globale (WH_KEYBOARD_LL): in assistita w e a s d z x c = gruppi
//    T9 come su un cellulare (w=2 e=3 a=4 s=5 d=6 z=7 x=8 c=9); Spazio=0=Conferma
//    continua; F=Roll G=Conferma R=Avanti T=Apri V/B=Naviga;
//    Tab/Backspace/BlocMaiusc=cancella; Esc=Scarta; 1..4=. , ? !; 5=Write; `=Read.
//  - Read/Write via clipboard (Read = legge; Write = incolla con Ctrl+V).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "motore/engine.hpp"
#include "onehand/types.hpp"   // onehand::Config / KeyMap

#include <fstream>
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
static HFONT          g_font   = nullptr;
static HFONT          g_overlayFont = nullptr;

struct PaintSpan { std::wstring text; int hl; bool spaceBefore; };
static std::vector<PaintSpan> g_spans;

// colori
static const COLORREF kBg     = RGB(43, 43, 43);
static const COLORREF kFg     = RGB(240, 240, 240);
static const COLORREF kSelBg  = RGB(58, 110, 165);   // azzurro
static const COLORREF kOpenBg = RGB(217, 164, 65);   // ambra

// ------------------------------------------------------------------ azioni
enum Act {
    A_NONE = 0, A_NAV_PREV, A_NAV_NEXT, A_OPEN, A_ROLL, A_CONFIRM, A_ADVANCE,
    A_CONTINUE, A_DEL_LETTER, A_DEL_WORD, A_DOT, A_COMMA, A_QUES, A_EXCL,
    A_READ, A_WRITE, A_DISCARD
};

static void refreshOverlay();

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

static void performAction(Act a) {
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
        g_spans.push_back({ u8ToW(s.text), (int)s.hl, s.spaceBefore });

    if (g_engine.empty()) { ShowWindow(g_overlay, SW_HIDE); return; }

    // posiziona vicino al mouse
    POINT pt; GetCursorPos(&pt);
    const int W = 900, H = 40;
    SetWindowPos(g_overlay, HWND_TOPMOST, pt.x + 12, pt.y + 22, W, H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
    int total = 0;
    for (const auto& s : g_spans) {
        if (s.spaceBefore) total += spaceW;
        SIZE sz; GetTextExtentPoint32W(hdc, s.text.c_str(), (int)s.text.size(), &sz);
        total += sz.cx;
    }
    int scroll = (total > rc.right - 12) ? (total - (rc.right - 12)) : 0;

    int x = 6 - scroll;
    const int y = 8;
    for (const auto& s : g_spans) {
        if (s.spaceBefore) x += spaceW;
        SIZE sz; GetTextExtentPoint32W(hdc, s.text.c_str(), (int)s.text.size(), &sz);
        if (s.hl == (int)motore::Highlight::Selected || s.hl == (int)motore::Highlight::Open) {
            RECT hr = { x - 2, y - 2, x + sz.cx + 2, y + sz.cy + 2 };
            HBRUSH hb = CreateSolidBrush(s.hl == (int)motore::Highlight::Open ? kOpenBg : kSelBg);
            FillRect(hdc, &hr, hb); DeleteObject(hb);
        }
        SetTextColor(hdc, kFg);
        TextOutW(hdc, x, y, s.text.c_str(), (int)s.text.size());
        x += sz.cx;
    }
    SelectObject(hdc, old);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) { paintOverlay(hwnd); return 0; }
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

static bool isMapped(DWORD vk) {
    if (vk == VK_SPACE || vk == VK_TAB || vk == VK_BACK || vk == VK_CAPITAL) return true;
    if (vk == VK_ESCAPE) return true;                // Scarta
    if (vk == VK_OEM_3) return true;                 // `
    if (vk >= '1' && vk <= '5') return true;
    if (vk >= 'A' && vk <= 'Z') return true;
    return false;
}

static bool handleKeyDown(DWORD vk) {
    const bool assisted = g_engine.assisted();

    // comuni a entrambe le modalità
    switch (vk) {
        case VK_SPACE:   performAction(A_CONTINUE); return true;
        case VK_TAB:     performAction(A_DEL_WORD); return true;
        case VK_BACK:    performAction(A_DEL_LETTER); return true;
        case VK_CAPITAL: performAction(A_DEL_LETTER); return true;
        case '1':        performAction(A_DOT);   return true;
        case '2':        performAction(A_COMMA); return true;
        case '3':        performAction(A_QUES);  return true;
        case '4':        performAction(A_EXCL);  return true;
        case '5':        performAction(A_WRITE); return true;
        case VK_OEM_3:   performAction(A_READ);  return true;
        case VK_ESCAPE:  performAction(A_DISCARD); return true;
        default: break;
    }

    if (assisted) {
        // funzioni su lettere (in assistita le lettere non-gruppo sono libere)
        switch (vk) {
            case 'F': performAction(A_ROLL);     return true;
            case 'G': performAction(A_CONFIRM);  return true;
            case 'R': performAction(A_ADVANCE);  return true;
            case 'T': performAction(A_OPEN);     return true;
            case 'V': performAction(A_NAV_PREV); return true;
            case 'B': performAction(A_NAV_NEXT); return true;
            default: break;
        }
        if (isGroupKey(vk)) {
            std::string sym; groupSymbol(vk, sym);
            g_engine.typeKey(sym); refreshOverlay(); return true;
        }
        return false;   // altre lettere non usate: lasciale passare
    } else {
        // classica: ogni lettera è letterale
        if (vk >= 'A' && vk <= 'Z') {
            std::string sym; groupSymbol(vk, sym);
            g_engine.typeKey(sym); refreshOverlay(); return true;
        }
        return false;
    }
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
    s += g_engine.assisted() ? L"  |  Modalità: Assistita (T9)" : L"  |  Modalità: Classica";
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
    HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (g_font) SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    return b;
}

static void createPanel(HWND hwnd) {
    const int W = 96, H = 30, PAD = 6;
    int x = PAD, y = PAD;
    auto row = [&](int col) { return PAD + col * (W + PAD); };
    mkButton(hwnd, L"Play/Pause", IDB_PLAY, row(0), y, W, H);
    mkButton(hwnd, L"Modalità",   IDB_MODE, row(1), y, W, H);
    g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                             row(2), y + 4, 3 * (W + PAD), 22, hwnd, nullptr, nullptr, nullptr);
    if (g_font) SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);

    y += H + PAD;
    mkButton(hwnd, L"◀ Naviga",   IDB_PREV,     row(0), y, W, H);
    mkButton(hwnd, L"Naviga ▶",   IDB_NEXT,     row(1), y, W, H);
    mkButton(hwnd, L"Apri/Edit",  IDB_OPEN,     row(2), y, W, H);
    mkButton(hwnd, L"Roll",       IDB_ROLL,     row(3), y, W, H);
    mkButton(hwnd, L"Conferma",   IDB_CONFIRM,  row(4), y, W, H);

    y += H + PAD;
    mkButton(hwnd, L"Avanti",       IDB_ADVANCE,  row(0), y, W, H);
    mkButton(hwnd, L"Conf. continua",IDB_CONTINUE,row(1), y, W, H);
    mkButton(hwnd, L"Canc. lettera",IDB_DELL,     row(2), y, W, H);
    mkButton(hwnd, L"Canc. parola", IDB_DELW,     row(3), y, W, H);

    y += H + PAD;
    mkButton(hwnd, L".",  IDB_DOT,   row(0), y, W / 2, H);
    mkButton(hwnd, L",",  IDB_COMMA, row(0) + W / 2, y, W / 2, H);
    mkButton(hwnd, L"?",  IDB_QUES,  row(1), y, W / 2, H);
    mkButton(hwnd, L"!",  IDB_EXCL,  row(1) + W / 2, y, W / 2, H);
    mkButton(hwnd, L"Read",  IDB_READ,  row(2), y, W, H);
    mkButton(hwnd, L"Write", IDB_WRITE, row(3), y, W, H);
    mkButton(hwnd, L"Scarta (Esc)", IDB_DISCARD, row(4), y, W, H);

    setStatus();
}

static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: createPanel(hwnd); return 0;
        case WM_COMMAND: {
            switch (LOWORD(wp)) {
                case IDB_PLAY: togglePlay(); break;
                case IDB_MODE: g_engine.setMode(!g_engine.assisted()); setStatus(); break;
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
static std::string dataPath(const char* name) {
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
    cfg.keymap = buildKeymap();
    cfg.maxCandidates = 8;
    g_engine.setConfig(cfg);                    // PRIMA di caricare i dati
    { std::ifstream wl(dataPath("wordlist_it.txt"), std::ios::binary);
      if (wl) g_engine.loadWordlist(wl); }
    g_engine.loadBigramModel(dataPath("it.bigrams.bin"));
    g_engine.setMode(true);                     // assistita

    // --- font ---
    g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_overlayFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

    // --- overlay window ---
    WNDCLASSEXW oc = { sizeof(oc) };
    oc.lpfnWndProc = OverlayProc; oc.hInstance = inst;
    oc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    oc.lpszClassName = L"SohwOverlay";
    RegisterClassExW(&oc);
    g_overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                oc.lpszClassName, L"", WS_POPUP,
                                0, 0, 900, 40, nullptr, nullptr, inst, nullptr);
    SetLayeredWindowAttributes(g_overlay, 0, 235, LWA_ALPHA);

    // --- panel window ---
    WNDCLASSEXW pc = { sizeof(pc) };
    pc.lpfnWndProc = PanelProc; pc.hInstance = inst;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    pc.lpszClassName = L"SohwPanel";
    RegisterClassExW(&pc);
    RECT r = { 0, 0, 530, 176 };
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
    return 0;
}
