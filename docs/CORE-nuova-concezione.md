# SmartOneHandWriter — CORE (nuova concezione)

> Documento di progettazione **e** di stato, completo e auto-contenuto. Cattura **tutto** ciò che è
> stato deciso, implementato e come funziona, incluse motivazioni ed evoluzione delle idee, così da
> poter riprendere in una **nuova sessione** senza perdere informazioni.
>
> Branch: `nuova-concezione`. Stato: **CORE completo (M1–M7), committato** (`45cd8e3`), 6/6 test verdi,
> GUI Win32 funzionante. §§1–19 = progettazione e razionale; **§20 = funzionamento e stato reale**;
> **§21 = come riprendere + prossimi passi** (leggi prima queste due se stai ricominciando).

---

## 0. Ripresa rapida (TL;DR)

- **Cos'è**: il CORE (namespace `sohw`) è un motore **stateless** che, dato un *contesto* (testo a
  sinistra/destra di uno slot) e una *parola codificata*, restituisce (3) i **match** decodificati e
  ordinati per contesto e (4) un **ventaglio di parole successive** per ciascun match.
- **Aggiornamento**: il **MOTORE** (macchina a stati sopra questo CORE) è ora **implementato** in
  `core/src/motore/` (`motore::Engine`) e pilota il **FE Windows** `sohw_assistant` (`app/windows/`);
  esiste una prima versione avviabile. Stato complessivo in `docs/ARCHITETTURA.md` §5. Questo documento
  resta la fonte di verità del **CORE**; le sezioni sotto che parlano del MOTORE come "rinviato" sono
  storiche. (Il legacy `engine.cpp` con `Document`/`Effects` è un'altra cosa e resta intatto.)
- **Idea centrale**: *il match è una predizione vincolata* → un unico `IPredictor` (a bigrammi) fa sia
  la disambiguazione del match sia il next-word. Il matching (T9 o digitazione classica) è
  intercambiabile e attivabile/disattivabile.
- **Build** (CMake di VS2022 + MSVC; ⚠️ usare un path di build **corto** per via di MAX_PATH):
  ```
  cmake -S <repo> -B C:\shwb
  cmake --build C:\shwb --config Release
  ```
- **Test** (6/6): `ctest --test-dir C:\shwb\core -C Debug` (prima builda i target di test in Debug).
- **Provalo** — GUI: `C:\shwb\gui\win32\Release\sohw_gui_win32.exe` · CLI:
  `echo "per | | 52" | C:\shwb\cli\Release\sohw_cli.exe data\wordlist_it.txt data\it.bigrams.bin`
- **Dato bigrammi**: il binario `data/it.bigrams.bin` è **gitignorato** (rigenerabile, ~20 MB). Se manca,
  rigeneralo (vedi §20 "Rigenerare il modello"): il CORE funziona lo stesso senza (ranking per sola
  frequenza, niente next-word).

---

## 1. Visione e scopo

Costruire un **motore di digitazione assistita**. In questa fase ci si concentra **solo sul CORE**
(il meccanismo), rimandando la macchina a stati che controlla il testo.

Il motore riceve **2 input**:
- un **contesto** contenente la parola codificata (all'inizio, alla fine o al centro);
- una **parola codificata**.

Il CORE decodifica la parola e propone predizioni contestuali. Un applicativo GUI di test mostra il
funzionamento con 4 campi.

---

## 2. Terminologia: CORE vs MOTORE

- **CORE** = il *meccanismo* del motore. È ciò che sviluppiamo ora. Contiene i due micro-core
  (matching + predittivo). È **stateless**: riceve il contesto dall'esterno, non possiede il documento.
- **MOTORE** = la *macchina a stati* che controlla il testo e agisce su di esso. **Rimandato.**
  Nel codice attuale corrisponde a `engine.cpp` (`Document`/`Effects`), che resta intatto per il futuro.

---

## 3. I due input del CORE

1. **Contesto** — rappresentato come **`left` / slot-parola / `right`** (modello scelto):
   due porzioni di testo (UTF-8) a sinistra e a destra della parola da decodificare. Lo slot è
   *implicito* tra le due, quindi non serve un indice esplicito: gestisce nativamente parola a
   inizio/centro/fine frase.
   *(Alternativa scartata: array di token + indice numerico — più verboso su C ABI.)*
2. **Parola codificata** — sequenza di **simboli** che rappresentano gruppi di lettere (stile T9),
   es. `2="abc"`, `3="def"`. Quando il matching è disattivato, questo campo contiene invece
   **lettere reali** (digitazione classica).

---

## 4. I due micro-core e il flusso

### Micro-core 1 — MATCHING
Decodifica la parola codificata in una **lista di parole reali** compatibili, con
**`lunghezza ≥ numero di simboli digitati`** (quindi non solo lunghezza esatta: sono ammessi i
**completamenti**, parole più lunghe di quanto digitato).

### Micro-core 2 — PREDITTIVO
Riceve il **contesto COMPLETO** (contesto + parola già decodificata + posizione). Va interrogato
**tante volte quanti sono i match** trovati dal matching. Ogni interrogazione, dato il contesto
completo di quello step, propone un **ventaglio di predizioni** (parole successive).

### Flusso
```
Context{left,right} ─┐
parola codificata ───┼─► CORE
mode (T9|Literal) ───┘     │
                           ├─ ① MATCHING: decodifica → candidati (len ≥ n)
                           ├─ ② PREDITTIVO.rank: riordina i candidati per contesto  → (3) parole decodificate
                           └─ ② PREDITTIVO.next: per ogni match, ventaglio next-word → (4) parole predette
```

---

## 5. Insight chiave concordato: *il match è una predizione vincolata*

Osservazione emersa in corso di discussione (utente): **anche il match è, in realtà, una predizione
di ciò che si sta scrivendo**, quindi ha molto senso usare il predittore anche per decodificare.

Conclusione architetturale: **non si fondono fisicamente i due micro-core.** Si separano invece
i ruoli e si usa **un unico predittore in due punti**:
- il **matching/provider** stabilisce *cosa è possibile* (candidati strutturalmente compatibili col
  codice T9 o col prefisso);
- il **predittore** stabilisce *cosa è probabile* (`P(candidato | parola precedente)`), sia per
  **disambiguare/riordinare il match corrente** sia per **predire la parola successiva**.

Fondere i due core ucciderebbe la modularità (backend BERT futuro) e la testabilità. Tenendoli
separati con un predittore condiviso si ottiene "il massimo anche in fase di matching" senza
accoppiare trie lessicale e modello linguistico.

Il predittore esistente in HEAD (`predictor.hpp`) ha **già** i due metodi giusti:
`rankCandidates(ctx, candidates)` (disambiguazione, con contratto "nessuna parola inventata") e
`predictNextWord(ctx, N)`.

---

## 6. Requisito aggiuntivo: matching attivabile/disattivabile

Deve essere possibile **attivare/disattivare il matching** per usare il modulo predittivo anche con
**digitazione classica** (lettere reali). Questo *non* significa "niente candidati": significa
**cambiare come si interpreta l'input**. Si generalizza il matcher in `ICandidateProvider`:
- **`T9CandidateProvider`** (matching ON): simboli → parole compatibili `len ≥ n`.
- **`LiteralCandidateProvider`** (matching OFF): lettere reali → **completamento per prefisso** dal
  dizionario (ordinato per frequenza). Sotto-modalità `passthrough` (usa la parola così com'è)
  opzionale; default = completamento.

Il predittore a valle è **identico** nei due casi → il modulo predittivo funziona anche in
digitazione classica. Il toggle sceglie il provider (o lo bypassa), **a runtime**.

---

## 7. Sistema predittivo: modulare e intercambiabile

Domanda iniziale dell'utente: dizionario di frequenze vs BERT — sistema intercambiabile o direzione
unica? **Decisione: interfaccia `IPredictor` + backend a n-gram ora, BERT (ONNX) come backend futuro
dietro la stessa interfaccia.** L'astrazione costa poco e dà testabilità e swappability; sviluppare
solo BERT ora bloccherebbe su tooling prima di validare il CORE.

### Cosa serve come dato contestuale
- **Unigram** (parola→frequenza): ordina i completamenti, **non** usa il contesto.
- **Bigrammi/trigrammi**: abilitano il ruolo contestuale reale (`rank` + `next`).
- **BERT**: contesto bidirezionale profondo (futuro).

### Dati disponibili
- `data/wordlist_it.txt` — **unigrammi con frequenza**, 49.028 parole (OpenSubtitles 2018 /
  hermitdave FrequencyWords), formato `parola<TAB>frequenza`, ordinato desc.
- File bigrammi fornito dall'utente (vedi §8). **Con i bigrammi il ruolo contestuale è reale già
  prima di BERT.**

---

## 8. Il file bigrammi (dettagli tecnici)

- Percorso fornito: `C:\Users\Mcucca.MEPINFORMATICA\Desktop\it.word.bigrams.7z`
- Archivio 7z (LZMA), **221 MiB** compresso → **1.19 GB** scompattato (file `it.word.bigrams`).
- 7-Zip disponibile in `C:\Program Files\7-Zip\7z.exe`. Estrazione una-tantum:
  `"/c/Program Files/7-Zip/7z.exe" x it.word.bigrams.7z`
- **Formato riga**: `frequenza<TAB>parola1<TAB>parola2` (NB: il conteggio è **primo**), ordinato
  per frequenza decrescente. Esempio:
  ```
  3262192	,	che
  2511341	per	la
  2028312	di	un
  ```
- **Encoding: NON UTF-8** → è **CP1252 / Latin-1** (es. "è" appare come byte singolo, non UTF-8).
  Il loader/tool deve **transcodificare CP1252→UTF-8** (tabella 256 voci, con le eccezioni
  0x80–0x9F di CP1252) per allinearsi a `wordlist_it.txt` e alla mappatura T9 accentata.
- Include la **punteggiatura come token** (`,`, `.`), utile e gestibile.

⚠️ **Non caricabile grezzo a ogni avvio** (1.19 GB, mappe pesanti). Strategia scelta: **preprocessing
offline → binario compatto con pruning** (vedi §11).

---

## 9. Stato del repository e mappa di riuso (da HEAD)

Working tree vuoto: **prima operazione `git restore .`**. In HEAD ci sono 26 file. Sintesi del CORE
esistente e riusabilità.

### Riusabile quasi as-is
- `core/src/utf8.{hpp,cpp}` — `utf8ToW()` (UTF-8→wstring, surrogati su Windows). Autonomo.
- Tabella **T9**: `core/src/config.cpp` → `defaultT9KeyMap()` (`2→abc … 9→wxyz`), `KeyMap::groupOf`
  (tasti non mappati → se stessi); override runtime da `data/config.json` (`keymap.letters`),
  parser tollerante `parseConfig()`.
- `data/wordlist_it.txt` — 49k unigrammi+frequenza.
- `core/src/alterations.{hpp,cpp}` — varianti case/accento IT (`alterationsOf`, `capitalizeFirst`).
- `core/include/onehand/predictor.hpp` — interfaccia `Predictor` con i due ruoli
  (`rankCandidates`, `predictNextWord`), contesto `PredictContext{leftWords,rightWords,sentenceStart}`.
  **Nota**: usa `std::wstring` e non restituisce score.
- `core/src/ngram_predictor.cpp` — `NgramPredictor`: mappa `w1→{w2→count}`, re-ranking a bigrammi +
  next-word. **Nota**: carica formato `w1<TAB>w2<TAB>count` (ordine diverso dal file fornito) e usa
  `unordered_map` annidate di `wstring` (pesante) → da sostituire col modello compatto.
- Struttura **CMake** a target multipli (vedi §13).

### Da modificare
- `core/src/dictionary.{hpp,cpp}` — attualmente `computeCandidates` filtra a **lunghezza esatta**;
  serve **`len ≥ n`** (completamento) + **lookup per prefisso** (per il provider Literal). Riusare lo
  storage parole+frequenza.

### Intatto (rimandato — è il MOTORE o legacy)
- `core/src/engine.cpp` + `core/include/onehand/engine.hpp` — macchina a stati con `Document`/`Effects`.
- `core/include/onehand/types.hpp` — `KeyKind`, `Action` (enum ABI 0–11, **mai rinumerare**),
  `Effects`, modello documento.
- `core/include/onehand/onehand_c.h` + `core/src/onehand_c.cpp` — C ABI legacy "pull" (on_key,
  on_action, render_text, edit diffs, nextword chips). Vincolato dagli enum "mai rinumerare".
- `core/src/predictor_frequency.cpp` — predittore identità.
- Frontend `platform/windows/main_win32.cpp` (Win32 puro, linka `onehand_core` direttamente) e
  `platform/macos/main.swift` (AppKit, passa dal C ABI). Espongono 7 azioni funzione (Roll, Canc.
  lettera/parola, Conferma[+spazio], Apri prec./succ.) + cifre/lettere + Invio.

---

## 10. Architettura target del CORE

Namespace nuovo (es. `sohw`). File nuovi in `core/`.

### Tipi e façade — `core/src/core.{hpp,cpp}`
```cpp
struct Context { std::string left; std::string right; };   // UTF-8
enum class InputMode { T9, Literal };
struct Suggestion { std::string word; float score; };
struct CoreResult {
  std::vector<Suggestion> matches;                  // (3) candidati correnti ordinati
  std::vector<std::vector<Suggestion>> nextByMatch; // (4) ventaglio next-word per ciascun match
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
`process()`: tokenizza `left`/`right` (ultima parola di `left` = `prev`); `candidates =
provider->candidates(encoded)`; `ranked = predictor->rankCandidates(ctx, candidates)`; per i primi
`topK` match calcola `predictNext(ctx+Wi, nextN)`.

### Candidate provider — `core/src/candidate_provider.hpp` + impl
```cpp
struct ICandidateProvider { virtual std::vector<std::string> candidates(const std::string&) const = 0; };
```
- `T9CandidateProvider` (usa `KeyMap` + dizionario, `len ≥ n`).
- `LiteralCandidateProvider` (prefisso, completamento; sotto-modalità passthrough).

### Predittore — `core/src/predictor.hpp` (nuovo, UTF-8 + score) + `bigram_predictor.{hpp,cpp}`
Evoluzione dell'interfaccia esistente in versione **UTF-8 con score**, stesso contratto
("rankCandidates restituisce solo permutazioni/sottoinsiemi dei candidati"). Algoritmo riusato da
`NgramPredictor`: `rankCandidates` ordina per `P(cand|prev)` con **backoff a frequenza unigramma**
quando manca il bigramma; `predictNext` = top-N successori. Backend `BigramPredictor` sul modello
compatto. BERT = futura impl. della stessa interfaccia.

### Modello bigrammi compatto — `core/src/bigram_model.{hpp,cpp}`
Carica (mmap o read) il binario del tool. Vocabolario `parola↔id`, indice CSR
`w1_id → [(w2_id,count)]` ordinato. API: `count(w1,w2)`, `successors(w1,N)`. Target: decine di MB.

---

## 11. Tool di preprocessing bigrammi — `tools/build_bigrams/`

Eseguibile standalone (CMake). Input: percorso del testo `it.word.bigrams` estratto + `wordlist_it.txt`.
1. Vocabolario dagli unigrammi (`wordlist_it.txt`) + token di punteggiatura → id.
2. Stream riga per riga (`count\tw1\tw2`), **transcodifica CP1252→UTF-8**, lowercase; tiene solo
   coppie con `w1,w2 ∈ vocab`.
3. **Pruning**: per ogni `w1` top-K successori (iniziale **K=64**) e scarto `count < soglia`
   (iniziale **2**).
4. Emette `data/it.bigrams.bin`: magic+versione, tabella stringhe vocab, offsets CSR, coppie
   `(w2_id,count)` little-endian, mmap-friendly.

Parametri pruning (K, soglia) da tarare qualità vs dimensione.

---

## 12. C ABI nuovo e snello — `core/include/sohw/smartcore_c.h` + `core/src/smartcore_c.cpp`

Separato dal legacy `onehand_c.h` (vincolato dagli enum "mai rinumerare"). **UTF-8 `char*`** in/out.
```c
sc_core* sc_create(const char* config_json);
void     sc_set_mode(sc_core*, int mode);   // 0=T9, 1=Literal
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
Memoria owned dal core. Build shared **opt-in**. (Bozza precedente con `oh_*` sostituita da questa.)

---

## 13. GUI Qt di test — `gui/qt/` (opt-in, Qt6 Widgets)

Quattro campi come richiesto + toggle:
1. **Contesto** — `QLineEdit` con marcatore dello slot parola (es. `▮`), placeholder esplicativo
   (`es: "il ▮ è bello"`). Split sul marcatore → `left`/`right`. (Se il marcatore manca: tutto = `left`.)
2. **Parola codificata** — `QLineEdit`.
3. **Parole decodificate** — `QPlainTextEdit` read-only, una riga per match `parola  score`.
4. **Parole predette** — `QPlainTextEdit` read-only, ventaglio next-word del match in cima.
- **Toggle T9/Classico** → `Core::setMode`.
- Aggiornamento **live** (debounce) a ogni modifica → `Core::process`.
- Linka **direttamente** la lib C++ del CORE (il C ABI resta per i linguaggi terzi).

*(Nota: non esiste GUI Qt in HEAD; i frontend attuali Win32/AppKit sono legati al MOTORE.)*

---

## 14. CMake / target

Mantenere i target esistenti e aggiungere:
- `sohw_core` (lib statica): façade + provider + predictor + bigram_model, linkando i moduli riusati
  (`utf8`, `dictionary`, `config`, `alterations`).
- `build_bigrams` (eseguibile tool).
- `smartcore_c` (shared, opt-in `-DSOHW_BUILD_C_ABI=ON`).
- `gui/qt` (opt-in, `find_package(Qt6 Widgets)`, guardato così che core/test compilino senza Qt).

Esistente da conservare: root C++17 `/utf-8`; `onehand_core` statico; `onehand_c` shared opt-in
(`-DONEHAND_BUILD_C_ABI=ON`); `engine_tests` (CTest); `platform/windows` solo se `WIN32`.

---

## 15. Milestone

1. `git restore .`; scaffolding `sohw_core` + tipi (`Context`, `Suggestion`, `CoreResult`, `Config`);
   nuovo target CMake compila a vuoto.
2. `Dictionary`: matching `len ≥ n` + lookup prefisso; unit test UTF-8/accenti.
3. `ICandidateProvider` + `T9CandidateProvider` + `LiteralCandidateProvider`; unit test.
4. Tool `build_bigrams` (CP1252→UTF-8, vocab, pruning, binario) → generare `it.bigrams.bin` dal file
   reale; verificare dimensione e lookup.
5. `BigramModel` + `BigramPredictor` (`rankCandidates` con backoff unigram, `predictNext`); unit test.
6. `Core::process` (pipeline + toggle) + C ABI `smartcore_c`; unit test end-to-end.
7. GUI Qt a 4 input + toggle; cablatura live.
8. (Futuro) `BertPredictor` dietro `IPredictor`; poi il MOTORE (macchina a stati) sopra questo CORE.

---

## 16. Verifica

- **Unit test (CTest)**: provider T9 (`len ≥ n`, gruppi, accenti); provider Literal (prefisso);
  `BigramModel::count/successors`; `rankCandidates` (bigramma vince, backoff unigram sul resto);
  `Core::process` su contesto noto.
- **Preprocessing**: `build_bigrams` su campione sintetico e sul file reale; round-trip
  `count(w1,w2)` per bigrammi noti (es. `per→la`, `che→non`); dimensione file contenuta.
- **End-to-end GUI**: T9, contesto `"il ▮ è bello"`, codificata plausibile → match = parole reali
  `len ≥ n`, ordine coerente col contesto (bigramma con `il`), ventaglio next-word coerente. Poi
  toggle Classico → completamenti per prefisso.
- **C ABI**: mini-driver C che chiama `sc_create/sc_load_*/sc_process/sc_match_*/sc_next_*`.

---

## 17. Nodi aperti / assunzioni

- Marcatore slot nel contesto GUI: assunto `▮`; se assente, l'intero campo è `left`.
- Parametri pruning bigrammi: K=64 successori / soglia count=2 (da tarare).
- `predictNext` per match: l'API restituisce un ventaglio **per ogni** match; la GUI mostra quello del
  match in cima (gli altri disponibili via API).
- Ranking senza bigramma: backoff esplicito a frequenza unigramma (`wordlist_it.txt`).
- Encoding: il file bigrammi è assunto CP1252; da confermare con qualche riga accentata dopo la
  transcodifica.

---

## 18. Cronologia delle decisioni (come siamo arrivati qui)

1. **Formato contesto** → `left`/`right` + slot implicito (scartato: array+indice).
2. **Ruolo predittore** → l'utente ha chiesto se convenisse *unire* i due micro-core per massimizzare
   anche il matching. Risposta condivisa: **non fondere**, ma usare **un unico predittore in due punti**
   (rank del match corrente + next-word). Il predittore *disambigua* la parola corrente col contesto.
3. **Backend predittivo** → **interfaccia + n-gram ora, BERT dopo**.
4. **Dizionario** → l'utente può fornire **solo unigrammi**; poi ha fornito anche un **file bigrammi**
   (§8), che abilita il contesto reale prima di BERT.
5. **Toggle matching** → nuovo requisito: matching ON/OFF, con OFF = digitazione classica; risolto con
   `ICandidateProvider` (T9 vs Literal) e stesso predittore a valle.
6. **Insight "il match è predizione"** → consolidato l'uso del predittore anche in decodifica.
7. **Scope** → **façade CORE nuova e stateless** riusando i moduli esistenti; `engine.cpp` (MOTORE)
   rimandato.
8. **Bigrammi 1.19 GB / CP1252** → **preprocessing offline in binario compatto + pruning**.
9. **GUI** → **Qt/C++** a 4 input + toggle, linkata direttamente al CORE.

---

## 19. Piano operativo di dettaglio

Il dettaglio operativo è nelle §10–§16 qui sopra. (Il piano `docs/plans/02-core-nuova-concezione.md`,
di pari contenuto, è stato rimosso a completamento: resta nella storia git.)

---

## 20. Funzionamento e stato reale (fonte di verità)

### 20.1 Stato milestone
| # | Milestone | Stato |
|---|-----------|-------|
| M1 | Scaffolding `sohw_core` + tipi + CMake | ✅ testato |
| M2 | `Dictionary` `len ≥ n` + lookup prefisso | ✅ testato |
| M3 | `ICandidateProvider` (T9 + Literal) | ✅ testato |
| M4 | Tool `build_bigrams` → binario compatto | ✅ generato (19.7 MB) |
| M5 | `BigramModel` + `BigramPredictor` | ✅ testato |
| M6 | `Core::process` + C ABI `smartcore_c` | ✅ testato |
| M7 | GUI a 4 input + toggle | ✅ **Win32** compilata/eseguita; **Qt** pronta (non compilata: Qt6 assente) |

Committato in `45cd8e3` sul branch `nuova-concezione` (34 file, +2368 righe). Il resto (MOTORE a stati,
BERT) è rinviato — vedi §21.

### 20.2 Mappa dei file (cosa fa cosa)
```
core/src/sohw/
  types.hpp              Context{left,right}, InputMode{T9,Literal}, Suggestion{word,score}, CoreResult
  core.{hpp,cpp}         Core: facade stateless. Possiede dict, model, i 2 provider e il predittore.
                         process() = pipeline; tokenize() spezza il contesto in parole minuscole.
  candidate_provider.hpp ICandidateProvider + T9CandidateProvider + LiteralCandidateProvider
  candidate_provider.cpp   T9: simbolo->KeyMap.groupOf->Dictionary.computeCandidatesPrefix (len>=n)
                           Literal: Dictionary.completionsOf(prefisso) oppure passthrough
  predictor.hpp          PredictContext{leftWords,rightWords,sentenceStart} + IPredictor (UTF-8+score)
  bigram_model.{hpp,cpp} BigramModel: legge il binario CSR; count(w1,w2), rowTotal(w1), successors(w1,N)
  bigram_predictor.*     BigramPredictor: rankCandidates (P(cand|prev)+backoff freq), predictNext
  bigram_format.hpp      magic "SHWB", versione; layout del binario (condiviso col tool)
  smartcore_c.cpp        implementazione del C ABI (wrapper su Core)
core/include/sohw/
  smartcore_c.h          C ABI pubblico (UTF-8): sc_create/set_mode/load_*/process/match_*/next_*/free
core/src/                (riuso/modifica del core esistente "onehand")
  dictionary.{hpp,cpp}   +computeCandidatesPrefix(len>=n) via TRIE folded, +completionsOf(prefisso)
                         via indice ordinato + lower_bound. Storage unigram+freq. Indici in buildIndexes().
  utf8.{hpp,cpp}         utf8ToW (esistente) + wToUtf8 (nuovo). Conversioni ai bordi.
  config.cpp/types.hpp   KeyMap T9 (defaultT9KeyMap, groupOf), Config, parseConfig (riusati)
tools/build_bigrams/     preprocessing offline: CP1252->UTF-8, vocab, pruning -> data/it.bigrams.bin
cli/                     sohw_cli: banco di prova da terminale (stdin: "left | right | encoded [| mode]")
gui/win32/               sohw_gui_win32: GUI nativa Win32 (4 campi + checkbox), cablata a sohw_core
gui/qt/                  sohw_gui: GUI Qt6 (opt-in, si auto-salta senza Qt6)
docs/CORE-nuova-concezione.md   questo documento
```
Il **MOTORE** legacy resta intatto e NON usato dal CORE: `engine.{hpp,cpp}`, `onehand_c.*`,
`predictor_frequency.cpp`, `ngram_predictor.*`, `platform/windows/main_win32.cpp`, `platform/macos`.

### 20.3 Come funziona una chiamata (`Core::process(ctx, encoded, topK, nextN)`)
1. **Tokenizza** `ctx.left`/`ctx.right` in token minuscoli (parole + punteggiatura separata). Il vicino
   di contesto `prev`/`next` è il **token reale più vicino** (salta la punteggiatura di default,
   `Core::setSkipPunctuationInContext`); `sentenceStart` = `left` vuoto.
2. **Candidati** dal provider secondo la modalità (`cfg.maxCandidates`, default 8):
   - **T9**: ogni simbolo → gruppo di lettere (`KeyMap::groupOf`), poi `Dictionary::computeCandidatesPrefix`
     restituisce le parole reali con `len ≥ n` i cui primi `n` caratteri stanno nei gruppi.
   - **Literal**: `Dictionary::completionsOf(prefisso)` (parole che iniziano col testo digitato), oppure,
     in passthrough, la parola così com'è.
3. **Ranking contestuale INTERPOLATO** (`BigramPredictor::rankCandidates`): per ogni candidato
   `score = wL·P(cand|prev) + wR·P(next|cand) + wU·P(cand)`, dove `prev` è il vicino di sinistra,
   `next` il vicino di destra, e `P(cand)` l'unigramma. Pesi di default `wL=0.55, wR=0.25, wU=0.20`
   (configurabili con `Core::setRankingWeights`). Usa quindi il contesto **bidirezionale** + la
   frequenza; anche i candidati senza bigramma hanno uno score (unigramma), non più 0. `stable_sort`
   desc (ordine del provider preservato tra pari). → `matches` (3).
4. **Next-word**: per i primi `topK` match, si crea un contesto con il match in coda a `leftWords` e si
   chiama `predictNext` → i `successors(match, nextN)` del modello, con score = `count/rowTotal`.
   Di default **filtra la punteggiatura** (`, . : …`) dai suggerimenti (`Core::setNextWordPunctuationFilter`).
   → `nextByMatch` (4), un vettore per ciascun match (la GUI mostra quello del match in cima).
5. **Senza modello caricato** (o `prev`/`next` sconosciuti): rank = solo unigramma (score piccolo ma non
   nullo) → in pratica ordine per frequenza; next vuoto. Il CORE resta usabile con la sola wordlist.

### 20.4 Formato del binario bigrammi (`data/it.bigrams.bin`)
CSR little-endian (dettaglio in `core/src/sohw/bigram_format.hpp`, **versione 2**): magic `SHWB`,
versione, `topK`, `minCount`, `V`; tabella vocabolario (`uint16 len` + bytes UTF-8, id = indice);
**`unigram[V]`** (frequenze dalla wordlist, 0 per la punteggiatura — v2); `offsets[V+1]`; coppie
`(uint32 w2_id, uint32 count)` — ogni riga ordinata per conteggio desc. Consuntivo generazione reale:
66M righe → 13.2M coppie in-vocab → **2.38M dopo pruning** (K=64, minCount=2) → **~19.9 MB** in ~21 s.
NB: i file v1 non sono più compatibili (rigenerare).

### 20.5 Build / test / run (comandi esatti)
- CMake è quello di VS2022: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` (idem `ctest.exe`). NON sono nel PATH.
- ⚠️ **Path di build corto** (`C:\shwb`): MSBuild FileTracker supera MAX_PATH con path lunghi (es. scratchpad) → errore `FTK1011`.
- Configura: `cmake -S <repo> -B C:\shwb` (opzioni: `-DSOHW_BUILD_C_ABI=ON` per la DLL C ABI).
- Build tutto: `cmake --build C:\shwb --config Release` (o `--target sohw_gui_win32`, `sohw_cli`, ecc.).
- Test: builda in Debug e `ctest --test-dir C:\shwb\core -C Debug` → **6/6**: `engine_tests` (legacy),
  `sohw_tests` (pipeline + integrazione reale), `dict_tests`, `provider_tests`, `bigram_tests`
  (hermetico + reale), `abi_tests` (C ABI).
- GUI: `C:\shwb\gui\win32\Release\sohw_gui_win32.exe` (dati letti da `SOHW_DATA_DIR` = `<repo>/data`).
- CLI: `echo "per | | 52" | C:\shwb\cli\Release\sohw_cli.exe data\wordlist_it.txt data\it.bigrams.bin`.

### 20.6 Rigenerare il modello bigrammi
Serve l'archivio `it.word.bigrams.7z` (fuori repo, es. sul Desktop) e 7-Zip
(`C:\Program Files\7-Zip\7z.exe`). Da **bash** (pipe binary-safe, niente file da 1.2 GB su disco):
```
"/c/Program Files/7-Zip/7z.exe" x -so /c/.../it.word.bigrams.7z \
  | /c/shwb/tools/build_bigrams/Release/build_bigrams.exe data/wordlist_it.txt - data/it.bigrams.bin
```
Argomenti tool: `build_bigrams <wordlist> <bigrams|-> <out.bin> [K=64] [minCount=2]`.

### 20.7 Verifica dal vivo (osservata)
- `per | | 52` → **la** (score 0.2213); next-word: *sua/prima/mia/loro/commissione/vita* (contesto applicato).
- `| | citt | literal` → **città, citta, cittadini, cittadino…** (accenti UTF-8 corretti).
- `| | 7653` (T9 di "sole") → *soldi, sole, soldato…*
- GUI Win32: schermata verificata (i 4 campi mostrano gli stessi risultati).

### 20.8 Miglioramenti gruppo A (fatti) e tuning residuo
Il **gruppo A** è stato sistemato:
- **A1** filtro punteggiatura nel next-word (default ON, `Core::setNextWordPunctuationFilter`).
- **A2** tokenizzatore del contesto sensibile alla punteggiatura (segni = token propri; apostrofo attaccato).
- **A3** ranking che usa il contesto **destro** oltre al sinistro (bigramma bidirezionale).
- **A4** ranking **interpolato** bigramma+unigramma (formato v2 con `unigram[V]`): niente più score 0.
- **A5** accent-folding nel matching T9 (`città` matcha il codice di `c-i-t-t-a`), in `dictionary.cpp`.
- **A7** parametri a runtime: `setRankingWeights`, `setNextWordPunctuationFilter`, `setLiteralCompletion`,
  `setSkipPunctuationInContext`.
- **A6 (decisione di confine)**: le **maiuscole** NON sono gestite dal CORE. Il CORE restituisce forme
  canoniche minuscole; la capitalizzazione (inizio frase, nomi propri) è responsabilità del MOTORE/
  frontend che conosce la posizione nel testo. Si può aggiungere un helper opzionale in futuro.

Tuning residuo: pesi interpolazione (0.55/0.25/0.20) e pruning bigrammi (K=64, minCount=2) tarabili;
`config.json` non espone ancora questi parametri (solo via API `Core::set*`).

### 20.9 Blocchi ad alta priorità risolti (prestazioni + contesto)
- **#1 Prestazioni** — il `Dictionary` non fa più scansioni O(N) per query: **trie** sulle lettere
  folded per il T9 (`computeCandidatesPrefix`) e **indice ordinato + `lower_bound`** per il
  completamento (`completionsOf`); indici costruiti in `buildIndexes()` a fine `load()`. Benchmark:
  **~0.48 ms/query** end-to-end (incluso I/O e stampa) su 20k query, dizionario reale 49k + modello v2.
- **#2/#3 Vicino di contesto** — `prev`/`next` sono ora il **token reale più vicino**, saltando la
  punteggiatura (default ON, `Core::setSkipPunctuationInContext`). Con i bigrammi il contesto utile
  resta l'immediato: contesto più profondo (multi-parola) richiede trigrammi/BERT (rinviato).

---

## 21. Come riprendere in una nuova sessione + prossimi passi

### 21.1 Ripresa a freddo (checklist)
1. Sei sul branch `nuova-concezione`; i file sono committati (`45cd8e3`). `git status` per eventuali
   modifiche non salvate.
2. Se manca `data/it.bigrams.bin` (è gitignorato): rigeneralo (§20.6) **oppure** procedi senza (ranking
   per sola frequenza, niente next-word).
3. Configura e builda con path corto (§20.5), esegui `ctest` per confermare 6/6.
4. Prova GUI/CLI (§0). Leggi §20.3 per capire il flusso di `process()`.

### 21.2 Lavori piccoli
- ✅ **Gruppo A fatto** (vedi §20.8): filtro punteggiatura, tokenizzazione, ranking bidirezionale +
  interpolato, accent-folding T9, parametri runtime.
- Rimasto: esporre i parametri (`pesi`, `topK`, `nextN`, completamento, filtro) anche da `config.json`
  (oggi solo via API `Core::set*`). Eventuale helper opzionale di capitalizzazione (A6) se servirà.

### 21.3 Prossimi pezzi grandi (dal piano originale)
- **Backend BERT** dietro `IPredictor` (ONNX + tokenizer): nuova classe che implementa
  `rankCandidates`/`predictNext`; nessuna modifica a Core/provider. Sblocca contesto bidirezionale.
- **Il MOTORE**: la macchina a stati che *usa* questo CORE per controllare il testo (inserimento,
  conferma, Roll, cancellazioni…). È la fase esplicitamente rinviata; il CORE è progettato per essere
  pilotato dall'esterno (stateless) proprio per questo.

### 21.4 Backlog sospeso (sviluppo CORE in PAUSA)
Lo sviluppo del CORE è **sospeso** dopo M1–M7 + gruppo A + blocchi ad alta priorità (tutti committati,
`9a80e28`). Punti tenuti per il futuro, per priorità:

- **Media**
  - Trigrammi (contesto oltre l'immediato) — o direttamente il backend BERT.
  - Taratura di pesi interpolazione (0.55/0.25/0.20) e pruning (K=64, minCount=2) su casi reali; valutare
    smoothing vero (Kneser-Ney/backoff) al posto dell'interpolazione lineare.
  - Esporre i parametri (`pesi`, `topK`, `nextN`, filtro punteggiatura, completamento, skip-punteggiatura)
    da `config.json` (oggi solo via API `Core::set*`).
  - Allineare il **tokenizer del CORE** alla Strategia A degli apostrofi del MOTORE (split *dopo*
    l'apostrofo: `dell'aria` → `dell'` + `aria`), per coerenza con il documento del MOTORE
    (Strategia A del MOTORE, vedi `docs/ARCHITETTURA.md`). Oggi il CORE tiene l'apostrofo word-internal
    senza splittare.
  - Frontend: compilare la GUI **Qt** (serve Qt6); la GUI **Win32** è solo un banco (mostra il next del
    solo match in cima, niente click-per-scegliere, non ridimensionabile); **macOS** non allineato al CORE.
  - Disallineamento sorgenti: unigrammi (OpenSubtitles) vs bigrammi (file 2009) — euristico.
- **Bassa**
  - Verifica build su Linux/macOS (il CORE è portabile; `build_bigrams` gestisce lo stdin binario solo
    sotto `_WIN32`).
  - Test che carichi la **DLL** `smartcore_c` a runtime da un altro linguaggio (ora solo link statico).
  - `data/it.bigrams.bin` non versionato: da rigenerare su clone pulito (o degradare a sola frequenza).
  - Helper opzionale di capitalizzazione (A6), se servirà.
- **Roadmap grande**
  - Backend **BERT** dietro `IPredictor` (userebbe pienamente il contesto destro/bidirezionale).
  - Il **MOTORE**: macchina a stati che usa il CORE per editare il testo (inserimento, conferma, Roll,
    cancellazioni, gestione documento).

### 21.5 Puntatori
- Architettura complessiva (CORE/MOTORE/FE) e stato/backlog: `docs/ARCHITETTURA.md`.
- Memoria di sessione: `…/memory/core-nuova-concezione.md` (+ indice `MEMORY.md`).
- Commit di riferimento: `45cd8e3`.
