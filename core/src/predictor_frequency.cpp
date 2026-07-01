// Predittore di default: identita'. Mantiene l'ordine gia' prodotto dal
// dizionario (per frequenza). E' il fallback puro, senza dipendenze.
#include "onehand/predictor.hpp"

namespace onehand {

namespace {
class FrequencyPredictor final : public Predictor {
public:
    std::vector<std::wstring> rankCandidates(
        const PredictContext&, const std::vector<std::wstring>& candidates) override {
        return candidates;
    }
};
} // namespace

std::unique_ptr<Predictor> makeFrequencyPredictor() {
    return std::unique_ptr<Predictor>(new FrequencyPredictor());
}

} // namespace onehand
