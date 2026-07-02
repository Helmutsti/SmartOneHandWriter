// Test dei candidate provider (M3): T9 (con completamento) e Literal (prefisso /
// passthrough). Costruiscono Dictionary + KeyMap in memoria e verificano l'output
// UTF-8. Nessuna dipendenza esterna.
#include "sohw/candidate_provider.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using onehand::Dictionary;
using onehand::defaultT9KeyMap;

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v) if (x == s) return true;
    return false;
}

int main() {
    Dictionary d;
    std::string data =
        "sole\t100\n"
        "sono\t90\n"
        "sala\t80\n"
        "casa\t70\n"
        "casetta\t60\n";
    std::istringstream in(data);
    d.load(in);

    onehand::KeyMap km = defaultT9KeyMap();  // 7->pqrs 6->mno 5->jkl 3->def ...

    // --- T9: "76" (s,o) con completamento -> sole, sono ---------------------
    {
        sohw::T9CandidateProvider t9(d, km);
        auto c = t9.candidates("76", 8);
        assert(c.size() == 2);
        assert(c[0] == "sole" && c[1] == "sono");   // ordinati per frequenza
        assert(!contains(c, "sala"));               // 's','a' ma 'a' non e' in [mno]
    }

    // --- T9: "7653" = codice esatto di "sole" ------------------------------
    {
        sohw::T9CandidateProvider t9(d, km);
        auto c = t9.candidates("7653", 8);
        assert(c.size() == 1 && c[0] == "sole");
    }

    // --- Literal completion: "cas" -> casa, casetta ------------------------
    {
        sohw::LiteralCandidateProvider lit(d, /*completion=*/true);
        auto c = lit.candidates("cas", 8);
        assert(c.size() == 2 && c[0] == "casa" && c[1] == "casetta");
        // Case-insensitive: "CAS" deve dare lo stesso risultato.
        auto c2 = lit.candidates("CAS", 8);
        assert(c2 == c);
    }

    // --- Literal passthrough: input restituito com'e' (minuscolo) ----------
    {
        sohw::LiteralCandidateProvider lit(d, /*completion=*/false);
        auto c = lit.candidates("XyZ", 8);
        assert(c.size() == 1 && c[0] == "xyz");
    }

    std::puts("provider_tests: OK (M3 T9 + Literal)");
    return 0;
}
