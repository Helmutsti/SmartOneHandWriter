#include "onehand/ngram_predictor.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <numeric>

namespace onehand {

namespace {
std::wstring lower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = static_cast<wchar_t>(towlower(c));
    return r;
}
} // namespace

void NgramPredictor::loadBigrams(std::istream& f) {
    bi_.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        // tokenizza su tab o spazio: w1, w2, [conteggio]
        auto split = [](const std::string& s) {
            std::vector<std::string> out;
            std::size_t i = 0;
            while (i < s.size()) {
                while (i < s.size() && (s[i] == '\t' || s[i] == ' ')) ++i;
                std::size_t j = i;
                while (j < s.size() && s[j] != '\t' && s[j] != ' ') ++j;
                if (j > i) out.push_back(s.substr(i, j - i));
                i = j;
            }
            return out;
        };
        std::vector<std::string> t = split(line);
        if (t.size() < 2) continue;
        std::wstring w1 = lower(utf8ToW(t[0]));
        std::wstring w2 = lower(utf8ToW(t[1]));
        if (w1.empty() || w2.empty()) continue;
        double c = (t.size() >= 3) ? atof(t[2].c_str()) : 1.0;
        if (c <= 0.0) c = 1.0;
        bi_[w1][w2] += c;
    }
}

std::vector<std::wstring> NgramPredictor::rankCandidates(
    const PredictContext& ctx, const std::vector<std::wstring>& candidates) {
    if (bi_.empty() || ctx.leftWords.empty() || candidates.size() <= 1) return candidates;

    auto it = bi_.find(lower(ctx.leftWords.back()));
    if (it == bi_.end()) return candidates;
    const auto& nxt = it->second;

    auto score = [&](const std::wstring& w) -> double {
        auto f = nxt.find(lower(w));
        return (f == nxt.end()) ? 0.0 : f->second;
    };

    // ordinamento stabile per punteggio decrescente: i candidati senza bigramma
    // (punteggio 0) mantengono l'ordine originale (per frequenza).
    std::vector<std::size_t> idx(candidates.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(),
                     [&](std::size_t a, std::size_t b) {
                         return score(candidates[a]) > score(candidates[b]);
                     });

    std::vector<std::wstring> out;
    out.reserve(candidates.size());
    for (std::size_t i : idx) out.push_back(candidates[i]);
    return out;
}

std::vector<std::wstring> NgramPredictor::predictNextWord(const PredictContext& ctx, int maxN) {
    if (bi_.empty() || ctx.leftWords.empty() || maxN <= 0) return {};

    auto it = bi_.find(lower(ctx.leftWords.back()));
    if (it == bi_.end()) return {};

    std::vector<std::pair<std::wstring, double>> v(it->second.begin(), it->second.end());
    std::sort(v.begin(), v.end(),
              [](const std::pair<std::wstring, double>& a,
                 const std::pair<std::wstring, double>& b) { return a.second > b.second; });

    std::vector<std::wstring> out;
    for (auto& p : v) {
        out.push_back(p.first);
        if (static_cast<int>(out.size()) >= maxN) break;
    }
    return out;
}

} // namespace onehand
