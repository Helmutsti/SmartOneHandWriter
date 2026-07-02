// SmartOneHandWriter - modello bigrammi compatto (lettura del binario CSR).
//
// Carica in memoria il file prodotto da tools/build_bigrams (vedi
// bigram_format.hpp) e offre lookup rapidi: conteggio di una coppia e successori
// di una parola (gia' ordinati per conteggio desc). Zero dipendenze esterne.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sohw {

class BigramModel {
public:
    // Carica dal percorso. Ritorna false se il file manca o e' malformato.
    bool load(const std::string& path);
    bool loaded() const { return !vocab_.empty(); }

    // id del token (-1 se assente).
    int idOf(const std::string& word) const;

    // Conteggio della coppia (w1,w2); 0 se assente (o non nel modello prunato).
    uint32_t count(const std::string& w1, const std::string& w2) const;

    // Somma dei conteggi dei successori memorizzati di w1 (per normalizzare le
    // probabilita' condizionate). 0 se w1 non ha successori.
    uint64_t rowTotal(const std::string& w1) const;

    // Successori di w1 ordinati per conteggio desc, al massimo maxN.
    std::vector<std::pair<std::string, uint32_t>> successors(
        const std::string& w1, int maxN) const;

    // Frequenza unigramma del token (0 se assente / punteggiatura).
    uint32_t unigramCount(const std::string& word) const;
    // Somma di tutte le frequenze unigramma (per normalizzare).
    uint64_t unigramTotal() const { return uniTotal_; }

    uint32_t vocabSize() const { return static_cast<uint32_t>(vocab_.size()); }

private:
    std::vector<std::string>                    vocab_;    // id -> parola
    std::unordered_map<std::string, uint32_t>   id_;       // parola -> id
    std::vector<uint32_t>                       uni_;      // id -> freq unigramma
    uint64_t                                    uniTotal_ = 0;
    std::vector<uint32_t>                       offsets_;  // CSR, size V+1
    std::vector<uint32_t>                       w2_;       // size P
    std::vector<uint32_t>                       cnt_;      // size P
};

} // namespace sohw
