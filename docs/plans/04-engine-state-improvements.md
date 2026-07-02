# Piano 04 — Engine (MOTORE): macchina a stati & miglioramenti

> Il MOTORE è lo strato C++ **condiviso** sopra il CORE (vedi `docs/ARCHITETTURA.md`): macchina a stati
> pura `(stato, azione) → (nuovo stato, Effects)`. Questo piano deriva il modello a stati dai requisiti
> del FE Windows (`docs/plans/03-frontend-windows.md`) ed **estrae miglioramenti** rispetto al legacy
> `core/src/engine.cpp`. Il MOTORE va costruito sopra il **CORE stateless** `sohw::Core` (non sui vecchi
> Dictionary/Predictor diretti).

## 1. Modello dati
- **Document** = lista ordinata di `Word` (nessuno spazio memorizzato; gli spazi sono al render).
- **Word**:
  - `state ∈ {Open, Resolved}`
  - `origin ∈ {Typed, Loaded}`
  - `cls ∈ {Text, Punct}` — parola vera o token di punteggiatura
  - `cells` = tasti/gruppi premuti (solo per parole *Typed* editabili col Roll)
  - `cands` + `idx` = candidati e selezione corrente (parola Open)
  - `text/glyphs` = forma mostrata
- **Due cursori** (novità chiave vs legacy):
  - `sel` = indice della parola **selezionata** (evidenziata) per la navigazione
  - `open` = indice della parola **aperta** in editing, oppure nessuno
  - Invariante: al più **una** parola Open; quando è aperta, di norma `sel == open`.

## 1.1 Tokenizzazione & spaziatura (regole)
- **Token = parola (Text) o punteggiatura (Punct)**, entrambi `Word` distinti. La **navigazione include
  anche la punteggiatura** (C7): le frecce scorrono tutti i token.
- **Apostrofo — Strategia A (elisione)**: l'apostrofo è **word-internal** ma **termina** il token di
  sinistra (split *dopo* l'apostrofo, che resta attaccato). Es. `dell'aria` → `["dell'", "aria"]` (due
  token, non tre). Combacia col dizionario/bigrammi (`dell'`, `l'`, …) e in T9 `dell'` compare come
  completamento del codice `d-e-l-l` (l'apostrofo è oltre le lettere digitate).
- **Spaziatura al render/iniezione** (gli spazi NON sono memorizzati, sono derivati da regole):
  - spazio singolo tra due token Text;
  - **nessuno spazio dopo un token che finisce in apostrofo** (elisione → `dell'aria`);
  - **nessuno spazio prima** della punteggiatura di chiusura (`. , ; : ! ?` `)` `»`) e **nessuno spazio
    dopo** quella di apertura (`(` `«`); (regole standard, da rifinire).

## 2. Azioni (API dell'engine) — atomiche + una combo
| Azione | Effetto |
|---|---|
| `NavigatePrev` / `NavigateNext` | (auto-conferma l'eventuale parola aperta, vedi §3) e sposta `sel` |
| `OpenSelected` | apre `sel` per l'editing (`open=sel`, `state=Open`): abilita digitazione/Roll. Su parola **Loaded**: solo evidenziazione + cancellazione (niente Roll né digitazione, §D11) |
| `Roll` | cicla `cands` della parola aperta (solo Typed) |
| `DeleteLetter` | sulla parola **aperta**: rimuove l'ultima cella; se vuota → rimuove la parola |
| `DeleteWord` | sulla parola **chiusa** selezionata: rimuove l'intera parola |
| `Confirm` | chiude la parola aperta (`state=Resolved`), **senza spostarsi né creare** |
| `Advance` | dopo la conferma: crea+apre una **nuova parola vuota subito a destra** della corrente (`open=nuova`) |
| `ConfirmContinue` | **combo** `Confirm`+`Advance`; automatica a fine frase, o esplicita (§3) |
| `TypeKey(sym)` | se nessuna parola è aperta → apre una nuova parola; poi appende `sym` e ricalcola i candidati via CORE. **Ignorato** se la parola aperta è Loaded (§D11: prima `DeleteWord`) |
| `Punct(sym)` | conferma l'eventuale parola aperta e inserisce un token **Punct**; se `sym` è terminale (`. ! ?`) applica la **Conferma continua** (§3) |
| `Read(text)` | importa testo (già completato) come parole **Resolved/Loaded** nel buffer |
| `Write` | conferma l'aperta, restituisce il testo completo e **svuota** il documento |

Le primitive di cancellazione restano **due** e atomiche (niente "delete intelligente" hardcoded:
è composizione lato FE). Anche `Confirm`/`Advance` sono atomiche; `ConfirmContinue` è la loro combo.

## 3. Regola chiave: Conferma / Avanti / Conferma continua
Modello (dalla lista bottoni aggiornata dall'utente):
- **`Confirm`**: chiude la parola aperta e **resta** sul posto. Nessuna creazione, nessuno spostamento.
- **`Advance`**: presuppone una parola appena confermata; **crea+apre** una nuova parola a destra
  (proseguire in coda). È il "avanti".
- **`ConfirmContinue`** = `Confirm` + `Advance` come **combo**. Scatta **in automatico a fine frase**
  (la frase è delimitata da punteggiatura `. ! ?`), ed è anche un **bottone** esplicito.
- **In mezzo** al testo si usa `Confirm` da solo (chiude e basta): la **selezione resta** sulla parola
  appena confermata (C9), nessuno spostamento. Per proseguire in coda si usa `Advance` o la conferma
  continua automatica; per spostarsi si naviga con le frecce.
- **Navigare via** da una parola aperta la **auto-conferma** (`Confirm`) senza creare.

**Trigger dell'automatismo (deciso: ogni confine di frase).** La conferma continua automatica scatta a
**ogni fine frase**, non solo a fine buffer: quando la parola confermata **completa una frase** —
operativamente, quando è **seguita da punteggiatura terminale** (`. ! ?`) o è a **fine buffer**. In quel
caso `Confirm` diventa `ConfirmContinue` (Confirm+Advance) e apre la parola successiva. Altrove
(parola non a fine frase) `Confirm` chiude soltanto.

## 4. Integrazione col CORE (`sohw::Core`)
Per la parola **aperta**, l'engine costruisce ed interroga il CORE:
- `Context.left`  = testo delle parole risolte a sinistra; `Context.right` = a destra.
- `encoded`       = simboli/gruppi delle `cells` della parola aperta (modo T9) o le lettere (modo Classico).
- `mode`          = assistita → `InputMode::T9`; classica → `InputMode::Literal`.
- Risultato: `matches` → `word.cands` (per il Roll); `nextByMatch` → (futuro) striscia di parole
  successive suggerite.
- **Classica (Literal), regola "letterale primo" (B6)**: il MOTORE mette il **testo digitato** come
  `cands[0]`, poi i completamenti del CORE. `Confirm` risolve al candidato corrente (`idx`): senza Roll
  resta il letterale. (In T9 non si applica: i candidati sono parole del dizionario per frequenza/contesto.)
  Nota: si può in futuro spostare l'opzione "letterale primo" dentro il CORE (`LiteralCandidateProvider`).

## 5. Modello di render (novità per l'overlay)
L'engine espone un **render strutturato** per il box in sovraimpressione (il FE non calcola nulla):
- testo completo del buffer (ASCII), con confini di parola;
- per ogni parola lo **stato di evidenziazione**: `normale | selezionata | aperta`;
- così il FE colora "selezionata" (colore A) e "aperta" (colore B).

È **distinto** dall'**iniezione** su `Write`: il MOTORE rende il buffer in **stringa completa** (regole
§1.1) e il FE la scrive nell'app via **clipboard + `Ctrl+V`** (D12). Overlay = mostra il buffer; Write =
stringa da incollare. Il contratto `Effects`/diff (N backspace + inserimento) resta previsto per una
futura **iniezione incrementale** in-app, non usato dall'attuale `Write` "a blocco".

## 6. Miglioramenti estratti (vs legacy `engine.cpp`)
1. **Selezione vs Apertura separate** (`sel` ≠ `open`): il legacy confondeva navigazione e apertura
   (`OpenPrev/OpenNext`). Serve per l'evidenziazione a due livelli dell'overlay.
2. **Render model** per un overlay che mostra **tutto il buffer** con stati per-parola (il legacy
   emetteva solo diff di iniezione verso app esterne).
3. **`Load`**: ingestione di testo esistente come parole Resolved/Loaded (nuovo).
4. **`Complete`**: dump del testo completo + svuota (nuovo; il legacy `Finalize` era per-frase/pass-through).
5. **Costruito sul CORE nuovo** (`sohw::Core`): contesto a bigrammi interpolato, completamento `len≥n`,
   modo Literal — invece dei vecchi Dictionary/Predictor diretti.
6. **Primitive di cancellazione atomiche** mantenute; fusione "intelligente" lasciata al FE.
7. **Conferma/Avanti/Conferma continua** come azioni distinte + combo auto a fine frase (§3), invece
   di un unico "insert".
8. **Mappa tasto fisico → gruppo** flessibile (tastierino numerico / `q,w,e,a,s,d,z,x,c`), delegata al
   keymap; il modo (assistita/classica) è `Core::setMode`.

## 7. C ABI del MOTORE
Estende il contratto FFI: azioni (`NavigatePrev/Next`, `OpenSelected`, `Roll`, `DeleteLetter`,
`DeleteWord`, `Confirm`, `Advance`, `ConfirmContinue`, `TypeKey`, `Punct`, `Read`, `Write`), lettura del
**render model** (conteggio parole,
testo, stato per-parola, indici `sel`/`open`) e della **stringa completa** restituita da `Write` (che il
FE incolla via clipboard). Da valutare se sopra il legacy `onehand_c.h` (rinumerazione vietata) o un
nuovo `motore_c.h` versionato.

## 8. Questioni aperte (da chiudere in implementazione)
- ~~**§3** trigger conferma continua~~ → **deciso**: a ogni fine frase (parola seguita da `. ! ?` o fine
  buffer).
- ~~Editing di parole Loaded~~ → **deciso (D11)**: `OpenSelected` su Loaded = evidenzia + consente solo
  `DeleteLetter`/`DeleteWord` (Roll no-op). **Non si digita** dentro una Loaded (opzione ii): per
  cambiarla `DeleteWord` + ridigita (diventa Typed, con Roll). Arricchire = inserire parole intorno.
- **Navigazione con parola aperta**: conferma-e-muovi (assunto) vs vietato.
- **Punteggiatura come parole**: se e come il MOTORE gestisce i segni (il CORE li tokenizza già).
- **Capitalizzazione** → **rinviata** (G20): per ora tutto minuscolo; in futuro auto a inizio frase +
  tasto "Maiuscola" manuale (responsabilità del MOTORE al render).

## 9. Milestone (proposta)
1. ✅ **FATTO** — Modello dati + due cursori + render model + tokenizzazione (Strategia A) e regole di
   spaziatura; test headless. Codice in `core/src/motore/` (`types.hpp`, `engine.{hpp,cpp}`), lib
   `motore_core` sopra `sohw_core`, test `motore_tests` (7/7 verdi). `loadResolved` popola il documento;
   `select`/`openSelected`/`closeOpen` sono primitive minime (M2 completerà la semantica).
2. ✅ **FATTO** — Navigazione (`navigatePrev/Next` con auto-conferma), `openSelected` (ricalcolo
   candidati), `roll`, `typeKey` (auto-open, celle, ignora Loaded per D11-ii) + **integrazione
   `sohw::Core`** (contesto left/right dai vicini risolti, `encoded` dalle celle) e regola "letterale
   primo" in classica. Test hermetici + integrazione reale (per+52→"la"; classica "cas" con letterale
   primo). 7/7 verdi.
3. ✅ **FATTO** — `Confirm` (chiude, rimuove se vuota), `Advance`/`ConfirmContinue` (apre nuova a
   destra), `Punct` (token Punct; terminale `. ! ?` → conferma continua automatica), `DeleteLetter`
   (celle o testo; rimuove la parola se vuota), `DeleteWord`. Test hermetici (7/7 verdi).
   Nota: la conferma continua **automatica** scatta sulla punteggiatura terminale; a fine buffer si usa
   Avanti/Conferma continua (lo Spazio della keymap §5.1 del piano 03).
4. `Read` (parse testo → parole Resolved) e `Write` (stringa completa resa; il FE incolla via clipboard).
5. C ABI del MOTORE + driver di test headless.
6. Cablatura col FE Windows (piano 03).

## 10. Puntatori
- FE: `docs/plans/03-frontend-windows.md` · Architettura: `docs/ARCHITETTURA.md` · CORE:
  `docs/CORE-nuova-concezione.md`.
