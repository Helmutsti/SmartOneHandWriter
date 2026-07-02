# Piano — CORE del motore di digitazione assistita (nuova concezione)

## Context

Il progetto `SmartOneHandWriter` viene ripensato ("nuova concezione", branch `nuova-concezione`).
Obiettivo di **questa fase**: costruire il **CORE** — il meccanismo che, dati un **contesto** e una
**parola codificata**, produce (a) una lista di parole reali decodificate/ordinate e (b) un ventaglio di
predizioni di parola successiva. Il CORE è **stateless**: riceve il contesto dall'esterno, non mantiene
un documento. La macchina a stati che controlla il testo (il "MOTORE") è rimandata: l'attuale
`engine.cpp` (con `Document`/`Effects`) resta intatto per il futuro e **non** viene toccato ora.

Due micro-core sequenziali dentro il CORE:
1. **Matching** — decodifica la parola codificata in candidati reali compatibili (`len ≥ n° simboli`).
2. **Predittivo** — riordina i candidati per contesto (`P(candidato | parola precedente)`) e, per ogni
   match, propone le parole successive. È il punto chiave concordato: *il match è una predizione
   vincolata*; un unico predittore serve sia la disambiguazione del match sia il next-word.

Il predittivo è **modulare/intercambiabile** (interfaccia `IPredictor`): backend a bigrammi ora,
BERT/ONNX in futuro senza toccare il resto. Il matching è **attivabile/disattivabile**: OFF = digitazione
classica (lettere reali) con completamento, riusando lo stesso predittore.

Stato working tree: **vuoto su disco**; tutti i file sono in HEAD. Primo passo operativo:
`git restore .` per ripristinarli.

## Decisioni prese (con l'utente)

- **Contesto**: modello `left`/`right` + slot implicito (niente indice esplicito). Testo UTF-8.
- **Scope**: façade CORE nuova e stateless riusando i moduli esistenti; `engine.cpp` (MOTORE) rimandato.
- **Backend predittivo**: `IPredictor` + backend bigrammi ora; BERT dopo.
- **Bigrammi**: file `it.word.bigrams` (1.19 GB, **CP1252**, formato `count<TAB>w1<TAB>w2`, ordinato per
  count desc, include punteggiatura) → **preprocessing offline** in binario compatto con pruning.
- **GUI**: Qt/C++ a 4 input, linkata direttamente al C++ del CORE.

## Architettura

```
Context{left,right}  ─┐
parola codificata ────┼─► Core::process(ctx, encoded)
mode (T9 | Literal) ──┘        │
                               ├─ ICandidateProvider::candidates(encoded)   ← struttura (cosa è possibile)
                               │     T9CandidateProvider | LiteralCandidateProvider
                               ├─ IPredictor::rankCandidates(ctx, cands)    ← contesto (cosa è probabile) → (3)
                               └─ per i top-K match: IPredictor::predictNext(ctxConWi, N) → (4)
```

### Nuovi tipi/façade — `core/src/core.{hpp,cpp}` (namespace nuovo, es. `sohw`)
```cpp
struct Context { std::string left; std::string right; };   // UTF-8
enum class InputMode { T9, Literal };
struct Suggestion { std::string word; float score; };
struct CoreResult {
  std::vector<Suggestion> matches;                 // (3) candidati correnti ordinati
  std::vector<std::vector<Suggestion>> nextByMatch;// (4) ventaglio next-word per ciascun match
};
class Core {
public:
  explicit Core(const Config&);
  void setMode(InputMode);
  void loadWordlist(std::istream&);
  void loadBigramModel(const std::string& binPath);
  CoreResult process(const Context&, const std::string& encoded, int topK, int nextN) const;
};
```
`process`: tokenizza `left`/`right` in parole (ultima di `left` = `prev`); `candidates = provider->candidates(encoded)`;
`ranked = predictor->rankCandidates(ctx, candidates)`; per i primi `topK` calcola `predictNext`.

### Candidate provider — `core/src/candidate_provider.hpp` (+ due impl)
```cpp
struct ICandidateProvider { virtual std::vector<std::string> candidates(const std::string& in) const = 0; ... };
```
- **`T9CandidateProvider`**: usa `KeyMap::groupOf` (T9) + dizionario. **Modifica chiave** rispetto a HEAD:
  il `Dictionary` attuale (`dictionary.cpp`, `computeCandidates`) filtra a **lunghezza esatta**; qui serve
  `len ≥ n` con i primi `n` caratteri compatibili col gruppo (completamento). Generalizzare il filtro.
- **`LiteralCandidateProvider`** (matching OFF): input = lettere reali → **completamento per prefisso** dal
  dizionario, ordinato per frequenza unigramma. Sotto-modalità `passthrough` (usa la parola così com'è)
  opzionale; default = completamento.

### Predittore — `core/src/predictor.hpp` (nuovo, UTF-8 + score) e `bigram_predictor.{hpp,cpp}`
Evolvere l'interfaccia esistente `predictor.hpp` (che è `std::wstring`, senza score) in una versione
**UTF-8 con score** per la nuova façade, mantenendo il contratto: `rankCandidates` restituisce solo
permutazioni/sottoinsiemi dei candidati (nessuna parola inventata). Riusare l'**algoritmo** di
`NgramPredictor` (`ngram_predictor.cpp`): `rankCandidates` ordina per `P(cand | prev)` con **backoff a
frequenza unigramma** quando manca il bigramma; `predictNext` = top-N successori di `prev`/della parola
corrente. Backend concreto `BigramPredictor` costruito sul `BigramModel` compatto (sotto).
BERT resterà una futura impl. della stessa interfaccia.

### Modello bigrammi compatto — `core/src/bigram_model.{hpp,cpp}`
Carica (mmap o read) il binario prodotto dal tool: tabella vocabolario `parola↔id`, indice CSR
`w1_id → [(w2_id, count)]` ordinato per count. API: `count(w1,w2)`, `successors(w1, N)`. Decine di MB.

### Tool di preprocessing — `tools/build_bigrams/` (eseguibile CMake standalone)
Input: percorso del testo `it.word.bigrams` estratto + `wordlist_it.txt`. Passi:
1. Costruisce il vocabolario dagli unigrammi (`wordlist_it.txt`, 49k parole) + set di token di
   punteggiatura; assegna id.
2. Stream del file bigrammi riga per riga (`count\tw1\tw2`), **transcodifica CP1252→UTF-8** (tabella
   256 voci, gestione 0x80–0x9F), lowercase per allinearsi al dizionario; tiene solo coppie con
   `w1,w2 ∈ vocab`.
3. **Pruning**: per ogni `w1` mantiene i top-K successori (es. K=64) e scarta `count < soglia` (es. 2).
4. Emette `data/it.bigrams.bin` (magic+versione, tabella stringhe vocab, offsets CSR, coppie
   `(w2_id,count)` little-endian, mmap-friendly).

Estrazione una-tantum dell'archivio (documentata):
`"/c/Program Files/7-Zip/7z.exe" x it.word.bigrams.7z` → `it.word.bigrams`.

### C ABI nuovo e snello — `core/include/sohw/smartcore_c.h` + `core/src/smartcore_c.cpp`
Separato dal legacy `onehand_c.h` (vincolato dagli enum "mai rinumerare" del MOTORE). **UTF-8 `char*`**
in/out (non `wchar_t`), per pulizia cross-linguaggio:
```c
sc_core* sc_create(const char* config_json);
void     sc_set_mode(sc_core*, int mode);          // 0=T9, 1=Literal
int      sc_load_wordlist(sc_core*, const char* path);
int      sc_load_bigrams(sc_core*, const char* bin_path);
sc_result* sc_process(sc_core*, const char* left, const char* right, const char* encoded,
                      int topK, int nextN);
size_t sc_match_count(const sc_result*);
const char* sc_match_word(const sc_result*, size_t i);  float sc_match_score(const sc_result*, size_t i);
size_t sc_next_count(const sc_result*, size_t matchIdx);
const char* sc_next_word(const sc_result*, size_t matchIdx, size_t j);
void sc_free_result(sc_result*);
void sc_destroy(sc_core*);
```
Memoria owned dal core (`sc_free_result`/`sc_destroy`). Build come shared **opt-in**.

### GUI Qt — `gui/qt/` (opt-in, richiede Qt6 Widgets)
- **Input 1 (contesto)**: `QLineEdit` con marcatore dello slot parola (es. `▮`); placeholder esplicativo
  (`es: "il ▮ è bello"`). Split sul marcatore → `left`/`right`.
- **Input 2 (codificata)**: `QLineEdit`.
- **Toggle T9/Classico**: `QCheckBox`/`QComboBox` → `Core::setMode`.
- **Output 3 (decodificate)**: `QPlainTextEdit` read-only, una riga per match `parola  score`.
- **Output 4 (predette)**: `QPlainTextEdit` read-only, ventaglio next-word del match in cima
  (o raggruppato per match).
- Aggiornamento live (debounce) a ogni modifica → `Core::process`. Linka direttamente la lib C++ del CORE.

## File — riuso vs nuovo/modifica

**Riuso quasi as-is** (da HEAD): `core/src/utf8.{hpp,cpp}`; tabella T9 `config.cpp`
(`defaultT9KeyMap`, `KeyMap::groupOf`) + `data/config.json`; `data/wordlist_it.txt` (unigram+freq);
`alterations.{hpp,cpp}` (case/accenti); `parseConfig`; struttura CMake a target multipli.

**Modifica**: `dictionary.{hpp,cpp}` → matching `len ≥ n` + lookup per prefisso (per Literal); riusare
storage parole+frequenza.

**Nuovo**: `core.{hpp,cpp}`, `candidate_provider.hpp` + `t9_provider`/`literal_provider`, `predictor.hpp`
(UTF-8+score) + `bigram_predictor`, `bigram_model`, `smartcore_c.{h,cpp}`, `tools/build_bigrams/`,
`gui/qt/`.

**Intatto (rimandato)**: `engine.{hpp,cpp}`, `types.hpp` (modello documento/azioni), `onehand_c.*`,
`predictor_frequency.cpp`, `ngram_predictor.*` (resta come riferimento/legacy), i frontend Win32/AppKit.

## CMake
Mantenere i 3 target esistenti e aggiungere: lib statica `sohw_core` (nuova façade + provider + predictor
+ bigram_model, linkando i moduli riusati); eseguibile `build_bigrams`; shared `smartcore_c` (opt-in
`-DSOHW_BUILD_C_ABI=ON`); app `gui/qt` (opt-in, `find_package(Qt6 Widgets)`, guardata così che
core/test compilino senza Qt).

## Milestone

1. `git restore .`; scaffolding `sohw_core` + tipi (`Context`, `Suggestion`, `CoreResult`) + `Config`;
   CMake nuovo target compila a vuoto.
2. `Dictionary`: matching `len ≥ n` + lookup prefisso; unit test UTF-8/accenti.
3. `ICandidateProvider` + `T9CandidateProvider` + `LiteralCandidateProvider`; unit test.
4. Tool `build_bigrams` (transcodifica CP1252→UTF-8, vocab, pruning, binario) → generare `it.bigrams.bin`
   dal file reale; verificarne dimensione e lookup.
5. `BigramModel` + `BigramPredictor` (`rankCandidates` con backoff unigram, `predictNext`); unit test.
6. `Core::process` (pipeline completa + toggle modalità) + C ABI `smartcore_c`; unit test end-to-end.
7. GUI Qt a 4 input + toggle; cablatura live al CORE.
8. (Futuro) `BertPredictor` dietro `IPredictor`; MOTORE (macchina a stati) su questo CORE.

## Verifica

- **Unit test (CTest)**: provider T9 (`len ≥ n`, gruppi, accenti), provider Literal (prefisso),
  `BigramModel::count/successors`, `rankCandidates` (bigramma vince, backoff unigram sul resto),
  `Core::process` su contesto noto.
- **Preprocessing**: eseguire `build_bigrams` su un campione sintetico e sul file reale; verificare
  round-trip di `count(w1,w2)` per bigrammi noti (es. `per→la`, `che→non`) e dimensione file contenuta.
- **End-to-end GUI**: modalità T9, contesto `"il ▮ è bello"`, codificata plausibile → i match sono parole
  reali con `len ≥ n`, l'ordine riflette il contesto (bigramma con `il`), il ventaglio next-word è coerente
  con la parola in cima. Poi toggle su Classico: input lettere reali → completamenti per prefisso.
- **C ABI**: mini-driver C che chiama `sc_create/sc_load_*/sc_process/sc_match_*/sc_next_*` e stampa i
  risultati (valida il layer FFI oltre alla GUI).

## Nodi aperti / assunzioni

- Marcatore slot nel contesto GUI: assunto un carattere sentinella (`▮`); se assente si tratta l'intero
  campo come `left` (parola in coda).
- Parametri di pruning bigrammi (K successori, soglia count): valori iniziali K=64 / soglia=2, da tarare
  su qualità vs dimensione.
- `predictNext` per match: l'API restituisce un ventaglio **per ogni** match; la GUI mostra quello del
  match in cima (gli altri restano disponibili via API).
