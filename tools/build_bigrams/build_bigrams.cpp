// build_bigrams - preprocessing offline del file bigrammi italiano.
//
// Input bigrammi: testo in CP1252 (Latin-1 + estensioni), una riga per
//   "conteggio<TAB>parola1<TAB>parola2", ordinato per conteggio desc, con
//   punteggiatura inclusa come token. Puo' essere enorme (~1.2 GB), quindi lo si
//   legge in streaming (anche da stdin, per pipare "7z x -so ...").
//
// Uscita: modello binario compatto (vedi core/src/sohw/bigram_format.hpp):
//   - transcodifica CP1252 -> UTF-8 e minuscolo (per allinearsi al dizionario);
//   - vocabolario dagli unigrammi (wordlist) + un set di punteggiatura;
//   - tiene solo le coppie con entrambe le parole nel vocabolario;
//   - pruning: per ogni w1, top-K successori e scarto conteggio < minCount.
//
// Uso:
//   build_bigrams <wordlist> <bigrams|-> <out.bin> [K=64] [minCount=2]
//   ("-" come input legge da stdin)
#include "sohw/bigram_format.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// --- CP1252 -> code point ---------------------------------------------------
// 0x00-0x7F: ASCII. 0xA0-0xFF: Latin-1 (== code point). 0x80-0x9F: tabella CP1252.
static const uint32_t kCp1252HighToCp[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static inline uint32_t cp1252ToCp(unsigned char b) {
    if (b < 0x80) return b;
    if (b < 0xA0) return kCp1252HighToCp[b - 0x80];
    return b;  // 0xA0-0xFF: Latin-1
}

// Minuscolo su code point: ASCII + Latin-1 (le maiuscole accentate -> minuscole).
static inline uint32_t lowerCp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 32;  // À-Þ (no ×)
    return cp;
}

static inline void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Trasforma un campo CP1252 in UTF-8 minuscolo.
static std::string cp1252FieldToUtf8Lower(const char* p, std::size_t n) {
    std::string out;
    out.reserve(n + 4);
    for (std::size_t i = 0; i < n; ++i)
        appendUtf8(out, lowerCp(cp1252ToCp(static_cast<unsigned char>(p[i]))));
    return out;
}

// --- scrittura little-endian ------------------------------------------------
static void wr32(std::string& b, uint32_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
    b.push_back(static_cast<char>((v >> 16) & 0xFF));
    b.push_back(static_cast<char>((v >> 24) & 0xFF));
}
static void wr16(std::string& b, uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "uso: build_bigrams <wordlist> <bigrams|-> <out.bin> [K=64] [minCount=2]\n");
        return 2;
    }
    const std::string wordlistPath = argv[1];
    const std::string bigramsPath  = argv[2];
    const std::string outPath      = argv[3];
    const uint32_t K        = (argc > 4) ? static_cast<uint32_t>(std::atoi(argv[4])) : 64u;
    const uint32_t minCount = (argc > 5) ? static_cast<uint32_t>(std::atoi(argv[5])) : 2u;

    // --- vocabolario: unigrammi (UTF-8, gia' minuscoli) + punteggiatura -----
    std::unordered_map<std::string, uint32_t> id;
    std::vector<std::string> vocab;   // id -> stringa
    std::vector<uint32_t>    uni;     // id -> frequenza unigramma (0 = punteggiatura)
    auto intern = [&](const std::string& w, uint32_t freq) {
        auto it = id.find(w);
        if (it != id.end()) return;
        id.emplace(w, static_cast<uint32_t>(vocab.size()));
        vocab.push_back(w);
        uni.push_back(freq);
    };

    {
        std::ifstream wf(wordlistPath, std::ios::binary);
        if (!wf) { std::fprintf(stderr, "errore: wordlist non apribile: %s\n", wordlistPath.c_str()); return 1; }
        std::string line;
        while (std::getline(wf, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::size_t sep = line.find('\t');
            if (sep == std::string::npos) sep = line.find(' ');
            std::string w = (sep == std::string::npos) ? line : line.substr(0, sep);
            std::string fs = (sep == std::string::npos) ? "" : line.substr(sep + 1);
            uint32_t freq = fs.empty() ? 1u : static_cast<uint32_t>(std::strtoul(fs.c_str(), nullptr, 10));
            if (freq == 0) freq = 1;
            if (!w.empty()) intern(w, freq);
        }
    }
    // Punteggiatura utile come token di contesto (« » in UTF-8). Unigramma 0.
    for (const char* p : {",", ".", ";", ":", "!", "?", "'", "\"", "(", ")",
                          "\xC2\xAB", "\xC2\xBB", "-"})
        intern(p, 0);

    std::fprintf(stderr, "vocab: %zu token\n", vocab.size());

    // --- stream dei bigrammi ------------------------------------------------
    std::istream* in = &std::cin;
    std::ifstream bf;
    if (bigramsPath != "-") {
        bf.open(bigramsPath, std::ios::binary);
        if (!bf) { std::fprintf(stderr, "errore: bigrammi non apribili: %s\n", bigramsPath.c_str()); return 1; }
        in = &bf;
    } else {
#if defined(_WIN32)
        _setmode(_fileno(stdin), _O_BINARY);   // niente traduzione \r\n
#endif
    }

    // w1_id -> lista di (w2_id, count). Ogni coppia compare una sola volta nel file.
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> rows;

    std::string line;
    std::uint64_t nLines = 0, nKept = 0;
    while (std::getline(*in, line)) {
        ++nLines;
        if ((nLines % 20000000ull) == 0)
            std::fprintf(stderr, "  ...%llu righe, %llu coppie tenute\n",
                         (unsigned long long)nLines, (unsigned long long)nKept);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // count<TAB>w1<TAB>w2
        std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        std::size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;

        uint32_t count = static_cast<uint32_t>(std::strtoul(line.c_str(), nullptr, 10));
        if (count < minCount) continue;   // il file e' ordinato desc: si potrebbe
                                          // uscire prima, ma restiamo robusti.

        std::string w1 = cp1252FieldToUtf8Lower(line.data() + t1 + 1, t2 - t1 - 1);
        std::string w2 = cp1252FieldToUtf8Lower(line.data() + t2 + 1, line.size() - t2 - 1);

        auto i1 = id.find(w1); if (i1 == id.end()) continue;
        auto i2 = id.find(w2); if (i2 == id.end()) continue;

        rows[i1->second].push_back({i2->second, count});
        ++nKept;
    }
    std::fprintf(stderr, "lette %llu righe, %llu coppie in-vocab (pre-pruning)\n",
                 (unsigned long long)nLines, (unsigned long long)nKept);

    // --- pruning per riga: ordina per count desc, tieni top-K --------------
    const uint32_t V = static_cast<uint32_t>(vocab.size());
    std::vector<uint32_t> offsets(V + 1, 0);
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (uint32_t w1 = 0; w1 < V; ++w1) {
        offsets[w1] = static_cast<uint32_t>(pairs.size());
        auto it = rows.find(w1);
        if (it == rows.end()) continue;
        auto& v = it->second;
        std::sort(v.begin(), v.end(),
                  [](const std::pair<uint32_t, uint32_t>& a,
                     const std::pair<uint32_t, uint32_t>& b) { return a.second > b.second; });
        const std::size_t keep = (v.size() < K) ? v.size() : K;
        for (std::size_t i = 0; i < keep; ++i) pairs.push_back(v[i]);
    }
    offsets[V] = static_cast<uint32_t>(pairs.size());
    std::fprintf(stderr, "coppie dopo pruning (K=%u, minCount=%u): %zu\n",
                 K, minCount, pairs.size());

    // --- scrittura binaria --------------------------------------------------
    std::string buf;
    buf.reserve(pairs.size() * 8 + V * 8 + 64);
    buf.append(sohw::kBigramMagic, 4);
    wr32(buf, sohw::kBigramVersion);
    wr32(buf, K);
    wr32(buf, minCount);
    wr32(buf, V);
    for (const auto& w : vocab) {
        wr16(buf, static_cast<uint16_t>(w.size()));
        buf.append(w);
    }
    for (uint32_t i = 0; i < V; ++i) wr32(buf, uni[i]);   // (v2) unigrammi
    for (uint32_t i = 0; i <= V; ++i) wr32(buf, offsets[i]);
    wr32(buf, static_cast<uint32_t>(pairs.size()));
    for (const auto& p : pairs) { wr32(buf, p.first); wr32(buf, p.second); }

    std::ofstream out(outPath, std::ios::binary);
    if (!out) { std::fprintf(stderr, "errore: out non scrivibile: %s\n", outPath.c_str()); return 1; }
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out.close();
    std::fprintf(stderr, "scritto %s (%zu byte)\n", outPath.c_str(), buf.size());
    return 0;
}
