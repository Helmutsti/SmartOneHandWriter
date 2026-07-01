// Dizionario del motore: parole con frequenza + ricerca dei candidati.
// Dettaglio interno del core (non fa parte dell'API pubblica).
#pragma once

#include <istream>
#include <set>
#include <string>
#include <vector>

namespace onehand {

struct Word {
    std::wstring w;
    double       f;
};

class Dictionary {
public:
    // Carica da uno stream gia' aperto (una parola per riga, opzionale
    // "parola<TAB|spazio>frequenza"; righe vuote e con '#' ignorate).
    // 'available' = lettere digitabili; se !wildcardAny, il jolly '?' puo'
    // rappresentare solo le lettere NON disponibili.
    void load(std::istream& in, const std::wstring& available, bool wildcardAny);

    // Candidati per uno scheletro (lettere + '?'), ordinati per frequenza,
    // al massimo maxCand. Senza '?' restituisce {pattern}.
    std::vector<std::wstring> computeCandidates(const std::wstring& pattern, int maxCand) const;

    bool empty() const { return words_.empty(); }

private:
    std::vector<Word>  words_;
    std::set<wchar_t>  wildSet_;
    bool               wildAny_ = false;
};

} // namespace onehand
