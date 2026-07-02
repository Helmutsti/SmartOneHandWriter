// SmartOneHandWriter - predittore a bigrammi (implementazione di IPredictor).
//
// Riordina i candidati per P(candidato | parola precedente) con BACKOFF alla
// frequenza unigramma (l'ordine con cui arrivano i candidati dal provider e' gia'
// per frequenza: i candidati senza bigramma restano in quell'ordine, quelli con
// bigramma salgono per conteggio). predictNext propone i successori della parola
// corrente. Senza modello caricato si comporta come identita' (ordine invariato,
// nessun suggerimento): sempre sicuro da usare.
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

private:
    const BigramModel& model_;
};

} // namespace sohw
