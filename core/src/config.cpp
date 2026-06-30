#include "onehand/types.hpp"
#include "utf8.hpp"

#include <cstdlib>

namespace onehand {

// Estrae il valore (stringa o numero) di una chiave dal testo JSON. Parser
// minimale e tollerante (lo stesso del codice originale), senza dipendenze.
static std::string findVal(const std::string& s, const std::string& key) {
    std::size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    std::size_t c = s.find(':', k);
    if (c == std::string::npos) return "";
    std::size_t i = c + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size()) return "";
    if (s[i] == '"') {
        std::size_t e = s.find('"', i + 1);
        if (e == std::string::npos) return "";
        return s.substr(i + 1, e - i - 1);
    }
    std::size_t e = i;
    while (e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != '\n') ++e;
    std::string v = s.substr(i, e - i);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t')) v.pop_back();
    return v;
}

Config parseConfig(const std::string& s) {
    Config c;  // i default valgono se il testo e' vuoto o privo della chiave

    std::string av = findVal(s, "available_keys");
    if (!av.empty()) c.availableKeys = utf8ToW(av);

    std::string wm = findVal(s, "wildcard_matches");
    if (wm == "any") c.wildcardAny = true;

    std::string mc = findVal(s, "max_candidates");
    if (!mc.empty()) c.maxCandidates = atoi(mc.c_str());

    std::string dp = findVal(s, "double_press_ms");
    if (!dp.empty()) c.doublePressMs = atoi(dp.c_str());

    std::string pu = findVal(s, "punctuation");
    if (!pu.empty()) c.punctuation = utf8ToW(pu);

    std::string wl = findVal(s, "wordlist");
    if (!wl.empty()) c.wordlistName = utf8ToW(wl);

    return c;
}

} // namespace onehand
