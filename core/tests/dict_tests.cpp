// Test dei nuovi metodi del Dictionary per la "nuova concezione":
//  - computeCandidatesPrefix: matching T9 con COMPLETAMENTO (len >= n)
//  - completionsOf:           completamento per prefisso (digitazione classica)
// Piu' un round-trip utf8ToW/wToUtf8. Nessuna dipendenza esterna.
#include "dictionary.hpp"
#include "utf8.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using onehand::Dictionary;
using onehand::utf8ToW;
using onehand::wToUtf8;

// Costruisce un gruppo di lettere da una stringa ASCII ("abc" -> {a,b,c}).
static std::vector<wchar_t> grp(const char* s) {
    std::vector<wchar_t> g;
    for (const char* p = s; *p; ++p) g.push_back(static_cast<wchar_t>(*p));
    return g;
}

// Converte la lista di candidati (wstring) in UTF-8, per confronti leggibili.
static std::vector<std::string> u8(const std::vector<std::wstring>& v) {
    std::vector<std::string> out;
    for (auto& w : v) out.push_back(wToUtf8(w));
    return out;
}

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v) if (x == s) return true;
    return false;
}

int main() {
    // Round-trip UTF-8 con accento.
    assert(wToUtf8(utf8ToW("citt\xC3\xA0")) == "citt\xC3\xA0");  // "città"

    Dictionary d;
    std::string data =
        "# test wordlist\n"
        "sole\t100\n"
        "sono\t90\n"
        "sala\t80\n"
        "casa\t70\n"
        "casetta\t60\n"
        "citt\xC3\xA0\t50\n"       // città
        "cittadino\t40\n";
    std::istringstream in(data);
    d.load(in);

    // --- T9 con completamento: len >= n ------------------------------------
    // Codice s,o = [pqrs][mno]: parole con len>=2 che iniziano s,o -> sole, sono.
    {
        std::vector<std::vector<wchar_t>> g = { grp("pqrs"), grp("mno") };
        auto c = u8(d.computeCandidatesPrefix(g, 8));
        assert(c.size() == 2);
        assert(c[0] == "sole");   // freq 100 prima
        assert(c[1] == "sono");   // freq 90
        // "sala" inizia s,a ma a non e' nel gruppo [mno] -> escluso.
        assert(!contains(c, "sala"));
    }

    // Codice esatto di "sole" s,o,l,e = [pqrs][mno][jkl][def], n=4 -> solo "sole".
    {
        std::vector<std::vector<wchar_t>> g = { grp("pqrs"), grp("mno"), grp("jkl"), grp("def") };
        auto c = u8(d.computeCandidatesPrefix(g, 8));
        assert(c.size() == 1 && c[0] == "sole");
    }

    // --- Completamento per prefisso (digitazione classica) -----------------
    {
        auto c = u8(d.completionsOf(utf8ToW("cas"), 8));
        assert(c.size() == 2);
        assert(c[0] == "casa");     // 70 > 60
        assert(c[1] == "casetta");
    }
    {
        // Prefisso con accento: "citt" deve pescare "città" e "cittadino".
        auto c = u8(d.completionsOf(utf8ToW("citt"), 8));
        assert(c.size() == 2);
        assert(c[0] == "citt\xC3\xA0");   // città (50) > cittadino (40)
        assert(c[1] == "cittadino");
    }

    std::puts("dict_tests: OK (M2 len>=n + prefisso)");
    return 0;
}
