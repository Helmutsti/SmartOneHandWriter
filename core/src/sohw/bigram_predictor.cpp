#include "sohw/bigram_predictor.hpp"

#include <algorithm>

namespace sohw {

static std::string prevWord(const PredictContext& ctx) {
    return ctx.leftWords.empty() ? std::string() : ctx.leftWords.back();
}

std::vector<Suggestion> BigramPredictor::rankCandidates(
    const PredictContext& ctx, const std::vector<std::string>& candidates) const {
    const std::string prev = prevWord(ctx);
    const uint64_t total = prev.empty() ? 0 : model_.rowTotal(prev);

    // (indice originale, conteggio bigramma). Stable sort per conteggio desc:
    // preserva l'ordine per frequenza del provider tra pari (inclusi gli zeri).
    struct Item { std::size_t idx; uint32_t cnt; };
    std::vector<Item> items;
    items.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        uint32_t c = (prev.empty() || total == 0) ? 0u : model_.count(prev, candidates[i]);
        items.push_back({i, c});
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.cnt > b.cnt; });

    std::vector<Suggestion> out;
    out.reserve(items.size());
    for (const auto& it : items) {
        float score = (total > 0 && it.cnt > 0)
                          ? static_cast<float>(static_cast<double>(it.cnt) / static_cast<double>(total))
                          : 0.0f;
        out.push_back({candidates[it.idx], score});
    }
    return out;
}

std::vector<Suggestion> BigramPredictor::predictNext(
    const PredictContext& ctx, int maxN) const {
    std::vector<Suggestion> out;
    const std::string prev = prevWord(ctx);
    if (prev.empty()) return out;

    const uint64_t total = model_.rowTotal(prev);
    if (total == 0) return out;
    for (auto& s : model_.successors(prev, maxN)) {
        float score = static_cast<float>(static_cast<double>(s.second) / static_cast<double>(total));
        out.push_back({s.first, score});
    }
    return out;
}

} // namespace sohw
