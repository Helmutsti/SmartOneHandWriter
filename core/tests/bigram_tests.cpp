// Test del BigramModel + BigramPredictor (M5).
//  1) Hermetico: costruisce un piccolo modello binario in memoria, lo scrive,
//     lo rilegge e verifica count/successors/rankCandidates/predictNext.
//  2) Integrazione (se SOHW_BIGRAM_BIN esiste): verifica bigrammi reali noti.
#include "sohw/bigram_model.hpp"
#include "sohw/bigram_predictor.hpp"
#include "sohw/bigram_format.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace sohw;

static void wr32(std::string& b, uint32_t v) {
    b.push_back((char)(v & 0xFF)); b.push_back((char)((v >> 8) & 0xFF));
    b.push_back((char)((v >> 16) & 0xFF)); b.push_back((char)((v >> 24) & 0xFF));
}
static void wr16(std::string& b, uint16_t v) {
    b.push_back((char)(v & 0xFF)); b.push_back((char)((v >> 8) & 0xFF));
}

// Costruisce un modello: vocab {il,cane,gatto}, bigrammi il->cane(10), il->gatto(5).
static std::string buildSample() {
    std::vector<std::string> vocab = {"il", "cane", "gatto"};
    std::string b;
    b.append(kBigramMagic, 4);
    wr32(b, kBigramVersion);
    wr32(b, 64);   // topK
    wr32(b, 2);    // minCount
    wr32(b, (uint32_t)vocab.size());
    for (auto& w : vocab) { wr16(b, (uint16_t)w.size()); b.append(w); }
    // offsets (V+1): il(0)->[0,2), cane(1)->[2,2), gatto(2)->[2,2), sentinel 2
    uint32_t offs[4] = {0, 2, 2, 2};
    for (uint32_t o : offs) wr32(b, o);
    wr32(b, 2);              // P
    wr32(b, 1); wr32(b, 10); // (cane,10)
    wr32(b, 2); wr32(b, 5);  // (gatto,5)
    return b;
}

int main() {
    const std::string path = "sohw_test_model.bin";
    { std::ofstream o(path, std::ios::binary); std::string s = buildSample(); o.write(s.data(), (std::streamsize)s.size()); }

    BigramModel m;
    assert(m.load(path));
    assert(m.loaded());
    assert(m.vocabSize() == 3);

    assert(m.idOf("il") >= 0);
    assert(m.idOf("assente") < 0);

    assert(m.count("il", "cane") == 10);
    assert(m.count("il", "gatto") == 5);
    assert(m.count("il", "topo") == 0);
    assert(m.count("cane", "il") == 0);
    assert(m.rowTotal("il") == 15);

    auto sc = m.successors("il", 1);
    assert(sc.size() == 1 && sc[0].first == "cane" && sc[0].second == 10);

    BigramPredictor pred(m);
    PredictContext ctx; ctx.leftWords = {"il"};

    // rankCandidates: 'topo' non ha bigramma -> resta in coda; cane>gatto per conteggio.
    auto r = pred.rankCandidates(ctx, {"gatto", "cane", "topo"});
    assert(r.size() == 3);
    assert(r[0].word == "cane" && r[1].word == "gatto" && r[2].word == "topo");
    assert(r[0].score > r[1].score && r[2].score == 0.0f);

    // predictNext: successori di "il".
    auto n = pred.predictNext(ctx, 5);
    assert(n.size() == 2 && n[0].word == "cane" && n[1].word == "gatto");
    assert(n[0].score > n[1].score);

    std::remove(path.c_str());
    std::puts("bigram_tests: OK (hermetico)");

    // --- integrazione col modello reale (se presente) ----------------------
#ifdef SOHW_BIGRAM_BIN
    {
        BigramModel real;
        if (real.load(SOHW_BIGRAM_BIN)) {
            // bigrammi molto frequenti dell'italiano, devono essere sopravvissuti.
            assert(real.count("per", "la") > 0);
            assert(real.count("che", "non") > 0);
            auto s = real.successors("per", 5);
            assert(!s.empty());
            std::printf("bigram_tests: integrazione OK (vocab=%u, per->la=%u)\n",
                        real.vocabSize(), real.count("per", "la"));
        } else {
            std::puts("bigram_tests: integrazione SALTATA (modello reale non caricabile)");
        }
    }
#else
    std::puts("bigram_tests: integrazione non configurata (SOHW_BIGRAM_BIN assente)");
#endif
    return 0;
}
