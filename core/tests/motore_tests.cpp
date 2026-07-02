// Test headless della MILESTONE 1 del MOTORE: tokenizzazione (Strategia A degli
// apostrofi + punteggiatura come token), modello dati, cursori sel/open e render
// model con regole di spaziatura ed evidenziazioni. Nessuna GUI, nessun CORE.
#include "motore/engine.hpp"

#include <cassert>
#include <cstdio>
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
    return 0;
}
