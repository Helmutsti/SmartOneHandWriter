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
  - `cells` = tasti/gruppi premuti (solo per parole *Typed* editabili col Roll)
  - `cands` + `idx` = candidati e selezione corrente (parola Open)
  - `text/glyphs` = forma mostrata
- **Due cursori** (novità chiave vs legacy):
  - `sel` = indice della parola **selezionata** (evidenziata) per la navigazione
  - `open` = indice della parola **aperta** in editing, oppure nessuno
  - Invariante: al più **una** parola Open; quando è aperta, di norma `sel == open`.

## 2. Azioni (API dell'engine) — atomiche
| Azione | Effetto |
|---|---|
| `NavigatePrev` / `NavigateNext` | (auto-conferma l'eventuale parola aperta, vedi §3) e sposta `sel` |
| `OpenSelected` | apre `sel` per l'editing (`open=sel`, `state=Open`) |
| `Roll` | cicla `cands` della parola aperta (solo Typed) |
| `DeleteLetter` | sulla parola **aperta**: rimuove l'ultima cella; se vuota → rimuove la parola |
| `DeleteWord` | sulla parola **chiusa** selezionata: rimuove l'intera parola |
| `Insert` | conferma l'aperta (se c'è) e crea+apre una **nuova parola vuota a destra**, ovunque |
| `TypeKey(sym)` | se nessuna parola è aperta → apre una nuova parola (Insert implicito); poi appende `sym` e ricalcola i candidati via CORE |
| `Load(text)` | importa testo (già completato) come parole **Resolved/Loaded** nel buffer |
| `Complete` | conferma l'aperta, restituisce il testo completo e **svuota** il documento |

Le primitive di cancellazione restano **due** e atomiche (niente "delete intelligente" hardcoded:
è composizione lato FE, se e quando servirà).

## 3. Regola chiave: conferma / inserisci automatico (DA VALIDARE)
Interpretazione della specifica ("chiudere una parola scatena inserisci **solo se è l'ultima della
frase**, altrimenti in automatico"):
- **`Insert`** (azione esplicita): conferma l'aperta e **crea sempre** una nuova parola a destra. È il
  modo per chiudere l'**ultima** parola e continuare a scrivere in coda.
- **Navigare via** da una parola aperta la **auto-conferma** (close) **senza** creare nuove parole →
  questo è il caso "in mezzo": editi una parola interna e ti sposti, la chiusura è automatica.
- Quindi: la creazione automatica di una nuova parola avviene **solo in coda** (ultima), tramite
  `Insert`; in mezzo la chiusura è automatica e non genera parole vuote.

⚠️ Questa è l'interpretazione da **confermare** all'inizio dell'implementazione dell'engine: definisce
il cuore del comportamento. Va anche definito cos'è "ultima della frase" (fine buffer? prima di
punteggiatura di fine frase?).

## 4. Integrazione col CORE (`sohw::Core`)
Per la parola **aperta**, l'engine costruisce ed interroga il CORE:
- `Context.left`  = testo delle parole risolte a sinistra; `Context.right` = a destra.
- `encoded`       = simboli/gruppi delle `cells` della parola aperta (modo T9) o le lettere (modo Classico).
- `mode`          = assistita → `InputMode::T9`; classica → `InputMode::Literal`.
- Risultato: `matches` → `word.cands` (per il Roll); `nextByMatch` → (futuro) striscia di parole
  successive suggerite.

## 5. Modello di render (novità per l'overlay)
L'engine espone un **render strutturato** per il box in sovraimpressione (il FE non calcola nulla):
- testo completo del buffer (ASCII), con confini di parola;
- per ogni parola lo **stato di evidenziazione**: `normale | selezionata | aperta`;
- così il FE colora "selezionata" (colore A) e "aperta" (colore B).

È **distinto** dal contratto `Effects`/diff usato per l'**iniezione** su `Complete` (N backspace +
inserimento verso l'app esterna). Overlay = mostra il buffer; Effects = scrive nell'app.

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
7. **Regola inserisci** raffinata (auto solo in coda, §3).
8. **Mappa tasto fisico → gruppo** flessibile (tastierino numerico / `q,w,e,a,s,d,z,x,c`), delegata al
   keymap; il modo (assistita/classica) è `Core::setMode`.

## 7. C ABI del MOTORE
Estende il contratto FFI: azioni (`NavigatePrev/Next`, `OpenSelected`, `Roll`, `DeleteLetter`,
`DeleteWord`, `Insert`, `TypeKey`, `Load`, `Complete`), lettura del **render model** (conteggio parole,
testo, stato per-parola, indici `sel`/`open`) e degli `Effects` di iniezione su `Complete`. Da valutare
se sopra il legacy `onehand_c.h` (rinumerazione vietata) o un nuovo `motore_c.h` versionato.

## 8. Questioni aperte (da chiudere in implementazione)
- **§3**: semantica esatta conferma/insert e definizione di "ultima della frase". *(priorità 1)*
- **Editing di parole Loaded**: senza `cells` niente Roll; editarle = cancella+ridigita (diventano
  Typed) oppure `DeleteWord`. Confermare il comportamento di `OpenSelected` su parola Loaded.
- **Navigazione con parola aperta**: conferma-e-muovi (assunto) vs vietato.
- **Punteggiatura come parole**: se e come il MOTORE gestisce i segni (il CORE li tokenizza già).
- **Capitalizzazione** (A6 del CORE): inizio frase / nomi propri → responsabilità del MOTORE al render.

## 9. Milestone (proposta)
1. Modello dati + due cursori + render model; test headless (senza GUI).
2. Azioni di navigazione/apertura/roll + integrazione `sohw::Core` per i candidati.
3. `TypeKey` + regola conferma/`Insert` (§3) + cancellazioni; test dei percorsi.
4. `Load` (parse testo → parole Resolved) e `Complete` (testo + `Effects` di iniezione).
5. C ABI del MOTORE + driver di test headless.
6. Cablatura col FE Windows (piano 03).

## 10. Puntatori
- FE: `docs/plans/03-frontend-windows.md` · Architettura: `docs/ARCHITETTURA.md` · CORE:
  `docs/CORE-nuova-concezione.md`.
