# SmartOneHandWriter — Riscrittura core in modello T9

## Context

Oggi il core (`core/`, C++17 puro, zero dipendenze OS) è un motore di composizione a
*una parola* basato su **scheletro con wildcard**: `pattern_` = lettere certe + `?`,
`committed_` = parole chiuse con lo spazio incollato dentro, `committedPatterns_` = scheletro
salvato per riaprire solo l'ultima parola. `dictionary.cpp::computeCandidates` fa match a
lunghezza esatta sullo scheletro (`?` = lettere non raggiungibili dalla mano), ordinato per
frequenza su ~49k parole italiane. I frontend (Windows Win32 C++ system-wide via `SendInput`;
macOS Swift via C ABI su editor interno `view.buffer`) applicano solo `EditEffect{backspaces, insert}`.

Il proprietario vuole **ripensare il funzionamento attorno alla singola Parola** e passare a un
input **T9 classico** (tastierino numerico, una pressione per lettera, dizionario che disambigua),
con **keymap configurabile**. Obiettivo: un modello più semplice e generale, dove lo spazio non
esiste come dato ma è derivato, e ogni parola è un oggetto riapribile e correggibile.

### Decisioni prese (finali)
- **T9 classico sostituisce** il modello wildcard one-hand. Niente `?`. Una pressione = una lettera; il dizionario disambigua l'intera parola; il Roll cicla le collisioni.
- **Nessuna wildcard "qualsiasi lettera"**: ogni posizione proviene da un gruppo di tasti definito dal keymap.
- **Keymap configurabile**: `config.json` mappa `tasto → gruppo di lettere` e `tasto → funzione`.
- **Maiuscole/minuscole/accenti** = alterazioni mostrate **dentro** la lista dei suggerimenti, ordinate DOPO le parole reali (non le devono sommergere).
- **"Apri una parola" (accesso casuale)**: supportato **solo nell'editor interno** dell'app. Su app esterne (Windows system-wide) si resta lineari (riapertura da destra).
- **Ranking**: solo frequenza per ora, ma dietro un'interfaccia `Predictor` pronta per il neurale.
- **Rinviati**: punteggiatura/apostrofi, ranking neurale (ONNX/n-gram), accesso casuale system-wide.

## Modello dati nuovo (sostituisce pattern_/committed_/committedPatterns_)

In `core/include/onehand/types.hpp` (additivo; `Effects/EditEffect/PopupEffect/TimerEffect` restano invariati):

```cpp
enum class WordState { Open, Resolved };
enum class WordClass { Text /*, Punct (futuro) */ };

struct Cell {
    int      key;      // tasto premuto (indice nel keymap) — la sua natura è il gruppo di lettere
    wchar_t  glyph;    // lettera scelta/mostrata per questa cella (dopo disambiguazione/roll)
};

struct Word {
    std::vector<Cell>        cells;
    WordState                state = WordState::Open;
    WordClass                cls   = WordClass::Text;
    std::vector<std::wstring> cands;  // candidati (solo mentre Open)
    int                       idx   = 0;
    bool                      capFirst = false; // maiuscola iniziale al render
};

struct Document { std::vector<Word> words; }; // ordinato; NESSUNO spazio memorizzato
```

Stato privato del motore (`engine.hpp`): `Document doc_; int openIndex_=-1; std::wstring lastRender_;
int lastCaret_=0; bool sentenceStart_=true; std::unique_ptr<Predictor> predictor_; KeyMap keymap_;`.

Rimozioni: `pattern_`, `committed_`, `committedPatterns_`, `preview_`, `capNext_`, `trailingSpace_`,
`removedSpace_`, `punctMode_`, `punctIdx_`, `singleLetter_`, tutto il path wildcard/preview
(`previewWildcard`, `Action::Wildcard`, `wildcard_matches`, `available_keys`).

## Keymap configurabile

In `config.cpp` + `config.json`: un keymap che mappa ogni tasto fisico a **un gruppo di lettere**
(es. classico `2→abc 3→def 4→ghi 5→jkl 6→mno 7→pqrs 8→tuv 9→wxyz`) e i tasti-funzione alle azioni
(Conferma, Conferma+spazio, Roll, Apri prev/next, Cancella lettera, Cancella parola). Struttura:

```jsonc
{
  "keymap": {
    "letters": { "2": "abc", "3": "def", "4": "ghi", "5": "jkl",
                 "6": "mno", "7": "pqrs", "8": "tuv", "9": "wxyz" },
    "functions": { "confirm": "...", "confirm_space": "...", "roll": "...",
                   "delete_char": "...", "delete_word": "...",
                   "open_prev": "...", "open_next": "..." }
  }
}
```

Il core espone il gruppo di lettere per tasto; è l'unica cosa che serve a `computeCandidates`.
Un preset di default (numpad classico) va in `data/config.json`.

## Disambiguazione dizionario (generalizza computeCandidates)

`dictionary.cpp::computeCandidates` cambia pochissimo: invece di "il char in posizione i deve essere
`pattern[i]` o ∈ wildcardSet", diventa **"il char in posizione i deve appartenere al gruppo del tasto
premuto in posizione i"**. Il match a lunghezza esatta e l'ordinamento per frequenza restano. Firma:
`computeCandidates(const std::vector<std::vector<wchar_t>>& groups, int maxCand)` (una lista di gruppi,
uno per pressione). Il resto (dedup, `maxCandidates`, ritorno pattern se vuoto) invariato.

## Semantica operazioni (O = openIndex_)

- **Lettera (tasto gruppo):** se nessuna parola aperta, apri in coda; aggiungi `Cell{key}`; `recompute()`; `emit()`.
- **Roll:** `idx = (idx+1) % cands.size()`; riscrive i glifi delle celle da `cands[idx]`; `emit()`.
- **Conferma:** `state=Resolved`; celle congelate; `openIndex_=-1`; `emit()`.
- **Conferma+spazio:** Conferma, aggiungi `Word{Open}` vuota a destra, `openIndex_=last`; `emit()` (compare lo spazio derivato).
- **Apri prev/next/at:** Conferma corrente; `openIndex_=target`, `state=Open`, ricostruisci `cands` dai gruppi delle sue celle (il Roll torna a funzionare per QUALSIASI parola perché ogni Cell ricorda il suo `key`/gruppo); `emit()` (ridigita la coda dal target in poi). **`OpenWordAt` solo in editor interno.**
- **Cancella lettera:** apri l'ultima se serve; rimuovi la cella più a destra; se vuota → Cancella parola; `recompute()`; `emit()`.
- **Cancella parola:** elimina `words[O]`; apri il vicino a SINISTRA (`O-1`), altrimenti `openIndex_=-1`; `emit()`.
- **Finalize (Invio):** Conferma; `emit()`; `passThrough=true`; reset documento; `sentenceStart_=true`.

## Render + diff minimale (cuore tecnico)

`render(doc, openIndex) -> (text, caret)`: concatena i glifi di ogni parola, unisce con
`spaceBetween(left,right)` (Text|Text → `" "`; funzione pura: la punteggiatura futura restituirà `""`
prima di `.,`), applica `capFirst` alla prima cella. `caret` = coda della parola aperta.

`emit(newText)`: `p = longestCommonPrefix(lastRender_, newText); backspaces = lastRender_.size()-p;
insert = newText.substr(p); lastRender_=newText; lastCaret_=newText.size()`. Corretto perché ogni
edit ridigita dal punto di modifica fino alla coda ⇒ il caret esterno coincide sempre con la fine del
campo. Il trim del suffisso comune (meno sfarfallio) richiede di parcheggiare il caret in mezzo ⇒
**disponibile solo nell'editor interno**, mai system-wide.

**Confine caret:** il motore traccia un caret *logico* = fine di `lastRender_`. L'accesso casuale con
caret parcheggiato *dentro* una parola in mezzo è offerto **solo se `ownsEditor==true`** (macOS
`view.buffer`, o futuro editor Windows). System-wide resta tail-only dietro un flag di capacità.

## Alterazioni dentro i suggerimenti (ordinamento)

`Word::cands` costruito come: (1) `base = computeCandidates(groups, maxCand)` — solo parole reali;
(2) `ranked = predictor_->rankCandidates(ctx, base)` — riordina solo le reali; (3) `alts =
alterationsOf(parolaRealeSelezionata)` — varianti maiuscolo/minuscolo + accenti (tabelle `accentsFor`),
**appese dopo** le reali, dedupate; (4) `cands = ranked ++ alts`. Regola deterministica (fissata da
test): le alterazioni seguono la parola reale a indice 0, o quella su cui hai fatto Roll. Scegliere
un'alterazione col Roll congela il glifo nella cella.

## Interfaccia Predictor (default frequenza; neurale poi)

Nuovo header `core/include/onehand/predictor.hpp` (puro, zero dipendenze):

```cpp
struct PredictContext { std::vector<std::wstring> leftWords, rightWords; bool sentenceStart=false; };
class Predictor {
public:
  virtual ~Predictor() = default;
  // DEVE restituire una permutazione/sottoinsieme dei candidati (nessuna parola inventata)
  virtual std::vector<std::wstring> rankCandidates(
      const PredictContext&, const std::vector<std::wstring>& candidates) = 0;
  virtual std::vector<std::wstring> predictNextWord(const PredictContext&, int maxN) { return {}; }
};
std::unique_ptr<Predictor> makeFrequencyPredictor(); // identità/frequenza (default)
```

`Engine::setPredictor(...)`; `recompute()` applica `rankCandidates` dopo `computeCandidates`. Il
default identità mantiene il comportamento a frequenza e i test verdi senza dipendenze. Il neurale
(ONNX/n-gram) sarà un target opt-in separato che implementa la stessa interfaccia — **fuori scope ora**.

## Nuove Action + C ABI (append-only, ABI stabile)

`Action` esistenti invariati; append: `ConfirmNewWord`, `Roll` (già c'è come Rolling), `OpenPrevWord`,
`OpenNextWord`, `OpenWordAt`. `OpenWordAt` richiede un intero ⇒ nuovo entry point
`Effects Engine::onActionIndex(Action, int)` e C `onehand_on_action_index(e, action, index)`.
Introspezione per editor interno (click-to-open + mostrare il caret): `onehand_word_count`,
`onehand_open_index`, `onehand_caret`, `onehand_render_text`. Rimuovere/deprecare
`Action::Wildcard`, `onehand_preview_wildcard`. **Mai rinumerare** i valori enum esistenti.

## Migrazione: tieni vs riscrivi

- **Tieni:** `utf8.*`; `config.cpp` (estendi con keymap); struttura C ABI (getter pull, add-only); tipi `Effects` pubblici invariati.
- **Adatta:** `dictionary.cpp` (generalizza a gruppi; rinomina lo struct interno `onehand::Word` → `onehand::DictEntry` per liberare il nome `Word` al nuovo modello documento).
- **Estrai:** `accentsFor` + upper/lower in `core/src/alterations.hpp/.cpp`.
- **Riscrivi:** `engine.cpp` (Document/openIndex_/render/emit; niente wildcard/punteggiatura; wiring Predictor + KeyMap). `onKey/onTimeout` restano come adattatori sottili (i frontend usano `onAction`).
- Proprietà pura/no-OS-deps preservata.

## File toccati

- `core/include/onehand/types.hpp` — Cell/Word/Document/enums; append Action *(edit)*
- `core/include/onehand/predictor.hpp` — Predictor/PredictContext/makeFrequencyPredictor *(new)*
- `core/include/onehand/engine.hpp` — nuovo stato + metodi (onActionIndex, setPredictor, introspezione, keymap) *(rewrite)*
- `core/src/engine.cpp` — riscrittura attorno a Document/render/emit *(rewrite)*
- `core/src/alterations.hpp/.cpp` — tabelle case/accento *(new)*
- `core/src/predictor_frequency.cpp` — default identità/frequenza *(new)*
- `core/src/dictionary.hpp/.cpp` — gruppi di lettere; rinomina Word→DictEntry *(edit)*
- `core/src/config.cpp` — parsing keymap *(edit)*
- `core/include/onehand/onehand_c.h` + `core/src/onehand_c.cpp` — append action + nuove funzioni *(edit)*
- `core/CMakeLists.txt` — nuovi sorgenti *(edit)*
- `core/tests/engine_tests.cpp` — riscrittura porzioni *(rewrite)*
- `platform/windows/main_win32.cpp` — keymap numpad, slot funzione, gating accesso casuale off *(edit)*
- `platform/macos/main.swift` — costanti action, click-to-open, uso open-index/caret *(edit)*
- `data/config.json` — preset keymap numpad classico *(edit)*

## Testing (end-to-end)

- **Unit** (`engine_tests.cpp`, harness `Field` con backspaces+insert su `wstring` — invariato perché `emit` fa backspace solo dalla coda):
  - `4663` (o gruppi equivalenti) → candidati collidenti ordinati per frequenza; Roll cicla.
  - Conferma vs Conferma+spazio: render con esattamente uno spazio derivato + caret corretto.
  - Apri parola in mezzo (editor interno): cambia una lettera e i gruppi delle celle restano → Roll ancora valido; il diff ridigita solo da lì alla coda.
  - Cancella parola: in mezzo apre la sinistra; sulla prima → `openIndex_==-1`.
  - Cancella lettera fino a svuotamento → si comporta come Cancella parola.
  - Ordinamento alterazioni (reali prima, varianti dopo); Predictor stub riordina, default identità mantiene frequenza.
  - Diff minimale: `backspaces == len(suffisso vecchio)`, `insert == suffisso nuovo`.
- **Manuale**: build CMake (`onehand_core` + test). macOS: digitare via numpad mappato, cliccare una parola per riaprirla, correggere, verificare gli spazi derivati. Windows: digitare in un campo, verificare modalità lineare (accesso casuale disabilitato su app esterne).
- Comando build/test: configurare CMake e lanciare la suite `core/tests` (nessun modello/dipendenza esterna richiesti).

## Rischi

1. **Accesso casuale in editor interno**: richiede che il frontend possieda il buffer e muova il proprio caret (macOS ok; Windows system-wide resta lineare dietro flag `ownsEditor`).
2. **Collisioni T9 numerose**: senza neurale il ranking è solo a frequenza → il Roll può richiedere più cicli. Mitigato dall'interfaccia Predictor pronta.
3. **Collisione nome `Word`**: rinominare lo struct del dizionario (tocca gli include).
4. **Stabilità ABI**: solo append, mai rinumerare; macOS/Win32 hardcodano gli interi → aggiornare in lockstep.
5. **Sfarfallio ridigitazione coda** riaprendo parole in mezzo: limitato; trim del suffisso solo in editor interno.
