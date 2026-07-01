# OneHand ✍️

**Scrivi con una mano sola, come col T9 dei vecchi telefonini.**

OneHand è un piccolo aiuto per il computer. Quando è attivo, scrivi **premendo un
tasto per ogni lettera**, ma ogni tasto raggruppa più lettere (come sui vecchi
cellulari: il `2` vale `a b c`, il `3` vale `d e f`…). Non devi azzeccare la
lettera esatta: il programma **indovina la parola giusta** dal dizionario e la
scrive al posto tuo (nel Blocco note, nel browser, in Word… ovunque).

> **Esempio:** per scrivere **cane** premi i tasti `2 2 6 3`
> (c→2, a→2, n→6, e→3) → compare **cane**. Se volevi un'altra parola con la
> stessa sequenza (es. **band**), premi **Roll** per alternarle.

È pensato per chi usa **una mano sola**, in modo permanente o temporaneo (l'altra
mano occupata, un braccio ingessato, il mouse…): con il solo **tastierino
numerico** scrivi qualsiasi parola.

---

### 📍 Da dove comincio?

- **Devo installare il programma** (o lo installo per qualcun altro) → **[Parte 1 · Installazione](#parte-1--installazione)**.
- **Qualcuno me l'ha già installato e voglio solo scrivere** → **[Parte 2 · Come si usa](#parte-2--come-si-usa)**.

> L'installazione è tecnica e si fa **una volta sola**. Se non te la senti, falla
> fare a un familiare o a un tecnico: dopo, usare OneHand è semplicissimo.

---

# Parte 1 · Installazione

Il programma non è già pronto: va **creato una volta** dal computer (si chiama
«compilare»). Sotto trovi due strade separate — scegli **la tua**:

- **[💻 Ho Windows](#-installazione-su-windows)**
- **[🍏 Ho un Mac](#-installazione-su-mac-demo)**

---

## 💻 Installazione su Windows

### Cosa serve (una volta sola)

| Cosa | Come ottenerlo |
|------|----------------|
| **Compilatore C++** | [Visual Studio Build Tools](https://visualstudio.microsoft.com/it/downloads/) (gratis). Durante l'installazione spunta **«Sviluppo di applicazioni desktop con C++»**. |
| **CMake** | È già incluso nei Build Tools (componente «C++ CMake tools for Windows»). |

### I passi

1. **Scarica il progetto.** Se hai Git:

   ```bat
   git clone <url-del-repo>
   cd SmartOneHandWriter
   ```

   Niente Git? Scarica lo ZIP del progetto, estrailo e apri quella cartella.

2. **Apri il «Developer Command Prompt for VS»** dal menu Start e spostati nella
   cartella del progetto.

3. **Crea il programma** con questi due comandi:

   ```bat
   cmake -B build
   cmake --build build --config Release
   ```

4. **Fatto.** Trovi il programma qui:
   **`build\platform\windows\Release\onehand.exe`**
   Accanto ci sono già le impostazioni (`config.json`) e il dizionario
   (`wordlist_it.txt`): è pronto, non serve installare altro.

**Per avviarlo:** fai doppio clic su `onehand.exe`. Compare una finestrella con
un pulsante **▶ Play**. → Vai alla **[Parte 2 · Come si usa](#parte-2--come-si-usa)**.

> 🔁 **Hai cambiato qualcosa e vuoi rifarlo?** Basta ripetere solo
> `cmake --build build --config Release`.

---

## 🍏 Installazione su Mac (demo)

Su Mac per ora c'è una **demo**: una finestra con dentro un pannello di
configurazione (dizionario e tasti funzione riassegnabili, Play/Stop) e un foglio
su cui provare OneHand. Serve a **vederlo funzionare subito** e **non chiede alcun
permesso**. (Scrivere anche nelle *altre* app del Mac sarà un passo successivo.)

### Cosa serve (una volta sola)

I **Command Line Tools** di Apple (non serve Xcode intero). Se non li hai,
apri il Terminale e scrivi:

```bash
xcode-select --install
```

### I passi

Dal Terminale, dentro la cartella del progetto, dai questi comandi in ordine:

```bash
# 1) prepara il motore del programma
clang++ -std=c++17 -Icore/include -Icore/src -c \
  core/src/engine.cpp core/src/dictionary.cpp core/src/config.cpp \
  core/src/utf8.cpp core/src/alterations.cpp core/src/predictor_frequency.cpp \
  core/src/onehand_c.cpp

# 2) crea l'app della demo
swiftc platform/macos/main.swift *.o \
  -import-objc-header core/include/onehand/onehand_c.h \
  -framework Cocoa -lc++ -o onehand_mac

# 3) avvia
./onehand_mac
```

Si apre una finestra col pannello di configurazione in alto (dizionario, tasti
funzione, Play/Stop) e il foglio di prova sotto. Premi **▶ Play**, clicca nel
foglio e scrivi con il tastierino: vedi le parole comparire e il riquadro delle
alternative. Il pulsante **Pulisci** azzera il foglio.
→ Vai alla **[Parte 2 · Come si usa](#parte-2--come-si-usa)**.

> A differenza di Windows, qui si scrive **solo dentro questa finestra**: le
> impostazioni (`config.json`, tasti funzione) sono le stesse, ma OneHand non
> intercetta ancora la tastiera nelle altre app del Mac.

---

# Parte 2 · Come si usa

Questa è la parte che ti serve ogni giorno. È semplice: pochi tasti.

## Accendere e spegnere

- Premi **▶ Play** per accenderlo (il pulsante diventa **⏹ Stop**).
- Clicca in un punto dove si scrive (un campo di testo) e scrivi.
- Premi **⏹ Stop** per tornare alla tastiera normale.

## L'idea in una frase

Premi **una volta** il tasto di ciascuna lettera — anche se quel tasto contiene
più lettere. Il dizionario indovina la parola. Se ce n'è più d'una con la stessa
sequenza di tasti, il **Roll** te le fa scorrere.

## I tasti da usare

| Premi | Cosa succede |
|-------|--------------|
| **le cifre 2–9** | compongono la parola (ogni cifra = un gruppo di lettere) |
| **Roll** *(Tab)* | scorre le altre parole possibili con la stessa sequenza |
| **spazio** | conferma la parola e va avanti (ne apre una nuova) |
| **Shift destro** | conferma la parola **senza** andare avanti |
| **Backspace** | cancella l'ultima lettera |
| **Canc** | cancella tutta la parola (e riapre quella a sinistra) |
| **← / →** | riapre la parola **precedente / successiva** per correggerla |
| **Invio** | conferma e va a capo |

Il **tastierino numerico** funziona come le cifre in alto. Puoi anche scrivere le
lettere **direttamente** (tasti `a`–`z`): ogni lettera vale se stessa, utile per
nomi e parole fuori dizionario.

Mentre scrivi, vicino al cursore compare un **riquadro** con le altre parole
possibili, quando ce n'è più d'una: usa **Roll** per scegliere.

## Buono a sapersi

Tre cose che rendono tutto più naturale:

- **Gli spazi si mettono da soli.** Non esiste un tasto «spazio-lettera»: tu
  componi solo parole, e lo spazio tra una parola e l'altra lo mette il programma
  quando **confermi** (spazio) e apri la parola dopo.

- **La maiuscola arriva da sola.** A inizio frase la prima parola compare con
  l'iniziale **maiuscola**. Le varianti maiuscolo/minuscolo (e gli accenti)
  compaiono tra le alternative del **Roll**, dopo le parole vere.

- **Correggere una parola già scritta.** Con **← / →** ti sposti sulla parola
  prima o dopo e la **riapri**: torna modificabile e il **Roll** ti ripropone di
  nuovo le alternative (il programma ricorda quali tasti avevi premuto). Nella
  demo interna funziona su qualsiasi parola; scrivendo nelle app esterne
  (Windows) conviene procedere da destra verso sinistra.

> Con OneHand acceso, le **cifre** e i **tasti funzione** servono a comporre le
> parole. Per usarli in modo normale, premi **⏹ Stop**.
>
> Le scorciatoie con **Ctrl / Alt / Win** non vengono toccate: funzionano sempre.

---

# Parte 3 · Casi d'uso ed esempi

Con il **tastierino numerico** (2=abc 3=def 4=ghi 5=jkl 6=mno 7=pqrs 8=tuv
9=wxyz) scrivi qualsiasi parola premendo una cifra per lettera:

| Vuoi scrivere | Premi i tasti | Nota |
|---------------|---------------|------|
| **cosa** | `2 6 7 2` → spazio | c-o-s-a |
| **cane** | `2 2 6 3` → spazio | con **Roll** scegli tra cane / band… |
| **donna** | `3 6 6 6 2` → spazio | d-o-n-n-a |
| **giorno** | `4 4 6 7 6 6` → spazio | g-i-o-r-n-o |

Il principio è sempre lo stesso: **una cifra per ogni lettera**, **Roll** se la
parola mostrata non è quella giusta, **spazio** per confermare e andare avanti.

---

# Personalizzazione (facoltativa)

Le impostazioni stanno nel file **`config.json`**, accanto al programma. Lo apri
con un editor di testo, lo modifichi, **salvi e riavvii** OneHand. Questi sono i
valori di partenza:

```json
{
  "wordlist": "wordlist_it.txt",
  "max_candidates": 8,
  "keymap": {
    "letters": {
      "2": "abc", "3": "def", "4": "ghi", "5": "jkl",
      "6": "mno", "7": "pqrs", "8": "tuv", "9": "wxyz"
    }
  }
}
```

| Voce | A cosa serve |
|------|--------------|
| `wordlist` | Il dizionario da usare (un file di parole nella stessa cartella). |
| `max_candidates` | Quante alternative mostrare con il Roll. |
| `keymap.letters` | La mappa **tasto → gruppo di lettere**. Il preset è il T9 classico; puoi cambiarlo (es. layout su meno tasti). Un tasto non mappato vale se stesso. |

> Regole del file: virgolette `"..."` per il testo, numeri senza virgolette,
> una virgola tra una voce e l'altra ma **non** dopo l'ultima.

### Rimappare i tasti funzione

I tasti delle **funzioni** (Roll, Conferma, Conferma+spazio, Canc. lettera, Canc.
parola, Apri prec./succ.) si riassegnano direttamente dal **pannello** del
programma: clicca **«Assegna»**, premi il tasto che vuoi, poi **Salva**. La scelta
viene scritta in `config.json` (chiavi `roll_key`, `confirm_space_key`,
`confirm_key`, `delete_char_key`, `delete_word_key`, `open_prev_key`,
`open_next_key`) con nomi leggibili (`Tab`, `Space`, `Backspace`, `Delete`,
`Left`, `Right`, `RShift`, oppure una lettera/cifra).

### Usare un altro dizionario

Il file indicato in `wordlist` deve avere **una parola per riga**. Puoi mettere
solo la parola, oppure `parola`<kbd>TAB</kbd>`frequenza`: le parole più frequenti
vengono proposte per prime (e messe per prime dal Roll quando più parole
condividono la stessa sequenza di tasti). Righe vuote e righe che iniziano con `#`
sono ignorate. Quello incluso (`wordlist_it.txt`) ha ~49.000 parole italiane.

---

# Per chi sviluppa

Dettagli tecnici, utili solo a chi mette le mani nel codice.

## Il modello (T9 word-centric)

Tutto ruota attorno alla singola **Parola**. Ogni pressione è un **tasto** del
keymap, cioè un **gruppo di lettere**; il dizionario cerca le parole della stessa
lunghezza le cui lettere stanno, posizione per posizione, nei gruppi premuti, e le
ordina per frequenza. Il **Roll** cicla le collisioni. Gli **spazi non esistono
come dato**: il documento è una lista di parole e lo spazio tra loro è calcolato al
momento di disegnare il testo. Il motore possiede il testo canonico ed emette solo
il **diff minimo** (cancella dalla coda + reinserisci) verso il campo con focus.

## Com'è fatto il progetto

Il codice è diviso in **motore** (logica pura, portabile) e **frontend** (la
parte legata al sistema operativo). Così il cuore si scrive una volta sola e ogni
sistema aggiunge solo il suo strato sottile.

| Cartella / file | A cosa serve |
|-----------------|--------------|
| `core/` | **Il motore**: keymap, dizionario T9 (`computeCandidates` su gruppi di lettere), composizione a Parola, render con spazi derivati, alterazioni (maiuscole/accenti). Nessuna dipendenza dal sistema operativo; testabile da solo (`core/tests/`). |
| `core/include/onehand/predictor.hpp` | Interfaccia **`Predictor`** per il ranking dei candidati e la parola successiva. Default: ordine per frequenza. Pensata per agganciare più avanti un ranking neurale (n-gram / ONNX) senza toccare il motore. |
| `platform/windows/main_win32.cpp` | **Frontend Windows**: cattura tasti, scrittura del testo, finestrella Play/Stop, popup, rimappatura dei tasti funzione. |
| `platform/macos/main.swift` | **Demo macOS** (Swift + AppKit). Usa il motore tramite la C ABI. |
| `data/wordlist_it.txt` | Dizionario italiano (parola`TAB`frequenza, da OpenSubtitles 2018). |
| `data/config.json` | Le impostazioni (keymap, dizionario). |

> C'è una **C ABI** opzionale (`core/include/onehand/onehand_c.h`) per chiamare il
> motore da altri linguaggi (Swift, C#, Rust…). La usa la demo macOS: le azioni
> risolte dal frontend con `onehand_on_action` / `onehand_on_action_index` (per
> l'accesso casuale `OPEN_WORD_AT`), l'introspezione del documento
> (`onehand_word_count` / `onehand_open_index` / `onehand_caret` /
> `onehand_render_text`) e `onehand_apply_config_json` per leggere `config.json`
> con lo stesso parser tollerante. Il frontend Windows chiama `onehand::Engine`
> direttamente e non ne ha bisogno.

### In arrivo (rinviato)

Punteggiatura e apostrofi, ranking **neurale** (l'interfaccia `Predictor` è già
pronta) e accesso casuale anche nelle app esterne. Vedi il piano di progetto.

## Windows: opzioni aggiuntive

**Forzare i 64 bit** con il generatore Visual Studio:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

(`17 2022` è VS 2022; usa `16 2019` per VS 2019.)

**Eseguire i test del motore** (non serve la GUI):

```bat
ctest --test-dir build\core --output-on-failure -C Release
```

**Costruire anche la C ABI** (`onehand_c.dll`):

```bat
cmake -B build -DONEHAND_BUILD_C_ABI=ON
cmake --build build --config Release
```

## Windows: compilare a mano con `cl` (senza CMake)

Dalla **«x64 Native Tools Command Prompt for VS»**, nella cartella del progetto:

```bat
cl /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I core\include /I core\src ^
   platform\windows\main_win32.cpp ^
   core\src\engine.cpp core\src\dictionary.cpp core\src\config.cpp ^
   core\src\utf8.cpp core\src\alterations.cpp core\src\predictor_frequency.cpp ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib /OUT:onehand.exe
```

In questo caso i due file dati vanno copiati a mano accanto all'eseguibile
(CMake lo fa da solo, `cl` no):

```bat
copy data\config.json .
copy data\wordlist_it.txt .
```
