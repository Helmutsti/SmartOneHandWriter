// SmartOneHandWriter - CORE "nuova concezione": interfaccia di predizione.
//
// Secondo micro-core (il "predittivo"). Un unico predittore serve DUE ruoli
// (l'intuizione "il match e' una predizione vincolata"):
//   - rankCandidates: disambigua/riordina i candidati correnti col contesto;
//   - predictNext:    propone le parole successive dato il contesto completo.
// Contratto: rankCandidates DEVE restituire solo parole presenti in 'candidates'
// (permutazione/sottoinsieme) - il vincolo del matching resta sempre rispettato.
// UTF-8 + score. BERT sara' un'altra implementazione della stessa interfaccia.
#pragma once

#include "sohw/types.hpp"

#include <string>
#include <vector>

namespace sohw {

// Contesto per la predizione: parole gia' risolte a sinistra/destra (piu' recente
// in coda a leftWords), e se siamo a inizio frase.
struct PredictContext {
    std::vector<std::string> leftWords;
    std::vector<std::string> rightWords;
    bool                     sentenceStart = false;
};

struct IPredictor {
    virtual ~IPredictor() = default;

    // Riordina i candidati per plausibilita' contestuale, con score. L'output e'
    // una permutazione/sottoinsieme di 'candidates' (nessuna parola inventata).
    virtual std::vector<Suggestion> rankCandidates(
        const PredictContext& ctx, const std::vector<std::string>& candidates) const = 0;

    // Ventaglio di parole successive (dopo la parola corrente = leftWords.back()).
    // Puo' essere vuoto.
    virtual std::vector<Suggestion> predictNext(
        const PredictContext& ctx, int maxN) const = 0;
};

} // namespace sohw
