// OneHand (C++/Win32) - scrittura a una mano, a livello di sistema.
//
// Finestrella Play/Stop. Quando e' attivo, intercetta la tastiera in QUALSIASI
// applicazione: digiti lo "scheletro" della parola (lettere + spazio come jolly)
// e il programma inietta nel campo che ha il focus la parola ricostruita dal
// dizionario. Un piccolo popup mostra le alternative vicino al cursore di testo.
//
// Tasti (mentre e' attivo):
//   lettere A-Z      -> compongono la parola
//   spazio  (1x)     -> jolly '?'  (una lettera da indovinare)
//   spazio  (2x)     -> accetta la parola + spazio vero
//   Backspace (1x)   -> cancella un carattere
//   Backspace (2x)   -> cancella l'intera parola (corrente o precedente)
//   Tab              -> scorre le alternative (solo se c'e' una parola in corso)
//   Invio            -> conferma la parola e lascia passare l'Invio all'app
//   Ctrl/Alt/Win+... -> ignorati (le scorciatoie funzionano normalmente)
//
// Build: vedi build.bat (MSVC) oppure README.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <atomic>
#include <cwctype>
#include <cstdlib>

// ------------------------------------------------------------------ costanti
static const ULONG_PTR INJ_SIG   = 0xC0DE5151;  // firma degli eventi iniettati
static const UINT      WM_APP_KEY = WM_APP + 1;  // tasto inoltrato dall'hook
static const UINT_PTR  TIMER_DBL  = 1;           // timer doppia pressione

// ------------------------------------------------------------------ globali
static HHOOK g_hook = nullptr;
static std::atomic<bool> g_active{false};
static std::atomic<bool> g_hasWord{false};
static HWND g_mainWnd  = nullptr;
static HWND g_btn      = nullptr;
static HWND g_popupWnd = nullptr;
static HINSTANCE g_inst = nullptr;

// configurazione
static int          g_doubleMs = 280;
static int          g_maxCand  = 8;
static std::wstring g_available = L"qwertasdfgzxcvb";
static bool         g_wildAny  = false;
static std::wstring g_punct    = L",.?!:;'()-";  // tavolozza punteggiatura

// dizionario
struct Word { std::wstring w; double f; };
static std::vector<Word> g_words;
static std::set<wchar_t> g_wildSet;

// stato di composizione
static std::wstring              g_pattern;     // scheletro corrente (lettere + '?')
static std::vector<std::wstring> g_cands;       // candidati per la parola corrente
static int                       g_idx = 0;     // candidato selezionato
static std::wstring              g_preview;     // testo gia' iniettato per la parola corrente
static std::vector<std::wstring> g_committed;   // parole confermate (ognuna seguita da spazio)
static UINT                      g_pendingVk = 0; // tasto in attesa del "doppio"
static std::wstring              g_popupText;   // testo mostrato nel popup

// stato di spaziatura / maiuscole / punteggiatura
static bool g_capNext      = true;   // la prossima parola va in maiuscolo (inizio frase)
static bool g_trailingSpace = false; // c'e' uno spazio finale nostro dopo l'ultima parola
static bool g_singleLetter = false;  // la parola corrente e' una sola lettera (roller maiuscola)
static bool g_punctMode    = false;  // stiamo scegliendo un segno di punteggiatura
static int  g_punctIdx     = 0;      // segno selezionato nella tavolozza
static bool g_removedSpace = false;  // abbiamo tolto lo spazio prima del segno (da ripristinare)

// ------------------------------------------------------------------ utilita'
static std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string exeDirA() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t s = p.find_last_of("\\/");
    return (s == std::string::npos) ? "" : p.substr(0, s + 1);
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

// ------------------------------------------------------------------ dizionario
static void loadConfig() {
    std::ifstream f(exeDirA() + "config.json", std::ios::binary);
    if (!f) f.open("config.json", std::ios::binary);
    if (!f) return;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto findVal = [&](const std::string& key) -> std::string {
        size_t k = s.find("\"" + key + "\"");
        if (k == std::string::npos) return "";
        size_t c = s.find(':', k);
        if (c == std::string::npos) return "";
        size_t i = c + 1;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        if (i >= s.size()) return "";
        if (s[i] == '"') {
            size_t e = s.find('"', i + 1);
            return s.substr(i + 1, e - i - 1);
        }
        size_t e = i;
        while (e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != '\n') ++e;
        std::string v = s.substr(i, e - i);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t')) v.pop_back();
        return v;
    };

    std::string av = findVal("available_keys");
    if (!av.empty()) g_available = utf8ToW(av);
    std::string wm = findVal("wildcard_matches");
    if (wm == "any") g_wildAny = true;
    std::string mc = findVal("max_candidates");
    if (!mc.empty()) g_maxCand = atoi(mc.c_str());
    std::string dp = findVal("double_press_ms");
    if (!dp.empty()) g_doubleMs = atoi(dp.c_str());
    std::string pu = findVal("punctuation");
    if (!pu.empty()) g_punct = utf8ToW(pu);
}

static std::string configWordlistName() {
    std::ifstream f(exeDirA() + "config.json", std::ios::binary);
    if (!f) f.open("config.json", std::ios::binary);
    if (!f) return "wordlist_it.txt";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t k = s.find("\"wordlist\"");
    if (k == std::string::npos) return "wordlist_it.txt";
    size_t q1 = s.find('"', s.find(':', k) + 1);
    size_t q2 = s.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) return "wordlist_it.txt";
    return s.substr(q1 + 1, q2 - q1 - 1);
}

static void loadWordlist() {
    std::string name = configWordlistName();
    std::ifstream f(exeDirA() + name, std::ios::binary);
    if (!f) f.open(name, std::ios::binary);
    if (!f) {
        MessageBoxW(nullptr, L"Dizionario non trovato (wordlist_it.txt).",
                    L"OneHand", MB_ICONWARNING);
        return;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }
    long total = (long)lines.size();
    long rank = 0;
    for (auto& ln : lines) {
        // separatore: tab oppure spazio
        size_t sep = ln.find('\t');
        if (sep == std::string::npos) sep = ln.find(' ');
        std::string ws = (sep == std::string::npos) ? ln : ln.substr(0, sep);
        std::string fs = (sep == std::string::npos) ? "" : ln.substr(sep + 1);
        std::wstring w = utf8ToW(ws);
        for (auto& c : w) c = (wchar_t)towlower(c);
        if (w.empty()) { ++rank; continue; }
        double freq;
        if (!fs.empty()) {
            freq = atof(fs.c_str());
            if (freq == 0.0) freq = (double)(total - rank);
        } else {
            freq = (double)(total - rank);
        }
        g_words.push_back({w, freq});
        ++rank;
    }

    // insieme dei caratteri che un jolly puo' rappresentare
    if (!g_wildAny && !g_available.empty()) {
        std::set<wchar_t> avail(g_available.begin(), g_available.end());
        std::set<wchar_t> alpha;
        for (auto& e : g_words) for (wchar_t c : e.w) alpha.insert(c);
        for (wchar_t c : alpha) if (!avail.count(c)) g_wildSet.insert(c);
    } else {
        g_wildAny = true;
    }
}

// ------------------------------------------------------------------ motore
static std::vector<std::wstring> computeCandidates(const std::wstring& pat) {
    std::vector<std::wstring> out;
    if (pat.empty()) return out;
    if (pat.find(L'?') == std::wstring::npos) { out.push_back(pat); return out; }

    size_t n = pat.size();
    std::vector<std::pair<std::wstring, double>> m;
    for (auto& e : g_words) {
        if (e.w.size() != n) continue;
        bool ok = true;
        for (size_t i = 0; i < n; ++i) {
            wchar_t pc = pat[i], wc = e.w[i];
            if (pc == L'?') {
                if (!g_wildAny && !g_wildSet.count(wc)) { ok = false; break; }
            } else if (pc != wc) { ok = false; break; }
        }
        if (ok) m.push_back({e.w, e.f});
    }
    std::sort(m.begin(), m.end(),
              [](const std::pair<std::wstring,double>& a,
                 const std::pair<std::wstring,double>& b){ return a.second > b.second; });
    std::set<std::wstring> seen;
    for (auto& p : m) {
        if (seen.count(p.first)) continue;
        seen.insert(p.first);
        out.push_back(p.first);
        if ((int)out.size() >= g_maxCand) break;
    }
    if (out.empty()) out.push_back(pat);
    return out;
}

static std::wstring currentWord() {
    if (g_pattern.empty()) return L"";
    if (!g_cands.empty()) return g_cands[g_idx];
    return g_pattern;
}

// dichiarazione anticipata
static void updatePopup();

static void recompute() {
    g_cands = computeCandidates(g_pattern);
    g_idx = 0;
    g_singleLetter = (!g_pattern.empty() &&
                      g_pattern.find(L'?') == std::wstring::npos &&
                      g_pattern.size() == 1);
    if (g_singleLetter) {
        // su una sola lettera proponi anche la maiuscola
        wchar_t lo = g_pattern[0];
        wchar_t up = (wchar_t)towupper(lo);
        g_cands.clear();
        g_cands.push_back(std::wstring(1, lo));
        if (up != lo) g_cands.push_back(std::wstring(1, up));
        g_idx = (g_capNext && up != lo) ? 1 : 0;  // a inizio frase, default maiuscola
    }
    g_hasWord = !g_pattern.empty();
}

// parola da mostrare, con eventuale maiuscola iniziale a inizio frase
static std::wstring displayWord() {
    if (g_pattern.empty()) return L"";
    std::wstring w = currentWord();
    if (!g_singleLetter && g_capNext && !w.empty()) w[0] = (wchar_t)towupper(w[0]);
    return w;
}

// ridisegna l'anteprima nel campo: cancella la vecchia, scrive la nuova
static void render() {
    std::wstring cur = g_punctMode ? std::wstring(1, g_punct[g_punctIdx]) : displayWord();
    if (cur != g_preview) {
        sendBackspaces(g_preview.size());
        sendUnicode(cur);
        g_preview = cur;
    }
    updatePopup();
}

// ------------------------------------------------------------------ punteggiatura
static void enterPunctMode() {
    if (g_punct.empty()) return;
    g_punctMode = true;
    g_punctIdx = 0;
    // attacca il segno alla parola precedente: togli lo spazio finale (lo rimettiamo dopo)
    if (g_trailingSpace) { sendBackspaces(1); g_removedSpace = true; g_trailingSpace = false; }
    else g_removedSpace = false;
    g_preview.clear();
    g_hasWord = true;
    render();
}

static void cyclePunct() { g_punctIdx = (g_punctIdx + 1) % (int)g_punct.size(); render(); }

static void cancelPunct() {
    sendBackspaces(g_preview.size());
    g_preview.clear();
    if (g_removedSpace) { sendUnicode(L" "); g_trailingSpace = true; }
    g_removedSpace = false;
    g_punctMode = false;
    g_hasWord = false;
    updatePopup();
}

static void commitPunct() {
    wchar_t p = g_punct[g_punctIdx];
    sendUnicode(L" ");                       // spazio spostato dopo il segno
    if (g_removedSpace && !g_committed.empty() && !g_committed.back().empty()
        && g_committed.back().back() == L' ')
        g_committed.back().pop_back();        // la parola prima ha perso lo spazio
    g_committed.push_back(std::wstring(1, p) + L" ");
    g_preview.clear();
    g_punctMode = false; g_removedSpace = false; g_trailingSpace = true;
    if (p == L'.' || p == L'!' || p == L'?') g_capNext = true;  // maiuscola dopo il punto
    g_hasWord = false;
    updatePopup();
}

// ------------------------------------------------------------------ azioni
static void actLiteral(wchar_t ch) {
    if (g_punctMode) cancelPunct();
    g_pattern.push_back(ch); recompute(); render();
}

static void actWildcard() {
    if (g_punctMode) return;
    g_pattern.push_back(L'?'); recompute(); render();
}

static void actTab() {
    if (g_punctMode) { cyclePunct(); return; }
    if (!g_pattern.empty()) {
        if (!g_cands.empty()) { g_idx = (g_idx + 1) % (int)g_cands.size(); render(); }
        return;
    }
    enterPunctMode();                         // a inizio parola: scegli punteggiatura
}

static void actAccept() {
    if (g_punctMode) { commitPunct(); return; }
    if (g_pattern.empty()) return;
    std::wstring disp = displayWord();
    sendUnicode(L" ");
    g_committed.push_back(disp + L" ");
    g_pattern.clear(); g_cands.clear(); g_idx = 0; g_preview.clear();
    g_singleLetter = false; g_trailingSpace = true; g_capNext = false;
    g_hasWord = false;
    updatePopup();
}

static void actDeleteChar() {
    if (g_punctMode) { cancelPunct(); return; }
    if (!g_pattern.empty()) {
        g_pattern.pop_back();
        recompute(); render();
    } else if (!g_committed.empty()) {
        std::wstring tok = g_committed.back();
        g_committed.pop_back();
        sendBackspaces(tok.size());                 // rimuovi tutto il token
        if (!tok.empty() && tok.back() == L' ') tok.pop_back();
        g_pattern = tok.empty() ? L"" : tok.substr(0, tok.size() - 1);  // riapri senza ultimo char
        g_preview.clear();
        g_trailingSpace = (!g_committed.empty() && !g_committed.back().empty()
                           && g_committed.back().back() == L' ');
        recompute(); render();
    }
}

static void actDeleteWord() {
    if (g_punctMode) { cancelPunct(); return; }
    if (!g_pattern.empty()) {
        sendBackspaces(g_preview.size());
        g_pattern.clear(); g_cands.clear(); g_idx = 0; g_preview.clear();
        g_singleLetter = false; g_hasWord = false;
        updatePopup();
    } else if (!g_committed.empty()) {
        std::wstring tok = g_committed.back();
        g_committed.pop_back();
        sendBackspaces(tok.size());
        g_trailingSpace = (!g_committed.empty() && !g_committed.back().empty()
                           && g_committed.back().back() == L' ');
        updatePopup();
    }
}

static void actFinalizeOnEnter() {
    if (g_punctMode) { commitPunct(); }
    else if (!g_pattern.empty()) {
        g_committed.push_back(displayWord());   // resta nel campo, senza spazio
        g_preview.clear();
    }
    g_pattern.clear(); g_cands.clear(); g_idx = 0;
    g_singleLetter = false; g_trailingSpace = false; g_capNext = true;  // nuova riga
    g_hasWord = false;
    updatePopup();
}

static void resetComposition() {
    g_pattern.clear(); g_cands.clear(); g_idx = 0; g_preview.clear();
    g_committed.clear(); g_pendingVk = 0; g_hasWord = false;
    g_capNext = true; g_trailingSpace = false; g_singleLetter = false;
    g_punctMode = false; g_punctIdx = 0; g_removedSpace = false;
    updatePopup();
}

// ------------------------------------------------------------------ doppia pressione
static void doSingle(UINT vk) { if (vk == VK_SPACE) actWildcard();   else if (vk == VK_BACK) actDeleteChar(); }
static void doDouble(UINT vk) { if (vk == VK_SPACE) actAccept();     else if (vk == VK_BACK) actDeleteWord();  }

static void flushPending() {
    if (g_pendingVk) {
        KillTimer(g_mainWnd, TIMER_DBL);
        UINT v = g_pendingVk; g_pendingVk = 0;
        doSingle(v);
    }
}

static void processKey(UINT vk) {
    if (vk == VK_SPACE || vk == VK_BACK) {
        if (g_pendingVk == vk) {            // seconda pressione -> DOPPIA
            KillTimer(g_mainWnd, TIMER_DBL);
            g_pendingVk = 0;
            doDouble(vk);
        } else {                            // prima pressione -> attende
            flushPending();
            g_pendingVk = vk;
            SetTimer(g_mainWnd, TIMER_DBL, (UINT)g_doubleMs, nullptr);
        }
    } else {
        flushPending();
        if (vk == VK_TAB)        actTab();
        else if (vk == VK_RETURN) actFinalizeOnEnter();
        else if (vk >= 'A' && vk <= 'Z') actLiteral((wchar_t)(L'a' + (vk - 'A')));
    }
}

// ------------------------------------------------------------------ popup
static void updatePopup() {
    // mostra: la tavolozza punteggiatura, oppure le alternative quando sono piu' d'una
    bool show = g_active && (g_punctMode || g_cands.size() > 1);
    if (!show) {
        if (g_popupWnd) ShowWindow(g_popupWnd, SW_HIDE);
        return;
    }
    std::wstring t;
    if (g_punctMode) {
        for (int i = 0; i < (int)g_punct.size(); ++i) {
            if (i) t += L"  ";
            if (i == g_punctIdx) { t += L"["; t += g_punct[i]; t += L"]"; }
            else t += g_punct[i];
        }
    } else {
        for (int i = 0; i < (int)g_cands.size(); ++i) {
            if (i) t += L"   ";
            if (i == g_idx) { t += L"["; t += g_cands[i]; t += L"]"; }
            else t += g_cands[i];
        }
    }
    g_popupText = t;

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

    // dimensiona in base al testo
    HDC dc = GetDC(g_popupWnd);
    SIZE sz{ 0, 0 };
    GetTextExtentPoint32W(dc, g_popupText.c_str(), (int)g_popupText.size(), &sz);
    ReleaseDC(g_popupWnd, dc);
    int w = sz.cx + 20, h = sz.cy + 12;

    SetWindowPos(g_popupWnd, HWND_TOPMOST, pt.x, pt.y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_popupWnd, nullptr, TRUE);
}

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
            if (vk >= 'A' && vk <= 'Z')      { handled = true; swallow = true; }
            else if (vk == VK_SPACE || vk == VK_BACK) { handled = true; swallow = true; }
            else if (vk == VK_TAB)           { handled = true; swallow = true; }   // alternative / punteggiatura
            else if (vk == VK_RETURN)        { handled = true; swallow = false; } // finalizza ma passa

            if (handled) {
                PostMessageW(g_mainWnd, WM_APP_KEY, vk, 0);
                if (swallow) return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
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
        resetComposition();
        SetWindowTextW(g_btn, L"⏹  Stop");
    } else if (!on && g_active) {
        if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        KillTimer(g_mainWnd, TIMER_DBL);
        g_active = false;
        resetComposition();
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
    case WM_APP_KEY:
        processKey((UINT)wp);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_DBL) {
            KillTimer(h, TIMER_DBL);
            if (g_pendingVk) { UINT v = g_pendingVk; g_pendingVk = 0; doSingle(v); }
        }
        return 0;
    case WM_DESTROY:
        if (g_hook) UnhookWindowsHookEx(g_hook);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    loadConfig();
    loadWordlist();

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
