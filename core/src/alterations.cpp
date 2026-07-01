#include "alterations.hpp"

#include <cwctype>

namespace onehand {

namespace {
// Varianti accentate proponibili per una singola lettera (IT). towupper/towlower
// non maiuscolizzano gli accentati sotto la locale "C", quindi le coppie
// minuscola/maiuscola sono elencate esplicitamente.
struct AccentPair { wchar_t lo; wchar_t up; };

const std::vector<AccentPair>& accentsFor(wchar_t base) {
    static const std::vector<AccentPair> a = { {L'à', L'À'} };
    static const std::vector<AccentPair> e = { {L'è', L'È'}, {L'é', L'É'} };
    static const std::vector<AccentPair> i = { {L'ì', L'Ì'} };
    static const std::vector<AccentPair> o = { {L'ò', L'Ò'} };
    static const std::vector<AccentPair> u = { {L'ù', L'Ù'} };
    static const std::vector<AccentPair> none;
    switch (base) {
        case L'a': return a;
        case L'e': return e;
        case L'i': return i;
        case L'o': return o;
        case L'u': return u;
        default:   return none;
    }
}

void pushUnique(std::vector<std::wstring>& v, const std::wstring& s) {
    if (s.empty()) return;
    for (const auto& e : v) if (e == s) return;
    v.push_back(s);
}
} // namespace

std::wstring capitalizeFirst(const std::wstring& w) {
    if (w.empty()) return w;
    std::wstring r = w;
    r[0] = static_cast<wchar_t>(towupper(r[0]));
    return r;
}

std::vector<std::wstring> alterationsOf(const std::wstring& word) {
    std::vector<std::wstring> out;
    if (word.empty()) return out;

    if (word.size() == 1) {
        wchar_t lo = static_cast<wchar_t>(towlower(word[0]));
        wchar_t up = static_cast<wchar_t>(towupper(lo));
        pushUnique(out, std::wstring(1, lo));
        if (up != lo) pushUnique(out, std::wstring(1, up));
        for (const auto& acc : accentsFor(lo)) {
            pushUnique(out, std::wstring(1, acc.lo));
            pushUnique(out, std::wstring(1, acc.up));
        }
        return out;
    }

    // parola: base cosi' com'e', poi Capitalizzata e MAIUSCOLA.
    pushUnique(out, word);
    pushUnique(out, capitalizeFirst(word));
    std::wstring upper = word;
    for (auto& c : upper) c = static_cast<wchar_t>(towupper(c));
    pushUnique(out, upper);
    return out;
}

} // namespace onehand
