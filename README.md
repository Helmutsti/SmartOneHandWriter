# OneHand ✍️

**Scrivi con una mano sola, in qualsiasi programma.**

OneHand è un piccolo aiuto per il computer. Quando è attivo, digiti solo le
lettere che la tua mano raggiunge e metti uno **spazio** al posto di ogni lettera
che manca: il programma indovina la parola giusta e la scrive al posto tuo (nel
Blocco note, nel browser, in Word… ovunque).

> **Esempio:** vuoi scrivere **donna** ma la `o` non la raggiungi →
> digiti `d` spazio `nna` → compare **donna**.

È pensato per chi usa **una mano sola**, in modo permanente o temporaneo (l'altra
mano occupata, un braccio ingessato, il mouse…).

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

Su Mac per ora c'è una **demo**: una finestra con dentro un foglio su cui provare
OneHand. Serve a **vederlo funzionare subito** e **non chiede alcun permesso**.
(Scrivere anche nelle *altre* app del Mac sarà un passo successivo.)

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
  core/src/utf8.cpp core/src/onehand_c.cpp

# 2) crea l'app della demo
swiftc platform/macos/main.swift *.o \
  -import-objc-header core/include/onehand/onehand_c.h \
  -framework Cocoa -lc++ -o onehand_mac

# 3) avvia
./onehand_mac
```

Si apre una finestra: scrivi con una mano (lo **spazio** è il jolly), vedi le
parole comparire e il riquadro delle alternative. Il pulsante **Pulisci** azzera.
→ Vai alla **[Parte 2 · Come si usa](#parte-2--come-si-usa)**.

---

# Parte 2 · Come si usa

Questa è la parte che ti serve ogni giorno. È semplice: pochi tasti.

## Accendere e spegnere

- Premi **▶ Play** per accenderlo (il pulsante diventa **⏹ Stop**).
- Clicca in un punto dove si scrive (un campo di testo) e scrivi.
- Premi **⏹ Stop** per tornare alla tastiera normale.

## I tasti da usare

| Premi | Cosa succede |
|-------|--------------|
| **le lettere** | compongono la parola |
| **spazio** | mette una lettera «da indovinare» (un jolly) al posto di una che ti manca |
| **spazio ×2** | conferma la parola e va avanti |
| **Tab** | mostra altre parole possibili; **a inizio parola** apre la **punteggiatura** |
| **Backspace** | cancella una lettera |
| **Backspace ×2** | cancella tutta la parola |
| **Invio** | conferma e va a capo |

> **«×2»** vuol dire premere **due volte di fila, veloce**, lo stesso tasto.

Mentre scrivi, vicino al cursore compare un **riquadro** con le altre parole
possibili (quando ce n'è più d'una) o con i segni di punteggiatura.

## Buono a sapersi

Quattro cose che rendono tutto più naturale:

- **La conferma tiene la parola che vedi.** «Spazio ×2» scrive nel testo
  **esattamente la parola mostrata** in quel momento e aggiunge uno spazio. Se
  non hai usato lo spazio-jolly, è già la parola che hai digitato tu (es. `va`):
  non c'è niente da «scegliere», basta confermare e andare avanti.

- **La maiuscola arriva da sola.** A inizio frase, e dopo un punto `.` `!` `?`,
  la prima lettera della parola successiva diventa **maiuscola**
  automaticamente.

- **I segni di punteggiatura si mettono tra le parole.** Si scelgono con **Tab**
  solo quando **non** stai scrivendo una parola. Quindi, per mettere un segno
  dopo una parola, prima **conferma la parola** (spazio ×2), poi premi **Tab** e
  scegli il segno: si attacca da solo alla parola prima (`ciao,` — mai `ciao ,`).
  *Perché:* il programma non sa che hai finito la parola finché non la confermi.

- **Cancellare subito dopo una conferma.** Appena confermata una parola, il
  primo **Backspace** torna a modificarla: toglie in un colpo lo spazio **e**
  l'ultima lettera (es. `ciao ` → torni su `cia`). Da lì ogni Backspace toglie
  una lettera per volta.

> Con OneHand acceso, **spazio** e **Backspace** servono a comporre le parole.
> Per usarli in modo normale, premi **⏹ Stop**.
>
> Le scorciatoie con **Ctrl / Alt / Win** non vengono toccate: funzionano sempre.

---

# Parte 3 · Casi d'uso ed esempi

Immagina di avere solo la **mano sinistra** sulla tastiera: raggiungi le lettere
`qwertasdfgzxcvb` e ti mancano le altre (`i o u l m n h p`…). Ecco come scrivere
qualche parola:

| Vuoi scrivere | Digiti | Nota |
|---------------|--------|------|
| **cosa** | `c` spazio `sa` → spazio ×2 | |
| **cane** | `ca` spazio `e` → spazio ×2 | con **Tab** scegli tra cane / cape / cake… |
| **donna** | `d` spazio `nna` → spazio ×2 | |
| **giorno** | `gi` spazio `r` spazio spazio → spazio ×2 | due jolly, per la `o` e la `n` |

Il principio è sempre lo stesso: **scrivi le lettere che raggiungi**, metti uno
**spazio** dove ti manca una lettera, e **conferma** con spazio ×2.

---

# Personalizzazione (facoltativa)

Le impostazioni stanno nel file **`config.json`**, accanto al programma. Lo apri
con un editor di testo, lo modifichi, **salvi e riavvii** OneHand. Questi sono i
valori di partenza:

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
| `available_keys` | Le lettere che la tua mano raggiunge. Mano sinistra: `qwertasdfgzxcvb`. Mano destra: `yuiophjklnm`. |
| `wordlist` | Il dizionario da usare (un file di parole nella stessa cartella). |
| `wildcard_matches` | `"unavailable"` = il jolly è solo una lettera che non puoi digitare (consigliato). `"any"` = qualsiasi lettera. |
| `max_candidates` | Quante alternative mostrare quando premi Tab. |
| `punctuation` | I segni che compaiono (nell'ordine) quando apri la punteggiatura. |
| `double_press_ms` | Quanto tempo hai per fare il «×2». Più alto = più tempo. |

> Regole del file: virgolette `"..."` per il testo, numeri senza virgolette,
> una virgola tra una voce e l'altra ma **non** dopo l'ultima.

### Usare un altro dizionario

Il file indicato in `wordlist` deve avere **una parola per riga**. Puoi mettere
solo la parola, oppure `parola`<kbd>TAB</kbd>`frequenza`: le parole più frequenti
vengono proposte per prime. Righe vuote e righe che iniziano con `#` sono
ignorate. Quello incluso (`wordlist_it.txt`) ha ~49.000 parole italiane.

---

# Per chi sviluppa

Dettagli tecnici, utili solo a chi mette le mani nel codice.

## Com'è fatto il progetto

Il codice è diviso in **motore** (logica pura, portabile) e **frontend** (la
parte legata al sistema operativo). Così il cuore si scrive una volta sola e ogni
sistema aggiunge solo il suo strato sottile.

| Cartella / file | A cosa serve |
|-----------------|--------------|
| `core/` | **Il motore**: dizionario, ricostruzione delle parole, composizione, punteggiatura, maiuscole. Nessuna dipendenza dal sistema operativo; testabile da solo (`core/tests/`). |
| `platform/windows/main_win32.cpp` | **Frontend Windows**: cattura tasti, scrittura del testo, finestrella Play/Stop, popup. |
| `platform/macos/main.swift` | **Demo macOS** (Swift + AppKit). Usa il motore tramite la C ABI. |
| `data/wordlist_it.txt` | Dizionario italiano (parola`TAB`frequenza, da OpenSubtitles 2018). |
| `data/config.json` | Le impostazioni. |

> C'è una **C ABI** opzionale (`core/include/onehand/onehand_c.h`) per chiamare il
> motore da altri linguaggi (Swift, C#, Rust…). La usa la demo macOS; il frontend
> Windows non ne ha bisogno.

## Windows: opzioni aggiuntive

**Forzare i 64 bit** con il generatore Visual Studio:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

(`17 2022` è VS 2022; usa `16 2019` per VS 2019.)

**Eseguire i test del motore** (non serve la GUI):

```bat
ctest --test-dir build --output-on-failure
```

**Costruire anche la C ABI** (`onehand_c.dll`):

```bat
cmake -B build -DONEHAND_BUILD_C_ABI=ON
cmake --build build --config Release
```

## Windows: compilare a mano con `cl` (senza CMake)

Dalla **«x64 Native Tools Command Prompt for VS»**, nella cartella del progetto:

```bat
cl /EHsc /std:c++17 /DUNICODE /D_UNICODE /I core\include ^
   platform\windows\main_win32.cpp ^
   core\src\engine.cpp core\src\dictionary.cpp core\src\config.cpp core\src\utf8.cpp ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib /OUT:onehand.exe
```

In questo caso i due file dati vanno copiati a mano accanto all'eseguibile
(CMake lo fa da solo, `cl` no):

```bat
copy data\config.json .
copy data\wordlist_it.txt .
```
