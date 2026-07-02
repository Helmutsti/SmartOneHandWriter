#include "dictionary.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <set>

namespace onehand {

// Ripiega le lettere accentate italiane/latine sulla base, per il matching T9
// (i gruppi del keymap contengono solo lettere base: 'citta'/'città' condividono
// lo stesso codice numerico).
static wchar_t foldAccent(wchar_t c) {
    switch (c) {
        case L'à': case L'á': case L'â': case L'ã':
        case L'ä': case L'å': return L'a';
        case L'è': case L'é': case L'ê': case L'ë': return L'e';
        case L'ì': case L'í': case L'î': case L'ï': return L'i';
        case L'ò': case L'ó': case L'ô': case L'õ':
        case L'ö': return L'o';
        case L'ù': case L'ú': case L'û': case L'ü': return L'u';
        case L'ç': return L'c';
        case L'ñ': return L'n';
        default: return c;
    }
}

void Dictionary::load(std::istream& f) {
    words_.clear();

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }

    const long total = static_cast<long>(lines.size());
    long rank = 0;
    for (auto& ln : lines) {
        // separatore: tab oppure spazio
        std::size_t sep = ln.find('\t');
        if (sep == std::string::npos) sep = ln.find(' ');
        std::string ws = (sep == std::string::npos) ? ln : ln.substr(0, sep);
        std::string fs = (sep == std::string::npos) ? "" : ln.substr(sep + 1);

        std::wstring w = utf8ToW(ws);
        for (auto& c : w) c = static_cast<wchar_t>(towlower(c));
        if (w.empty()) { ++rank; continue; }

        double freq;
        if (!fs.empty()) {
            freq = atof(fs.c_str());
            if (freq == 0.0) freq = static_cast<double>(total - rank);
        } else {
            freq = static_cast<double>(total - rank);
        }
        words_.push_back({w, freq});
        ++rank;
    }
    buildIndexes();
}

void Dictionary::buildIndexes() {
    // Indice per prefisso: id ordinati per parola.
    byWord_.resize(words_.size());
    for (uint32_t i = 0; i < words_.size(); ++i) byWord_[i] = i;
    std::sort(byWord_.begin(), byWord_.end(),
              [this](uint32_t a, uint32_t b) { return words_[a].w < words_[b].w; });

    // Trie sulle lettere folded: ogni nodo raccoglie gli id delle parole che vi
    // passano (quindi il nodo a profondita' d contiene tutte le parole con quel
    // prefisso di d lettere e len >= d).
    trie_.clear();
    trie_.push_back(TrieNode{});   // radice
    for (uint32_t id = 0; id < words_.size(); ++id) {
        int cur = 0;
        for (wchar_t raw : words_[id].w) {
            wchar_t f = foldAccent(raw);
            auto it = trie_[cur].ch.find(f);
            int nxt;
            if (it != trie_[cur].ch.end()) {
                nxt = it->second;
            } else {
                nxt = static_cast<int>(trie_.size());
                trie_.push_back(TrieNode{});          // puo' riallocare: da qui reindicizzare
                trie_[cur].ch[f] = nxt;
            }
            trie_[nxt].words.push_back(id);
            cur = nxt;
        }
    }
}

std::vector<std::wstring> Dictionary::computeCandidates(
    const std::vector<std::vector<wchar_t>>& groups, int maxCand) const {
    std::vector<std::wstring> out;
    if (groups.empty()) return out;

    const std::size_t n = groups.size();
    std::vector<std::pair<std::wstring, double>> m;
    for (auto& e : words_) {
        if (e.w.size() != n) continue;
        bool ok = true;
        for (std::size_t i = 0; i < n; ++i) {
            const std::vector<wchar_t>& g = groups[i];
            if (std::find(g.begin(), g.end(), e.w[i]) == g.end()) { ok = false; break; }
        }
        if (ok) m.push_back({e.w, e.f});
    }
    std::sort(m.begin(), m.end(),
              [](const std::pair<std::wstring, double>& a,
                 const std::pair<std::wstring, double>& b) { return a.second > b.second; });

    std::set<std::wstring> seen;
    for (auto& p : m) {
        if (seen.count(p.first)) continue;
        seen.insert(p.first);
        out.push_back(p.first);
        if (static_cast<int>(out.size()) >= maxCand) break;
    }
    return out;
}

// Ordina per frequenza desc, deduplica e limita a maxCand. Helper condiviso.
static std::vector<std::wstring> rankAndCap(
    std::vector<std::pair<std::wstring, double>>& m, int maxCand) {
    std::sort(m.begin(), m.end(),
              [](const std::pair<std::wstring, double>& a,
                 const std::pair<std::wstring, double>& b) { return a.second > b.second; });
    std::vector<std::wstring> out;
    std::set<std::wstring> seen;
    for (auto& p : m) {
        if (seen.count(p.first)) continue;
        seen.insert(p.first);
        out.push_back(p.first);
        if (maxCand > 0 && static_cast<int>(out.size()) >= maxCand) break;
    }
    return out;
}

std::vector<std::wstring> Dictionary::computeCandidatesPrefix(
    const std::vector<std::vector<wchar_t>>& groups, int maxCand) const {
    std::vector<std::wstring> out;
    if (groups.empty() || trie_.empty()) return out;

    const std::size_t n = groups.size();
    // Discesa nel trie: a ogni livello si seguono le lettere del gruppo che
    // esistono come figli. I nodi a profondita' n sono distinti (percorsi diversi)
    // e i loro insiemi di parole sono disgiunti (nessun duplicato).
    std::vector<int> frontier = {0};
    for (std::size_t i = 0; i < n && !frontier.empty(); ++i) {
        std::vector<int> next;
        for (int node : frontier) {
            for (wchar_t letter : groups[i]) {
                auto it = trie_[node].ch.find(letter);
                if (it != trie_[node].ch.end()) next.push_back(it->second);
            }
        }
        frontier.swap(next);
    }

    std::vector<std::pair<std::wstring, double>> m;
    for (int node : frontier)
        for (uint32_t id : trie_[node].words)
            m.push_back({words_[id].w, words_[id].f});
    return rankAndCap(m, maxCand);
}

std::vector<std::wstring> Dictionary::completionsOf(
    const std::wstring& prefix, int maxCand) const {
    std::vector<std::wstring> out;
    const std::size_t n = prefix.size();
    if (n == 0) return out;

    // Ricerca binaria dell'inizio del range di prefisso su byWord_, poi scansione
    // finche' la parola inizia col prefisso.
    auto lo = std::lower_bound(byWord_.begin(), byWord_.end(), prefix,
                               [this](uint32_t id, const std::wstring& p) {
                                   return words_[id].w < p;
                               });
    std::vector<std::pair<std::wstring, double>> m;
    for (auto it = lo; it != byWord_.end(); ++it) {
        const std::wstring& w = words_[*it].w;
        if (w.size() < n || w.compare(0, n, prefix) != 0) break;
        m.push_back({w, words_[*it].f});
    }
    return rankAndCap(m, maxCand);
}

} // namespace onehand
