// SmartOneHandWriter - predittore a bigrammi (implementazione di IPredictor).
//
// Mini-modello linguistico INTERPOLATO sui bigrammi. rankCandidates combina tre
// segnali normalizzati a probabilita':
//   - sinistra:  P(candidato | parola_precedente)      = count(prev, cand)/row(prev)
//   - destra:    P(parola_successiva | candidato)       = count(cand, next)/row(cand)
//   - unigramma: P(candidato)                           = uni(cand)/uniTotale
// score = wL*Psx + wR*Pdx + wU*Puni  (pesi configurabili). Cosi' anche i candidati
// senza bigramma hanno uno score (unigramma) e non piu' 0, e il contesto DESTRO
// contribuisce alla disambiguazione. predictNext propone i successori della parola
// corrente, opzionalmente filtrando i token di punteggiatura.
// Senza modello: rank = ordine del provider (score 0), next vuoto (sicuro).
#pragma once

#include "sohw/predictor.hpp"
#include "sohw/bigram_model.hpp"

namespace sohw {

class BigramPredictor final : public IPredictor {
public:
    explicit BigramPredictor(const BigramModel& model) : model_(model) {}

    std::vector<Suggestion> rankCandidates(
        const PredictContext& ctx, const std::vector<std::string>& candidates) const override;

    std::vector<Suggestion> predictNext(
        const PredictContext& ctx, int maxN) const override;

    // A7: parametri a runtime.
    void setWeights(float left, float right, float unigram) {
        wL_ = left; wR_ = right; wU_ = unigram;
    }
    void setFilterNextPunctuation(bool on) { filterNextPunct_ = on; }
    // Se ON (default), il vicino di contesto (prev/next) e' il token REALE piu'
    // vicino, saltando la punteggiatura (es. "per, ▮" -> prev = "per", non ",").
    void setSkipPunctuationNeighbors(bool on) { skipPunctNb_ = on; }

private:
    std::string leftNeighbor(const PredictContext& ctx) const;
    std::string rightNeighbor(const PredictContext& ctx) const;

    const BigramModel& model_;
    float wL_ = 0.55f, wR_ = 0.25f, wU_ = 0.20f;
    bool  filterNextPunct_ = true;
    bool  skipPunctNb_ = true;
};

} // namespace sohw
