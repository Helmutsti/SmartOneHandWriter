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

    // ================= M3: composizione ===================================

    // --- confirm rimuove una parola vuota ----------------------------------
    {
        Engine e; e.loadResolved("ciao");     // sel=0
        e.advance();                           // apre una nuova parola vuota (index 1)
        assert(e.wordCount() == 2 && e.openIndex() == 1 && e.words()[1].text.empty());
        e.confirm();                           // parola vuota -> rimossa
        assert(e.wordCount() == 1 && e.openIndex() == -1 && e.selection() == 0);
    }

    // --- confirmContinue = avanti ------------------------------------------
    {
        Engine e; e.loadResolved("ciao"); e.select(0);
        e.confirmContinue();
        assert(e.openIndex() == 1 && e.wordCount() == 2);
    }

    // --- punct: non terminale attaccato, terminale -> conferma continua ----
    {
        Engine e; e.setMode(false); e.loadResolved("ciao"); e.select(0);
        e.punct(",");                          // virgola dopo "ciao"
        assert(e.wordCount() == 2 && e.words()[1].cls == WordClass::Punct && e.words()[1].text == ",");
        assert(e.openIndex() == -1);
        assert(e.render().fullText == "ciao,");
        e.punct(".");                          // punto terminale -> apre nuova parola
        assert(e.words()[2].text == ".");
        assert(e.wordCount() == 4 && e.openIndex() == 3);   // ciao , . <vuota aperta>
    }

    // --- deleteWord rimuove la parola selezionata --------------------------
    {
        Engine e; e.loadResolved("uno due tre"); e.select(1);   // "due"
        e.deleteWord();
        assert(e.wordCount() == 2 && e.words()[0].text == "uno" && e.words()[1].text == "tre");
        assert(e.selection() == 0);
    }

    // --- deleteLetter: svuotare NON rimuove: resta aperta e vuota (Typed) --
    {
        Engine e; e.setMode(true);
        e.typeKey("5"); e.typeKey("2");        // cells [5,2] -> "52" (nessun dict)
        assert(e.words()[0].text == "52" && e.words()[0].cells.size() == 2);
        e.deleteLetter();                       // cells [5] -> "5"
        assert(e.words()[0].text == "5" && e.words()[0].cells.size() == 1);
        e.deleteLetter();                       // celle vuote -> resta APERTA e VUOTA (non rimossa)
        assert(e.wordCount() == 1 && e.openIndex() == 0);
        assert(e.words()[0].cells.empty() && e.words()[0].text.empty());
        // pronta a ridigitare sul posto
        e.typeKey("3");
        assert(e.wordCount() == 1 && e.openIndex() == 0 && e.words()[0].cells.size() == 1);
        // svuoto ancora, poi il SECONDO Canc rimuove (qui niente parola prima)
        e.deleteLetter();                       // -> slot vuoto aperto
        assert(e.openIndex() == 0 && e.words()[0].text.empty());
        e.deleteLetter();                       // già vuoto -> rimosso
        assert(e.wordCount() == 0 && e.openIndex() == -1);
    }

    // --- deleteLetter: secondo Canc su slot vuoto seleziona la precedente --
    {
        Engine e; e.loadResolved("ciao");        // parola Loaded (idx0), sel=0
        e.typeKey("2"); e.typeKey("2");          // nuova parola Typed a idx1 (2 celle)
        assert(e.wordCount() == 2 && e.openIndex() == 1);
        e.deleteLetter(); e.deleteLetter();      // -> slot vuoto, ancora aperto
        assert(e.wordCount() == 2 && e.openIndex() == 1);
        assert(e.words()[1].cells.empty() && e.words()[1].text.empty());
        e.deleteLetter();                        // già vuoto -> rimuove e seleziona la precedente (chiusa)
        assert(e.wordCount() == 1 && e.openIndex() == -1 && e.selection() == 0);
        assert(e.words()[0].text == "ciao");     // "ciao" intatta, solo selezionata
    }

    // --- deleteLetter su Loaded: si svuota e diventa Typed riscrivibile ----
    {
        Engine e; e.loadResolved("ab"); e.select(0); e.openSelected();  // Loaded aperta
        e.deleteLetter();                        // "a"
        assert(e.words()[0].text == "a");
        e.deleteLetter();                        // "" -> resta aperta e vuota, ora Typed
        assert(e.wordCount() == 1 && e.openIndex() == 0);
        assert(e.words()[0].text.empty() && e.words()[0].cells.empty());
        assert(e.words()[0].origin == WordOrigin::Typed);   // convertita (decisione 2)
        e.typeKey("2");                          // non più ignorata come Loaded: riscrivibile
        assert(e.words()[0].cells.size() == 1);
    }
    std::puts("motore_tests: OK (M3 composizione)");

    // ================= M4: read / write ===================================
    {
        Engine e;
        e.loadResolved("ciao mondo");          // read = loadResolved
        assert(e.currentText() == "ciao mondo");
        std::string t = e.write();              // ritorna il testo e svuota
        assert(t == "ciao mondo");
        assert(e.empty() && e.wordCount() == 0);
    }
    std::puts("motore_tests: OK (M4 read/write)");

    // ================= M5: riga suggerimenti / next-word ==================

    // --- candidati della parola aperta (classica: letterale primo) --------
    {
        Engine e; e.setMode(false);            // classica (Literal), nessun dizionario
        e.typeKey("a"); e.typeKey("b");        // "ab"
        RenderModel r = e.render();
        assert(!r.suggestionsAreNext);
        assert(!r.suggestions.empty() && r.suggestions[0] == "ab");   // letterale primo
        assert(r.suggestionSel == 0);
        e.acceptSuggestion(0);                 // scegliere il candidato = conferma e resta
        assert(e.openIndex() == -1 && e.words()[0].text == "ab");
        assert(e.words()[0].state == WordState::Resolved);
    }

    // --- nessuna parola aperta -> riga in modo next-word -------------------
    {
        Engine e; e.loadResolved("ciao");      // sel=0, nessuna aperta
        assert(e.render().suggestionsAreNext);
    }

    // --- slot vuoto aperto -> riga in modo next-word ----------------------
    {
        Engine e; e.setMode(true);
        e.typeKey("5"); e.deleteLetter();      // slot vuoto aperto
        assert(e.openIndex() == 0 && e.words()[0].text.empty());
        assert(e.render().suggestionsAreNext);
    }
    std::puts("motore_tests: OK (M5 suggerimenti)");

    // ================= disponibilità azioni (attivazione bottoni) =========

    // --- documento vuoto: quasi tutto disabilitato -------------------------
    {
        Engine e;
        Availability a = e.render().actions;
        assert(!a.navPrev && !a.navNext && !a.open && !a.roll && !a.confirm);
        assert(!a.deleteLetter && !a.deleteWord && !a.write && !a.discard);
        assert(a.advance && a.punct && a.read);   // "nuova parola" / punteggiatura / read: sempre
    }

    // --- parola aperta in digitazione (T9 senza dizionario) ----------------
    {
        Engine e; e.setMode(true);
        e.typeKey("5"); e.typeKey("2");            // apre e digita "52" (nessun candidato)
        Availability a = e.render().actions;
        assert(a.confirm && a.deleteLetter && a.write && a.discard);
        assert(!a.roll);                            // 0/1 candidato -> niente da ciclare
        assert(!a.open);                            // la selezione è già la parola aperta
        assert(!a.navPrev && !a.navNext);           // un'unica parola
    }

    // --- documento risolto: navigazione e comandi selezione ----------------
    {
        Engine e; e.loadResolved("uno due tre");   // sel=2 (ultima), nessuna aperta
        {
            Availability a = e.render().actions;
            assert(a.navPrev && !a.navNext);        // in fondo: si va solo indietro
            assert(a.open && a.deleteWord);
            assert(!a.deleteLetter && !a.confirm);  // nessuna parola aperta
            assert(a.write && a.discard);
        }
        e.select(0);                                // prima parola
        {
            Availability a = e.render().actions;
            assert(!a.navPrev && a.navNext);        // in testa: si va solo avanti
        }
    }
    std::puts("motore_tests: OK (disponibilità azioni)");

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

            // next-word dal contesto: "per" -> suggerimenti non vuoti; scegliere inserisce + avanza
            Engine n;
            std::ifstream wl3(dir + "/wordlist_it.txt", std::ios::binary);
            n.loadWordlist(wl3);
            n.loadBigramModel(dir + "/it.bigrams.bin");
            n.setMode(true);
            n.loadResolved("per");           // sel=0, nessuna aperta
            RenderModel rn = n.render();
            assert(rn.suggestionsAreNext && !rn.suggestions.empty());
            const int before = n.wordCount();
            n.roll();                        // evidenzia il primo next-word
            n.confirm();                     // inserisce il next-word evidenziato + apre uno slot
            assert(n.wordCount() >= before + 1);
            std::printf("motore_tests: next-word OK ('per' -> '%s', %zu suggerimenti)\n",
                        rn.suggestions[0].c_str(), rn.suggestions.size());
        } else {
            std::puts("motore_tests: integrazione SALTATA (wordlist reale assente)");
        }
    }
#endif
    return 0;
}
