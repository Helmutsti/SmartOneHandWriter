# OneHand ✍️

**Scrivi con una mano sola, in qualsiasi programma.**

OneHand è una piccola utility per Windows. Quando è attiva, digiti solo le
lettere che la tua mano raggiunge e metti uno **spazio** al posto di ogni lettera
che manca: il programma indovina la parola da un dizionario e la scrive nel campo
in cui stai lavorando (Blocco note, browser, Word, ovunque).

> Esempio al volo: vuoi scrivere **donna** ma la `o` non la raggiungi →
> digiti `d` spazio `nna` → compare **donna**.

È pensato per chi usa una mano sola, in modo permanente o temporaneo (l'altra
mano occupata, un braccio ingessato, il mouse…).

---

## Com'è organizzato il progetto

Il codice è diviso in **motore** (logica pura, portabile) e **frontend** (parte
specifica del sistema operativo). Così il cuore del programma si scrive una volta
e ogni OS aggiunge solo il suo strato sottile.

| Cartella / file | A cosa serve | |
|-----------------|--------------|---|
| `core/` | **Il motore**: dizionario, ricostruzione delle parole, composizione, punteggiatura, maiuscole. Nessuna dipendenza dal sistema operativo. | ✅ incluso |
| `platform/windows/main_win32.cpp` | **Il frontend Windows**: cattura tasti, iniezione testo, finestrella Play/Stop, popup. Usa il motore. | ✅ incluso |
| `data/wordlist_it.txt` | Il dizionario italiano: una parola per riga, formato `parola`<kbd>TAB</kbd>`frequenza` (~49.000 parole, da OpenSubtitles 2018). | ✅ incluso |
| `data/config.json` | Le impostazioni (lettere della mano, dizionario, tempi…). | ✅ incluso |
| `onehand.exe` | L'eseguibile. **Non è incluso: lo crei tu compilando** (vedi sotto). | ⚙️ da compilare |

> 🧩 **Perché diviso così?** Il `core/` è una libreria senza GUI né tastiera: si
> può testare da solo (`core/tests/`) e riusare per frontend su altri sistemi.
> C'è anche una **C ABI** opzionale (`core/include/onehand/onehand_c.h`) per
> chiamare il motore da altri linguaggi (Swift, C#, Rust…): si attiva con
> `-DONEHAND_BUILD_C_ABI=ON` e non serve al frontend Windows.

---

## 1. Compila (Windows)

OneHand si compila su **Windows** in due modi: con **CMake** (consigliato) oppure
a mano con `cl`. Sotto trovi entrambi, passo per passo.

### 1.0 Prerequisiti

| Cosa | Come ottenerlo |
|------|----------------|
| **Compilatore C++** | [Visual Studio Build Tools](https://visualstudio.microsoft.com/it/downloads/) (gratis) → in fase di installazione spunta il carico di lavoro **«Sviluppo di applicazioni desktop con C++»**. In alternativa Visual Studio Community con lo stesso carico. |
| **CMake** | Incluso nei Build Tools (componente «C++ CMake tools for Windows»), oppure da [cmake.org](https://cmake.org/download/) → durante l'installazione scegli **«Add CMake to the PATH»**. |
| **Git** (facoltativo) | [git-scm.com](https://git-scm.com/), solo se cloni il repo invece di scaricarlo come ZIP. |

Scarica il progetto:

```bat
git clone <url-del-repo>
cd SmartOneHandWriter
```

> Niente Git? Scarica lo ZIP del repository, estrailo e apri quella cartella.

---

### 1.1 Metodo consigliato: CMake

Apri il **«Developer Command Prompt for VS»** (o «x64 Native Tools Command Prompt
for VS») dal menu Start, spostati nella cartella del progetto e lancia:

```bat
cmake -B build
cmake --build build --config Release
```

- `cmake -B build` → prepara la build nella cartella `build\` (rileva il
  compilatore e genera i file di progetto).
- `cmake --build build --config Release` → compila in versione ottimizzata.

Risultato: **`build\platform\windows\Release\onehand.exe`** (il percorso esatto
può variare col generatore, es. `build\Release\` con alcuni). CMake **copia da
solo** `config.json` e `wordlist_it.txt` accanto all'eseguibile: è già pronto
all'uso, nessuna installazione.

**Per avviarlo:** doppio clic su `onehand.exe`. Compare una finestrella con un
pulsante **▶ Play**.

> 🔁 **Ricompilare dopo una modifica:** ti basta rilanciare
> `cmake --build build --config Release` (non serve ripetere `cmake -B build`).

#### (Opzionale) Build a 64 bit esplicita

Se vuoi forzare l'architettura a 64 bit con il generatore Visual Studio:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

(`17 2022` è VS 2022; usa `16 2019` per VS 2019.)

#### (Opzionale) Eseguire i test del motore

I test verificano la logica del cuore (`core/tests/`) e **non richiedono la GUI**:

```bat
ctest --test-dir build --output-on-failure
```

#### (Opzionale) Costruire anche la C ABI

Serve solo se vuoi chiamare il motore da altri linguaggi (Swift, C#, Rust…):

```bat
cmake -B build -DONEHAND_BUILD_C_ABI=ON
cmake --build build --config Release
```

Produce la libreria condivisa `onehand_c.dll` accanto al resto. Il frontend
Windows **non** ne ha bisogno.

---

### 1.2 Metodo alternativo: a mano con `cl` (senza CMake)

Apri la **«x64 Native Tools Command Prompt for VS»** nella cartella del progetto
e lancia (una sola riga; i `^` mandano a capo nel prompt di Windows):

```bat
cl /EHsc /std:c++17 /DUNICODE /D_UNICODE /I core\include ^
   platform\windows\main_win32.cpp ^
   core\src\engine.cpp core\src\dictionary.cpp core\src\config.cpp core\src\utf8.cpp ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib /OUT:onehand.exe
```

Spiegazione dei pezzi principali:

| Pezzo | A cosa serve |
|-------|--------------|
| `/std:c++17` | il progetto usa C++17. |
| `/DUNICODE /D_UNICODE` | abilita le API Windows in versione Unicode (testo a 16 bit). |
| `/I core\include` | fa trovare gli header del motore (`onehand/engine.hpp`…). |
| i `core\src\*.cpp` | compila il motore **insieme** al frontend in un unico exe. |
| `/SUBSYSTEM:WINDOWS` | app con finestre (non console). |
| `user32.lib gdi32.lib` | librerie Windows per finestre, input e disegno. |

Questo crea `onehand.exe` nella cartella corrente. In questo caso **copia tu**
i due file dati accanto all'eseguibile (CMake lo fa da solo, `cl` no):

```bat
copy data\config.json .
copy data\wordlist_it.txt .
```

> Vuoi un doppio clic per ricompilare? Crea un file `build.bat` con dentro la
> riga `cl …` qui sopra (più i due `copy`), così ricompili lanciandolo.

---

## 1-bis. Demo su macOS (Swift)

Una **demo** che fa girare il motore dentro la sua finestra, con un editor
interno. Serve a **vedere subito OneHand al lavoro** su Mac: **non chiede alcun
permesso** (intercetta i tasti solo nella propria finestra; scrivere nelle *altre*
app — col permesso Accessibilità — sarà un passo successivo).

Ti bastano i **Command Line Tools** di Apple (niente Xcode): forniscono `swiftc`,
`clang` e l'SDK con AppKit. Se non li hai, installali con:

```bash
xcode-select --install
```

Poi, dalla **radice del repo**, compila in due passi:

```bash
# 1) il motore C++ (con la C ABI) → file oggetto
clang++ -std=c++17 -Icore/include -Icore/src -c \
  core/src/engine.cpp core/src/dictionary.cpp core/src/config.cpp \
  core/src/utf8.cpp core/src/onehand_c.cpp

# 2) l'app Swift: linka gli oggetti + Cocoa, importando l'header C
swiftc platform/macos/main.swift *.o \
  -import-objc-header core/include/onehand/onehand_c.h \
  -framework Cocoa -lc++ -o onehand_mac

# avvia (legge ./data/wordlist_it.txt)
./onehand_mac
```

Si apre una finestra: scrivi con una mano (lo **spazio** è il jolly), vedi le
parole comparire e il popup delle alternative. Il pulsante **Pulisci** azzera.

> 🧩 **Come funziona il collegamento:** l'app Swift non parla direttamente col
> C++; usa la **C ABI** (`core/include/onehand/onehand_c.h`), lo stesso ponte
> pensato per Swift/C#/Rust. Il motore (`core/`) è identico a quello di Windows.

---

## 2. Come si usa

Premi **▶ Play** per attivarlo (diventa **⏹ Stop**), clicca in un campo di testo
e scrivi. Premi **⏹ Stop** per tornare alla tastiera normale.

### I comandi

| Premi | Cosa fa |
|-------|---------|
| **lettere** | compongono la parola |
| **spazio** | mette un jolly `?` (una lettera da indovinare) |
| **spazio ×2** | conferma la parola e mette lo spazio |
| **Tab** | scorre le alternative; a inizio parola apre la **punteggiatura** |
| **Backspace** | cancella una lettera |
| **Backspace ×2** | cancella tutta la parola |
| **Invio** | conferma e va a capo (come al solito) |

«×2» = due pressioni veloci dello stesso tasto.

Mentre componi, un piccolo **popup** vicino al cursore mostra le alternative
(quando ce n'è più d'una) o i segni di punteggiatura.

### Maiuscole e punteggiatura

Tre comodità che funzionano da sole:

- **Maiuscola automatica**: dopo un punto `.` `!` `?` (e a inizio testo), la
  prima lettera della parola successiva diventa **maiuscola**.
- **Punteggiatura**: a inizio parola premi **Tab**, scorri fino al segno che vuoi
  e conferma con **spazio ×2**. Il segno si **attacca alla parola precedente** e
  lo spazio passa dopo (`ciao,` e poi `ciao, come` — mai `ciao ,`).
- **Maiuscola di una lettera**: se hai scritto una **sola lettera**, con **Tab**
  scegli tra minuscola e **MAIUSCOLA** (es. `a` → `A`).

> Le scorciatoie con **Ctrl / Alt / Win** non vengono toccate: funzionano sempre,
> anche con OneHand attivo.

---

## 3. Personalizza: il file `config.json`

È un file di testo accanto all'eseguibile. Lo modifichi, **salvi e riavvii**
OneHand. Ecco com'è fatto (questi sono i valori di default):

```json
{
  "hand": { "available_keys": "qwertasdfgzxcvb" },
  "wordlist": "wordlist_it.txt",
  "wildcard_matches": "unavailable",
  "max_candidates": 8,
  "punctuation": ",.?!:;'()-",
  "timing": { "double_press_ms": 280 }
}
```

| Voce | A cosa serve |
|------|--------------|
| `available_keys` | Le lettere che la tua mano riesce a premere. Cambiala in base alla mano: sinistra `qwertasdfgzxcvb`, destra `yuiophjklnm`. |
| `wordlist` | Il dizionario da usare (un file di parole nella stessa cartella). |
| `wildcard_matches` | `"unavailable"` = il jolly è solo una lettera che non puoi digitare (consigliato). `"any"` = qualsiasi lettera. |
| `max_candidates` | Quante alternative mostrare quando premi Tab. |
| `punctuation` | I segni che compaiono (nell'ordine) quando apri la punteggiatura con Tab. |
| `double_press_ms` | Quanto sei veloce con la doppia pressione (millisecondi). Più alto = più tempo per il «×2». |

Regole rapide del formato: virgolette `"..."` per il testo, numeri senza
virgolette, virgola tra una voce e l'altra ma **non** dopo l'ultima.

### Usare un altro dizionario

Il file indicato in `wordlist` deve avere **una parola per riga**. Puoi mettere
solo la parola, oppure `parola`<kbd>TAB</kbd>`frequenza` (o uno spazio al posto
del TAB): le parole più frequenti vengono proposte per prime. Le righe vuote e
quelle che iniziano con `#` vengono ignorate.

---

## 4. Esempi

Mano sinistra attiva (ti mancano `i o u l m n h p`…):

| Vuoi scrivere | Digiti | Note |
|---------------|--------|------|
| **cosa** | `c` spazio `sa` poi spazio ×2 | |
| **cane** | `ca` spazio `e` poi spazio ×2 | con Tab scegli tra cane / cape / cake… |
| **donna** | `d` spazio `nna` poi spazio ×2 | |
| **giorno** | `gi` spazio `r` spazio spazio poi spazio ×2 | due jolly per `o` e `n` |

---

### Buono a sapersi
Mentre OneHand è attivo, lo spazio e il Backspace servono a comporre le parole:
per scrivere o cancellare in modo normale, premi **⏹ Stop**.
