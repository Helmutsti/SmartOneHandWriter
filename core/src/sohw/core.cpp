#include "sohw/core.hpp"
#include "sohw/candidate_provider.hpp"
#include "sohw/predictor.hpp"
#include "sohw/bigram_model.hpp"
#include "sohw/bigram_predictor.hpp"
#include "dictionary.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cwctype>

namespace sohw {

// Tokenizza una porzione di contesto UTF-8 in parole minuscole (split su spazi).
static std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::wstring w = onehand::utf8ToW(text);
    std::wstring cur;
    auto flush = [&]() {
        if (!cur.empty()) { out.push_back(onehand::wToUtf8(cur)); cur.clear(); }
    };
    for (wchar_t c : w) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') flush();
        else cur.push_back(static_cast<wchar_t>(towlower(c)));
    }
    flush();
    return out;
}

struct Core::Impl {
    onehand::Config     cfg;
    InputMode           mode = InputMode::T9;

    onehand::Dictionary dict;
    BigramModel         model;

    // Provider e predittore tengono riferimenti ai membri sopra (indirizzi stabili:
    // Impl vive sull'heap dietro unique_ptr e non viene mai spostato).
    T9CandidateProvider      t9;
    LiteralCandidateProvider literal;
    BigramPredictor          predictor;

    explicit Impl(const onehand::Config& c)
        : cfg(c), t9(dict, cfg.keymap), literal(dict), predictor(model) {}

    const ICandidateProvider& provider() const {
        return (mode == InputMode::T9)
                   ? static_cast<const ICandidateProvider&>(t9)
                   : static_cast<const ICandidateProvider&>(literal);
    }
};

Core::Core(const onehand::Config& cfg) : impl_(std::make_unique<Impl>(cfg)) {}
Core::~Core() = default;
Core::Core(Core&&) noexcept = default;
Core& Core::operator=(Core&&) noexcept = default;

void Core::setMode(InputMode mode) { impl_->mode = mode; }
InputMode Core::mode() const { return impl_->mode; }

void Core::loadWordlist(std::istream& in) { impl_->dict.load(in); }
void Core::loadBigramModel(const std::string& binPath) { impl_->model.load(binPath); }

CoreResult Core::process(const Context& ctx, const std::string& encoded,
                         int topK, int nextN) const {
    CoreResult res;

    // 1) Contesto -> parole.
    PredictContext pctx;
    pctx.leftWords  = tokenize(ctx.left);
    pctx.rightWords = tokenize(ctx.right);
    pctx.sentenceStart = pctx.leftWords.empty();

    // 2) Candidati (cosa e' possibile) secondo la modalita'.
    std::vector<std::string> cands =
        impl_->provider().candidates(encoded, impl_->cfg.maxCandidates);
    if (cands.empty()) return res;

    // 3) Ranking contestuale (cosa e' probabile) -> match ordinati.
    res.matches = impl_->predictor.rankCandidates(pctx, cands);

    // 4) Per i primi topK match, ventaglio di parole successive col contesto
    //    completo di quello step (match inserito in coda a sinistra).
    res.nextByMatch.resize(res.matches.size());
    const int lim = (topK < 0) ? static_cast<int>(res.matches.size())
                               : std::min<int>(topK, static_cast<int>(res.matches.size()));
    for (int i = 0; i < lim; ++i) {
        PredictContext step = pctx;
        step.leftWords.push_back(res.matches[i].word);
        step.sentenceStart = false;
        res.nextByMatch[i] = impl_->predictor.predictNext(step, nextN);
    }
    return res;
}

} // namespace sohw
