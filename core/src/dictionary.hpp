// Dizionario del motore: parole con frequenza + ricerca dei candidati T9.
// Dettaglio interno del core (non fa parte dell'API pubblica).
#pragma once

#include <istream>
#include <string>
#include <vector>

namespace onehand {

struct DictEntry {
    std::wstring w;
    double       f;
};

class Dictionary {
public:
    // Carica da uno stream gia' aperto (una parola per riga, opzionale
    // "parola<TAB|spazio>frequenza"; righe vuote e con '#' ignorate).
    void load(std::istream& in);

    // Candidati per una sequenza di GRUPPI di lettere (uno per pressione di
    // tasto). Una parola e' candidata se ha la stessa lunghezza e, in ogni
    // posizione, la sua lettera appartiene al gruppo corrispondente. Ordinati
    // per frequenza, al massimo maxCand. Puo' essere vuoto (nessun match).
    std::vector<std::wstring> computeCandidates(
        const std::vector<std::vector<wchar_t>>& groups, int maxCand) const;

    // Come computeCandidates ma con COMPLETAMENTO: una parola e' candidata se ha
    // lunghezza >= numero di gruppi e i suoi PRIMI n caratteri appartengono ai
    // gruppi corrispondenti (le lettere oltre l'n-esima sono libere). Ordinati per
    // frequenza, al massimo maxCand. E' il matching T9 della "nuova concezione".
    std::vector<std::wstring> computeCandidatesPrefix(
        const std::vector<std::vector<wchar_t>>& groups, int maxCand) const;

    // Completamenti per prefisso (digitazione classica, matching OFF): parole che
    // iniziano con 'prefix' (lunghezza >= |prefix|), ordinate per frequenza, max
    // maxCand. 'prefix' e' confrontato cosi' com'e' (gia' minuscolo se serve).
    std::vector<std::wstring> completionsOf(
        const std::wstring& prefix, int maxCand) const;

    bool empty() const { return words_.empty(); }

private:
    std::vector<DictEntry> words_;
};

} // namespace onehand
