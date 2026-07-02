// SmartOneHandWriter - CORE "nuova concezione": generazione dei candidati.
//
// Astrae il primo micro-core (il "matching"). Stabilisce COSA E' POSSIBILE
// (parole strutturalmente compatibili con l'input), lasciando al predittore il
// COSA E' PROBABILE. Due implementazioni dietro la stessa interfaccia:
//   - T9CandidateProvider:      matching attivo (simboli -> parole, len >= n)
//   - LiteralCandidateProvider: matching disattivo (lettere reali -> completamento)
// Il toggle di modalita' del Core sceglie quale usare.
#pragma once

#include "dictionary.hpp"      // onehand::Dictionary (header interno del core)
#include "onehand/types.hpp"   // onehand::KeyMap

#include <string>
#include <vector>

namespace sohw {

struct ICandidateProvider {
    virtual ~ICandidateProvider() = default;

    // 'input' e' la parola codificata (simboli T9) oppure lettere reali, a
    // seconda dell'implementazione. Ritorna parole reali (UTF-8) ordinate per
    // frequenza, al massimo maxCand. Puo' essere vuoto (nessun match).
    virtual std::vector<std::string> candidates(const std::string& input, int maxCand) const = 0;
};

// Matching T9 con completamento (len >= n° simboli). Usa il KeyMap per mappare
// ogni simbolo al suo gruppo di lettere e il Dictionary per i candidati reali.
class T9CandidateProvider final : public ICandidateProvider {
public:
    T9CandidateProvider(const onehand::Dictionary& dict, const onehand::KeyMap& keymap)
        : dict_(dict), keymap_(keymap) {}

    std::vector<std::string> candidates(const std::string& input, int maxCand) const override;

private:
    const onehand::Dictionary& dict_;
    const onehand::KeyMap&     keymap_;
};

// Digitazione classica (matching OFF): l'input sono lettere reali.
//  - completion (default): completa la parola parziale per prefisso dal dizionario;
//  - passthrough:          restituisce la parola digitata cosi' com'e' (nessun match).
class LiteralCandidateProvider final : public ICandidateProvider {
public:
    explicit LiteralCandidateProvider(const onehand::Dictionary& dict, bool completion = true)
        : dict_(dict), completion_(completion) {}

    void setCompletion(bool on) { completion_ = on; }
    bool completion() const { return completion_; }

    std::vector<std::string> candidates(const std::string& input, int maxCand) const override;

private:
    const onehand::Dictionary& dict_;
    bool                       completion_ = true;
};

} // namespace sohw
