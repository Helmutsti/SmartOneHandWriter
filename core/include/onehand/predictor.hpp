// OneHand core - interfaccia di predizione (ranking dei candidati + parola
// successiva). Puro C++, zero dipendenze: il motore parla solo con questa
// astrazione, cosi' il ranking neurale (n-gram / ONNX) potra' essere agganciato
// piu' avanti come implementazione opt-in senza toccare il motore.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace onehand {

// Contesto per la predizione: parole gia' risolte a sinistra/destra della parola
// aperta (piu' recenti in coda) e se siamo a inizio frase.
struct PredictContext {
    std::vector<std::wstring> leftWords;
    std::vector<std::wstring> rightWords;
    bool                      sentenceStart = false;
};

class Predictor {
public:
    virtual ~Predictor() = default;

    // Riordina i candidati per plausibilita' nel contesto. DEVE restituire una
    // permutazione/sottoinsieme di 'candidates' (nessuna parola inventata), cosi'
    // il vincolo dei tasti resta sempre rispettato per costruzione.
    virtual std::vector<std::wstring> rankCandidates(
        const PredictContext& ctx,
        const std::vector<std::wstring>& candidates) = 0;

    // Suggerimenti di parola successiva (dopo una conferma). Puo' essere vuoto.
    virtual std::vector<std::wstring> predictNextWord(
        const PredictContext& ctx, int maxN) { (void)ctx; (void)maxN; return {}; }
};

// Default: identita' (mantiene l'ordine per frequenza del dizionario). Nessuna
// dipendenza esterna: i test restano verdi senza modelli.
std::unique_ptr<Predictor> makeFrequencyPredictor();

} // namespace onehand
