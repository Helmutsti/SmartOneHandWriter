// Test end-to-end del CORE (M6): pipeline provider -> rank -> next, toggle
// modalita', e (se i dati reali sono presenti) un caso contestuale reale.
#include "sohw/core.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool has(const std::vector<sohw::Suggestion>& v, const std::string& s) {
    for (auto& x : v) if (x.word == s) return true;
    return false;
}

int main() {
    // --- pipeline in memoria, senza modello (ranking = frequenza) ----------
    {
        sohw::Core core;
        assert(core.mode() == sohw::InputMode::T9);

        std::istringstream wl(
            "sole\t100\nsono\t90\nsala\t80\ncasa\t70\ncasetta\t60\n");
        core.loadWordlist(wl);

        // T9 "76" (s,o) -> sole, sono (per frequenza, nessun bigramma caricato).
        auto r = core.process(sohw::Context{"", ""}, "76", 8, 5);
        assert(r.matches.size() == 2);
        assert(r.matches[0].word == "sole" && r.matches[1].word == "sono");
        // Senza modello: nessun suggerimento di parola successiva.
        assert(r.nextByMatch.size() == r.matches.size());
        assert(r.nextByMatch[0].empty());

        // A2: la punteggiatura nel contesto non rompe il matching (tokenizzata a parte).
        auto rp = core.process(sohw::Context{"sono,", ""}, "76", 8, 5);
        assert(rp.matches.size() == 2 && rp.matches[0].word == "sole");

        // Toggle a digitazione classica: "cas" -> casa, casetta.
        core.setMode(sohw::InputMode::Literal);
        auto r2 = core.process(sohw::Context{"", ""}, "cas", 8, 5);
        assert(r2.matches.size() == 2);
        assert(r2.matches[0].word == "casa" && r2.matches[1].word == "casetta");
    }
    std::puts("sohw_tests: OK (pipeline in memoria)");

    // --- integrazione coi dati reali (se presenti) -------------------------
#ifdef SOHW_DATA_DIR
    {
        const std::string dir = SOHW_DATA_DIR;
        std::ifstream wl(dir + "/wordlist_it.txt", std::ios::binary);
        if (wl) {
            sohw::Core core;
            core.loadWordlist(wl);
            core.loadBigramModel(dir + "/it.bigrams.bin");  // no-op se assente

            // Contesto "per", codice T9 di "la" (l=5, a=2) -> "52". Con il bigramma
            // per->la il candidato "la" deve emergere tra i match.
            auto r = core.process(sohw::Context{"per", ""}, "52", 8, 5);
            assert(!r.matches.empty());
            assert(has(r.matches, "la"));
            std::printf("sohw_tests: integrazione, top match = '%s' (score=%.4f), %zu match\n",
                        r.matches[0].word.c_str(), r.matches[0].score, r.matches.size());
            if (!r.nextByMatch.empty() && !r.nextByMatch[0].empty())
                std::printf("  next-word[0] = '%s'\n", r.nextByMatch[0][0].word.c_str());
        } else {
            std::puts("sohw_tests: integrazione SALTATA (wordlist reale assente)");
        }
    }
#endif
    return 0;
}
