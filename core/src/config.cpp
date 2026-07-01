#include "onehand/types.hpp"
#include "utf8.hpp"

#include <cstdlib>

namespace onehand {

// ------------------------------------------------------------------ KeyMap
std::vector<wchar_t> KeyMap::groupOf(wchar_t key) const {
    auto it = groups.find(key);
    if (it == groups.end() || it->second.empty())
        return std::vector<wchar_t>(1, key);   // tasto non mappato: se stesso
    return std::vector<wchar_t>(it->second.begin(), it->second.end());
}

KeyMap defaultT9KeyMap() {
    KeyMap km;
    km.groups[L'2'] = L"abc";
    km.groups[L'3'] = L"def";
    km.groups[L'4'] = L"ghi";
    km.groups[L'5'] = L"jkl";
    km.groups[L'6'] = L"mno";
    km.groups[L'7'] = L"pqrs";
    km.groups[L'8'] = L"tuv";
    km.groups[L'9'] = L"wxyz";
    return km;
}

// ------------------------------------------------------------------ parser JSON
// Parser minimale e tollerante (nessuna dipendenza). Estrae il valore (stringa o
// numero) di una chiave.
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

// Estrae il blocco { ... } associato a 'key' (bilanciando le graffe).
static std::string findBlock(const std::string& s, const std::string& key) {
    std::size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    std::size_t b = s.find('{', k);
    if (b == std::string::npos) return "";
    int depth = 0;
    std::size_t i = b;
    for (; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') { if (--depth == 0) { ++i; break; } }
    }
    return s.substr(b, i - b);
}

// Legge le coppie "tasto": "gruppo" da un blocco JSON dentro il keymap.
static void parseGroups(const std::string& blk, KeyMap& km) {
    std::size_t i = 0;
    while (true) {
        std::size_t q1 = blk.find('"', i);           if (q1 == std::string::npos) break;
        std::size_t q2 = blk.find('"', q1 + 1);       if (q2 == std::string::npos) break;
        std::string key = blk.substr(q1 + 1, q2 - q1 - 1);
        std::size_t colon = blk.find(':', q2);        if (colon == std::string::npos) break;
        std::size_t v1 = blk.find('"', colon);        if (v1 == std::string::npos) break;
        std::size_t v2 = blk.find('"', v1 + 1);       if (v2 == std::string::npos) break;
        std::string val = blk.substr(v1 + 1, v2 - v1 - 1);
        std::wstring wkey = utf8ToW(key);
        std::wstring wval = utf8ToW(val);
        if (!wkey.empty()) km.groups[wkey[0]] = wval;
        i = v2 + 1;
    }
}

Config parseConfig(const std::string& s) {
    Config c;  // i default valgono se il testo e' vuoto o privo della chiave

    std::string mc = findVal(s, "max_candidates");
    if (!mc.empty()) c.maxCandidates = atoi(mc.c_str());

    std::string dp = findVal(s, "double_press_ms");
    if (!dp.empty()) c.doublePressMs = atoi(dp.c_str());

    std::string wl = findVal(s, "wordlist");
    if (!wl.empty()) c.wordlistName = utf8ToW(wl);

    // keymap: { "letters": { "2": "abc", ... } }; se assente resta il default T9.
    std::string keymap = findBlock(s, "keymap");
    if (!keymap.empty()) {
        std::string letters = findBlock(keymap, "letters");
        if (!letters.empty()) {
            KeyMap km;
            parseGroups(letters, km);
            if (!km.groups.empty()) c.keymap = km;
        }
    }

    return c;
}

} // namespace onehand
