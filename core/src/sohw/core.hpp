// SmartOneHandWriter - CORE "nuova concezione": facade stateless.
//
// Orchestra i due micro-core: matching (ICandidateProvider) e predittivo
// (IPredictor). E' stateless rispetto al documento: il contesto arriva a ogni
// process(). La macchina a stati (il "MOTORE", onehand::Engine) resta separata.
#pragma once

#include "sohw/types.hpp"
#include "onehand/types.hpp"   // riuso di onehand::Config / KeyMap

#include <istream>
#include <memory>
#include <string>

namespace sohw {

class Core {
public:
    explicit Core(const onehand::Config& cfg = onehand::Config{});
    ~Core();
    Core(Core&&) noexcept;
    Core& operator=(Core&&) noexcept;
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    // Toggle matching T9 <-> digitazione classica (a runtime).
    void      setMode(InputMode mode);
    InputMode mode() const;

    // Carica il dizionario (unigrammi con frequenza) da uno stream gia' aperto.
    void loadWordlist(std::istream& in);

    // Carica il modello a bigrammi compatto (binario prodotto da build_bigrams).
    // Senza modello il predittore ripiega su ordinamento per frequenza unigramma.
    void loadBigramModel(const std::string& binPath);

    // Elabora contesto + parola codificata:
    //  1) provider->candidates(encoded)         (cosa e' possibile)
    //  2) predictor->rankCandidates(ctx, cand)  (cosa e' probabile)      -> matches
    //  3) per i primi topK match: predictNext                            -> nextByMatch
    CoreResult process(const Context& ctx, const std::string& encoded,
                       int topK = 8, int nextN = 5) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sohw
