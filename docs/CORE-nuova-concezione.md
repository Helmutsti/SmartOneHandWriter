# SmartOneHandWriter — CORE (nuova concezione)

> Documento di progettazione completo e auto-contenuto. Cattura **tutto** ciò che è stato
> discusso e deciso, incluse le motivazioni e l'evoluzione delle idee, così da poter riprendere
> il lavoro in qualsiasi momento senza perdere informazioni.
>
> Branch: `nuova-concezione`. Stato working tree al momento della stesura: **vuoto su disco**
> (tutti i file tracciati sono in HEAD, cancellati nel working tree → serve `git restore .`).

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

Vedi anche il file di piano: `~/.claude/plans/twinkling-snuggling-feather.md` (stesso contenuto
operativo di §10–§16). Prossimo passo alla ripresa: **`git restore .`**, poi milestone 1.

---

## 20. Stato dell'implementazione (aggiornato)

Milestone M1–M6 **completate e testate**; M7 (GUI Qt) **scritta ma non compilabile qui** (Qt6 assente).

### Build
- Toolchain: CMake (quello incluso in Visual Studio 2022) + MSVC. CMake/compilatori NON sono nel PATH.
- ⚠️ **Path di build corto obbligatorio**: MSBuild FileTracker supera MAX_PATH con path lunghi (es. lo
  scratchpad). Build usata: `C:\shwb`.
- Configura: `cmake -S <repo> -B C:\shwb`
- Test: build dei target test + `ctest --test-dir C:\shwb\core -C Debug`. **6/6 verdi**:
  `engine_tests` (legacy), `sohw_tests` (pipeline+integrazione reale), `dict_tests`, `provider_tests`,
  `bigram_tests` (hermetico + reale), `abi_tests` (C ABI).

### Cosa è stato realizzato
- **CORE**: `core/src/sohw/` — `types.hpp`, `core.{hpp,cpp}`, `candidate_provider.{hpp,cpp}`
  (`T9CandidateProvider`, `LiteralCandidateProvider`), `predictor.hpp` (IPredictor UTF-8+score),
  `bigram_model.{hpp,cpp}`, `bigram_predictor.{hpp,cpp}`, `bigram_format.hpp`.
- **Riuso/modifica**: `dictionary.{hpp,cpp}` (+`computeCandidatesPrefix` len≥n, +`completionsOf` prefisso);
  `utf8.{hpp,cpp}` (+`wToUtf8`).
- **C ABI**: `core/include/sohw/smartcore_c.h` + `core/src/sohw/smartcore_c.cpp`. Shared opt-in
  `-DSOHW_BUILD_C_ABI=ON` (auto-export simboli su MSVC, verificato con dumpbin).
- **Tool**: `tools/build_bigrams/` → `data/it.bigrams.bin` (19.7 MB, K=64, minCount=2). Generazione:
  ``"/c/Program Files/7-Zip/7z.exe" x -so it.word.bigrams.7z | build_bigrams data/wordlist_it.txt - data/it.bigrams.bin`` (~21 s, pipe binary-safe via bash; niente file da 1.2 GB su disco).
- **Harness da terminale** (NUOVO, zero deps): `cli/` → `sohw_cli`. Legge `left | right | encoded [ | mode]`
  e stampa match + next-word. Usato per validare il CORE senza GUI.
- **GUI Qt**: `gui/qt/` (main.cpp + CMake). Opt-in, si **auto-salta** se Qt6 assente
  (`find_package(Qt6 QUIET)`). Non compilata qui (Qt6 non installato); resta pronta.
- **GUI Win32** (NUOVA, scelta dall'utente in mancanza di Qt): `gui/win32/main.cpp` — nativa, zero
  dipendenze, 4 campi + checkbox T9/Classico, UTF-8↔UTF-16 ai bordi, cablata a `sohw_core`.
  Target `sohw_gui_win32` (solo WIN32). **Compilata ed eseguita**: mostra correttamente match
  (`per`+`52`→"la" 0.2213) e next-word — verificata anche via screenshot.

### Verifica dal vivo (sohw_cli sui dati reali)
- `per | | 52` → **la** (score 0.2213), next-word: sua/prima/mia… → contesto applicato.
- `| | citt | literal` → **città, cittadini…** (accenti UTF-8 corretti).
- `| | 7653` → soldi/sole/soldato…

### Punti di tuning noti
- **Punteggiatura nel next-word**: i token `, . :` dominano `predictNext` (sono nel file bigrammi).
  Per la GUI conviene un filtro opzionale della punteggiatura nei suggerimenti next-word.
- **Score dei match senza contesto = 0**: senza `prev` (o bigramma) il ranking resta per frequenza
  (ordine del provider) con score 0. Atteso; il segnale contestuale entra con `prev` noto.
- Parametri pruning bigrammi (K=64, minCount=2) tarabili rigenerando il binario.
