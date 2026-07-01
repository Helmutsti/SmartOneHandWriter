// OneHand core - predittore n-gram (bigrammi), puro C++ e zero dipendenze.
//
// Primo re-ranker "contestuale" misurabile: riordina i candidati per
// P(candidato | ultima parola) e propone la parola successiva, usando una
// tabella di bigrammi caricata da file. Senza dati si comporta come l'identita'
// (nessun riordino, nessun suggerimento), quindi e' sempre sicuro da iniettare.
//
// Implementa la stessa interfaccia Predictor del ranking a frequenza: quando
// arrivera' un modello (es. BERT via ONNX), bastera' un'altra implementazione
// della stessa interfaccia, senza toccare il motore.
#pragma once

#include "onehand/predictor.hpp"

#include <istream>
#include <string>
#include <unordered_map>

namespace onehand {

class NgramPredictor final : public Predictor {
public:
    // Carica i bigrammi: una riga per "w1<TAB>w2<TAB>conteggio" (il conteggio e'
    // opzionale, default 1). Separatore tab o spazio. Righe vuote e con '#'
    // ignorate. Le parole sono normalizzate a minuscolo.
    void loadBigrams(std::istream& in);

    bool empty() const { return bi_.empty(); }

    std::vector<std::wstring> rankCandidates(
        const PredictContext& ctx, const std::vector<std::wstring>& candidates) override;
    std::vector<std::wstring> predictNextWord(
        const PredictContext& ctx, int maxN) override;

private:
    // w1 -> { w2 -> conteggio }
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, double>> bi_;
};

} // namespace onehand
