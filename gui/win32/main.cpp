// SmartOneHandWriter - banco di prova Win32 del CORE "nuova concezione".
//
// GUI nativa (zero dipendenze esterne) a 4 campi + toggle, cablata direttamente
// a sohw::Core. NON usa hook/iniezione: e' solo un banco per osservare il CORE.
//   1) Contesto, con un marcatore dello slot parola (▮).
//   2) Parola codificata (simboli T9, o lettere reali in modalita' Classico).
//   3) Parole decodificate (match ordinati per contesto).
//   4) Parole predette (next-word del match in cima).
// Aggiornamento live a ogni modifica. UTF-8 (core) <-> UTF-16 (Win32) ai bordi.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "sohw/core.hpp"

#include <fstream>
#include <string>

// ------------------------------------------------------------------ id controlli
enum {
    IDC_CTX   = 101,
    IDC_ENC   = 102,
    IDC_MODE  = 103,   // checkbox: spuntato = Classico (Literal)
    IDC_MATCH = 104,
    IDC_NEXT  = 105,
};

static const wchar_t kSlot = L'\x25AE';   // ▮

static sohw::Core* g_core = nullptr;
static HFONT       g_font = nullptr;
static HWND        g_ctx = nullptr, g_enc = nullptr, g_mode = nullptr,
                   g_match = nullptr, g_next = nullptr;

// ------------------------------------------------------------------ UTF-8 <-> UTF-16
static std::string wToU8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring u8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return {};
    std::wstring w((size_t)n + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize((size_t)n);
    return w;
}

// ------------------------------------------------------------------ ricalcolo
static void recompute() {
    if (!g_core) return;
    bool literal = (SendMessageW(g_mode, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_core->setMode(literal ? sohw::InputMode::Literal : sohw::InputMode::T9);

    std::wstring ctx = getText(g_ctx);
    std::wstring left = ctx, right;
    size_t slot = ctx.find(kSlot);
    if (slot != std::wstring::npos) { left = ctx.substr(0, slot); right = ctx.substr(slot + 1); }

    sohw::Context c{ wToU8(left), wToU8(right) };
    sohw::CoreResult r = g_core->process(c, wToU8(getText(g_enc)), 8, 6);

    std::wstring m;
    for (const auto& s : r.matches) {
        wchar_t sc[32]; swprintf(sc, 32, L"\t%.4f", s.score);
        m += u8ToW(s.word) + sc + L"\r\n";
    }
    SetWindowTextW(g_match, m.c_str());

    std::wstring n;
    if (!r.matches.empty() && !r.nextByMatch.empty()) {
        n += L"dopo \"" + u8ToW(r.matches[0].word) + L"\":\r\n";
        for (const auto& s : r.nextByMatch[0]) {
            wchar_t sc[32]; swprintf(sc, 32, L"\t%.4f", s.score);
            n += u8ToW(s.word) + sc + L"\r\n";
        }
    }
    SetWindowTextW(g_next, n.c_str());
}

// ------------------------------------------------------------------ creazione UI
static HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style,
               int x, int y, int w, int h, HWND parent, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void createUi(HWND hwnd) {
    const int X = 12, W = 596;
    mk(L"STATIC", L"Contesto (usa \x25AE per indicare lo slot della parola):",
       0, X, 10, W, 18, hwnd, -1);
    g_ctx = mk(L"EDIT", L"per \x25AE", WS_BORDER | ES_AUTOHSCROLL, X, 30, W, 24, hwnd, IDC_CTX);

    mk(L"STATIC", L"Parola codificata (T9, es. 52 = \"la\"):", 0, X, 62, W, 18, hwnd, -1);
    g_enc = mk(L"EDIT", L"52", WS_BORDER | ES_AUTOHSCROLL, X, 82, W, 24, hwnd, IDC_ENC);

    g_mode = mk(L"BUTTON", L"Digitazione classica (matching OFF: l'input sono lettere reali)",
                BS_AUTOCHECKBOX, X, 114, W, 20, hwnd, IDC_MODE);

    mk(L"STATIC", L"Parole decodificate (match, ordinati per contesto):",
       0, X, 142, W, 18, hwnd, -1);
    g_match = mk(L"EDIT", L"",
                 WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                 X, 162, W, 190, hwnd, IDC_MATCH);

    mk(L"STATIC", L"Parole predette (next-word del match in cima):",
       0, X, 360, W, 18, hwnd, -1);
    g_next = mk(L"EDIT", L"",
                WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                X, 380, W, 150, hwnd, IDC_NEXT);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        createUi(hwnd);
        recompute();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if ((id == IDC_CTX || id == IDC_ENC) && code == EN_CHANGE) recompute();
        else if (id == IDC_MODE && code == BN_CLICKED) recompute();
        return 0;
    }
    case WM_DESTROY:
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    sohw::Core core;
    { std::ifstream wl(dataPath("wordlist_it.txt"), std::ios::binary);
      if (wl) core.loadWordlist(wl); }
    core.loadBigramModel(dataPath("it.bigrams.bin"));   // no-op se assente
    g_core = &core;

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SohwCoreTester";
    RegisterClassExW(&wc);

    RECT r = { 0, 0, 620, 552 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
                                L"SmartOneHandWriter \x2014 banco CORE (Win32)",
                                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_core = nullptr;
    if (g_font) DeleteObject(g_font);
    return 0;
}
