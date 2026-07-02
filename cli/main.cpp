// sohw_cli - banco di prova da terminale del CORE (senza dipendenze GUI).
//
// Utile finche' la GUI Qt non e' disponibile: espone gli stessi 4 "campi".
// Legge righe da stdin nel formato:
//     left | right | encoded [ | mode ]
//   - left/right: contesto a sinistra/destra della parola (UTF-8, puo' essere vuoto)
//   - encoded:    parola codificata (T9) o lettere reali (Classico)
//   - mode:       "t9" (default) oppure "literal"
// Stampa (3) i match ordinati e (4) il ventaglio next-word del match in cima.
//
// Uso:  sohw_cli <wordlist> [bigrams.bin]
//   echo "per | | 52" | sohw_cli data/wordlist_it.txt data/it.bigrams.bin
#include "sohw/core.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::string trim(std::string s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (true) {
        std::size_t j = s.find(sep, i);
        if (j == std::string::npos) { out.push_back(s.substr(i)); break; }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: sohw_cli <wordlist> [bigrams.bin]\n"
                             "stdin: left | right | encoded [ | mode ]\n");
        return 2;
    }
    sohw::Core core;
    { std::ifstream wl(argv[1], std::ios::binary);
      if (!wl) { std::fprintf(stderr, "wordlist non apribile: %s\n", argv[1]); return 1; }
      core.loadWordlist(wl); }
    if (argc > 2) core.loadBigramModel(argv[2]);

    std::string line;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto f = split(line, '|');
        for (auto& x : f) x = trim(x);
        std::string left    = f.size() > 0 ? f[0] : "";
        std::string right   = f.size() > 1 ? f[1] : "";
        std::string encoded = f.size() > 2 ? f[2] : "";
        std::string mode    = f.size() > 3 ? f[3] : "t9";
        core.setMode(mode == "literal" ? sohw::InputMode::Literal : sohw::InputMode::T9);

        sohw::CoreResult r = core.process(sohw::Context{left, right}, encoded, 8, 6);

        std::printf("== [%s] \"%s%s%s\"  cod=\"%s\" ==\n",
                    mode.c_str(), left.c_str(), left.empty() ? "" : " ",
                    right.empty() ? "_" : ("_ " + right).c_str(), encoded.c_str());
        std::printf("(3) match:\n");
        for (const auto& s : r.matches)
            std::printf("    %-16s %.4f\n", s.word.c_str(), s.score);
        if (!r.matches.empty() && !r.nextByMatch.empty()) {
            std::printf("(4) next dopo \"%s\":\n", r.matches[0].word.c_str());
            for (const auto& s : r.nextByMatch[0])
                std::printf("    %-16s %.4f\n", s.word.c_str(), s.score);
        }
        std::printf("\n");
    }
    return 0;
}
