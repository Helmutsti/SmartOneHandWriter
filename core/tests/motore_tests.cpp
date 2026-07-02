// Test headless della MILESTONE 1 del MOTORE: tokenizzazione (Strategia A degli
// apostrofi + punteggiatura come token), modello dati, cursori sel/open e render
// model con regole di spaziatura ed evidenziazioni. Nessuna GUI, nessun CORE.
#include "motore/engine.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

using namespace motore;

static Highlight hlAt(const RenderModel& r, int i) { return r.spans[(size_t)i].hl; }

int main() {
    // --- tokenizzazione + Strategia A + spaziatura -------------------------
    {
        Engine e;
        e.loadResolved("dell'aria \xC3\xA8 bella.");   // "dell'aria è bella."
        // token: dell' | aria | è | bella | .
        assert(e.wordCount() == 5);
        assert(e.words()[0].text == "dell'");
        assert(e.words()[1].text == "aria");
        assert(e.words()[3].text == "bella");
        assert(e.words()[4].cls == WordClass::Punct && e.words()[4].text == ".");
        // Render: elisione (niente spazio dopo dell'), niente spazio prima del punto.
        RenderModel r = e.render();
        assert(r.fullText == "dell'aria \xC3\xA8 bella.");
    }

    // --- apostrofo che chiude il token (un'altra) --------------------------
    {
        Engine e;
        e.loadResolved("un'altra");
        assert(e.wordCount() == 2);
        assert(e.words()[0].text == "un'" && e.words()[1].text == "altra");
        assert(e.render().fullText == "un'altra");
    }

    // --- spaziatura punteggiatura: virgola attaccata a sinistra ------------
    {
        Engine e;
        e.loadResolved("ciao , mondo");
        assert(e.wordCount() == 3);
        assert(e.render().fullText == "ciao, mondo");
    }

    // --- parentesi: aperta attaccata a destra, chiusa a sinistra -----------
    {
        Engine e;
        e.loadResolved("( ciao )");
        assert(e.render().fullText == "(ciao)");
    }

    // --- caso preservato sulle parole caricate -----------------------------
    {
        Engine e;
        e.loadResolved("Citt\xC3\xA0");   // "Città"
        assert(e.wordCount() == 1 && e.words()[0].text == "Citt\xC3\xA0");
    }

    // --- cursori ed evidenziazioni -----------------------------------------
    {
        Engine e;
        e.loadResolved("il gatto nero");   // il | gatto | nero
        assert(e.wordCount() == 3);
        // default: selezione = ultima parola, nessuna aperta.
        assert(e.selection() == 2 && e.openIndex() == -1);
        {
            RenderModel r = e.render();
            assert(hlAt(r, 2) == Highlight::Selected);
            assert(hlAt(r, 0) == Highlight::None && hlAt(r, 1) == Highlight::None);
        }
        // seleziono "gatto" e la apro → evidenziazione Open (precede Selected).
        e.select(1);
        e.openSelected();
        assert(e.selection() == 1 && e.openIndex() == 1);
        assert(e.words()[1].state == WordState::Open);
        {
            RenderModel r = e.render();
            assert(hlAt(r, 1) == Highlight::Open);
        }
        // chiudo → torna Resolved, evidenziazione Selected.
        e.closeOpen();
        assert(e.openIndex() == -1 && e.words()[1].state == WordState::Resolved);
        assert(hlAt(e.render(), 1) == Highlight::Selected);
        // clamp della selezione.
        e.select(99);
        assert(e.selection() == 2);
        e.select(-5);
        assert(e.selection() == 0);
    }

    // --- documento vuoto ---------------------------------------------------
    {
        Engine e;
        assert(e.wordCount() == 0 && e.selection() == -1 && e.openIndex() == -1);
        assert(e.render().fullText.empty());
    }

    std::puts("motore_tests: OK (M1 modello dati + render + tokenizzazione)");

    // ================= M2: azioni + integrazione CORE =====================

    // --- hermetico: T9 senza dizionario -> fallback al codice --------------
    {
        Engine e;
        e.setMode(true);                 // assistita, nessuna wordlist caricata
        e.typeKey("5"); e.typeKey("2");  // apre una nuova parola e digita "52"
        assert(e.wordCount() == 1);
        assert(e.openIndex() == 0);
        assert(e.words()[0].origin == WordOrigin::Typed);
        assert(e.words()[0].cands.empty());
        assert(e.words()[0].text == "52");   // nessun candidato -> mostra il codice
    }

    // --- hermetico: non si digita dentro una parola Loaded (D11-ii) --------
    {
        Engine e;
        e.loadResolved("ciao");
        e.select(0); e.openSelected();       // Loaded aperta (nessuna cella)
        e.typeKey("5");                       // deve essere ignorato
        assert(e.words()[0].text == "ciao");
        assert(e.words()[0].cells.empty());
    }
    std::puts("motore_tests: OK (M2 hermetico)");

    // --- integrazione col CORE reale (se i dati sono presenti) -------------
#ifdef SOHW_DATA_DIR
    {
        const std::string dir = SOHW_DATA_DIR;
        std::ifstream wl(dir + "/wordlist_it.txt", std::ios::binary);
        if (wl) {
            Engine e;
            e.loadWordlist(wl);
            e.loadBigramModel(dir + "/it.bigrams.bin");
            e.setMode(true);                 // assistita
            e.loadResolved("per");           // contesto sinistro
            assert(e.selection() == 0);
            e.typeKey("5"); e.typeKey("2");  // codice T9 di "la" (l=5, a=2)
            assert(e.openIndex() == 1);
            const Word& wd = e.words()[1];
            assert(!wd.cands.empty());
            assert(wd.cands[0] == "la");     // il contesto "per" porta "la" in cima
            assert(wd.text == "la");
            assert(e.render().fullText == "per la");
            // Roll cambia candidato.
            assert(wd.cands.size() >= 2);
            e.roll();
            assert(e.words()[1].text == e.words()[1].cands[1]);
            // Navigazione auto-conferma la parola aperta.
            e.navigatePrev();
            assert(e.openIndex() == -1 && e.selection() == 0);

            // Classica: il testo letterale è sempre il primo candidato (B6).
            Engine c;
            std::ifstream wl2(dir + "/wordlist_it.txt", std::ios::binary);
            c.loadWordlist(wl2);
            c.setMode(false);                // classica
            c.typeKey("c"); c.typeKey("a"); c.typeKey("s");
            const Word& cw = c.words()[0];
            assert(cw.cands[0] == "cas");    // letterale primo
            bool hasCasa = false;
            for (const auto& x : cw.cands) if (x == "casa") hasCasa = true;
            assert(hasCasa);
            std::printf("motore_tests: integrazione OK (per+52 -> '%s'; classica 'cas' -> %zu cand)\n",
                        e.words()[1].text.c_str(), cw.cands.size());
        } else {
            std::puts("motore_tests: integrazione SALTATA (wordlist reale assente)");
        }
    }
#endif
    return 0;
}
