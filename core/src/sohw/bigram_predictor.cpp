#include "sohw/bigram_predictor.hpp"

#include <algorithm>

namespace sohw {

// Token di punteggiatura (stesso set interniato da build_bigrams).
static bool isPunctToken(const std::string& w) {
    static const char* const kP[] = {
        ",", ".", ";", ":", "!", "?", "'", "\"", "(", ")",
        "\xC2\xAB", "\xC2\xBB", "-", "\xE2\x80\x99" /* ’ */ };
    for (const char* p : kP) if (w == p) return true;
    return false;
}

// Vicino sinistro: ultimo token; se skipPunctNb_, l'ultima PAROLA reale.
std::string BigramPredictor::leftNeighbor(const PredictContext& ctx) const {
    for (auto it = ctx.leftWords.rbegin(); it != ctx.leftWords.rend(); ++it) {
        if (!skipPunctNb_ || !isPunctToken(*it)) return *it;
    }
    return {};
}
// Vicino destro: primo token; se skipPunctNb_, la prima PAROLA reale.
std::string BigramPredictor::rightNeighbor(const PredictContext& ctx) const {
    for (const auto& w : ctx.rightWords) {
        if (!skipPunctNb_ || !isPunctToken(w)) return w;
    }
    return {};
}

std::vector<Suggestion> BigramPredictor::rankCandidates(
    const PredictContext& ctx, const std::vector<std::string>& candidates) const {
    const std::string prev  = leftNeighbor(ctx);    // vicino di sinistra (parola reale)
    const std::string next  = rightNeighbor(ctx);   // vicino di destra (parola reale)
    const uint64_t    lTot  = prev.empty() ? 0 : model_.rowTotal(prev);
    const uint64_t    uTot  = model_.unigramTotal();

    struct Item { std::size_t idx; float score; };
    std::vector<Item> items;
    items.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::string& c = candidates[i];
        float Psx = (lTot > 0) ? (float)((double)model_.count(prev, c) / (double)lTot) : 0.0f;
        float Pdx = 0.0f;
        if (!next.empty()) {
            uint64_t rTot = model_.rowTotal(c);
            if (rTot > 0) Pdx = (float)((double)model_.count(c, next) / (double)rTot);
        }
        float Puni = (uTot > 0) ? (float)((double)model_.unigramCount(c) / (double)uTot) : 0.0f;
        items.push_back({i, wL_ * Psx + wR_ * Pdx + wU_ * Puni});
    }
    // Ordine per score desc; stable per preservare l'ordine del provider tra pari.
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.score > b.score; });

    std::vector<Suggestion> out;
    out.reserve(items.size());
    for (const auto& it : items) out.push_back({candidates[it.idx], it.score});
    return out;
}

std::vector<Suggestion> BigramPredictor::predictNext(
    const PredictContext& ctx, int maxN) const {
    std::vector<Suggestion> out;
    const std::string prev = leftNeighbor(ctx);
    if (prev.empty()) return out;
    const uint64_t total = model_.rowTotal(prev);
    if (total == 0) return out;

    // Sovra-campiona per poter scartare la punteggiatura e restare a maxN.
    const int fetch = (maxN > 0 && filterNextPunct_) ? maxN * 4 : maxN;
    for (auto& s : model_.successors(prev, fetch)) {
        if (filterNextPunct_ && isPunctToken(s.first)) continue;
        out.push_back({s.first, (float)((double)s.second / (double)total)});
        if (maxN > 0 && (int)out.size() >= maxN) break;
    }
    return out;
}

} // namespace sohw
