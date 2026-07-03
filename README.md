# SmartOneHandWriter ✍️

**Scrivi con una mano sola, come col T9 dei vecchi telefonini.**

SmartOneHandWriter (l'assistente si chiama `sohw_assistant`) è un piccolo aiuto per
il computer. Quando è attivo, scrivi **premendo un tasto per ogni lettera**, ma ogni
tasto raggruppa più lettere (come sui vecchi cellulari: il `2` vale `a b c`, il `3`
vale `d e f`…). Non devi azzeccare la lettera esatta: il programma **indovina la
parola giusta** dal dizionario e la scrive al posto tuo (nel Blocco note, nel
browser, in Word… ovunque).

> **Esempio:** per scrivere **cane** premi, una volta ciascuno, i tasti dei gruppi
> di `c a n e`; se compare un'altra parola con la stessa sequenza (es. **band**),
> premi **Roll** per alternarle. (Quali tasti fisici sono i «gruppi» è spiegato
> nella [Parte 2](#le-tre-modalità).)

È pensato per chi usa **una mano sola**, in modo permanente o temporaneo (l'altra
mano occupata, un braccio ingessato, il mouse…): con **otto tasti** a portata di una
mano scrivi qualsiasi parola.

---

### 📍 Da dove comincio?

- **Devo installare il programma** (o lo installo per qualcun altro) → **[Parte 1 · Installazione](#parte-1--installazione)**.
- **Qualcuno me l'ha già installato e voglio solo scrivere** → **[Parte 2 · Come si usa](#parte-2--come-si-usa)**.

> L'installazione è tecnica e si fa **una volta sola**. Se non te la senti, falla
> fare a un familiare o a un tecnico: dopo, usare il programma è semplicissimo.

---

# Parte 1 · Installazione

Il programma non è già pronto: va **creato una volta** dal computer (si chiama
«compilare»). Serve **Windows**.

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
   **`build\app\windows\Release\sohw_assistant.exe`**
   Il dizionario e il modello di parole vengono letti dalla cartella `data\` del
   progetto: è pronto, non serve installare altro.

**Per avviarlo:** fai doppio clic su `sohw_assistant.exe`. Compaiono un **pannello**
di comandi e (quando scrivi) un **riquadro** in sovraimpressione col testo. All'avvio
è **in pausa**: premi **Play/Pause** per accendere. → Vai alla
**[Parte 2 · Come si usa](#parte-2--come-si-usa)**.

> 🔁 **Hai cambiato qualcosa e vuoi rifarlo?** Basta ripetere solo
> `cmake --build build --config Release`.

> 📦 **Distribuire il programma su un altro PC** (senza compilatore): vedi la ricetta
> del pacchetto self-contained in `docs/ARCHITETTURA.md` §7.

---

# Parte 2 · Come si usa

L'assistente Windows (`sohw_assistant.exe`) ti aiuta a scrivere **in qualsiasi
applicazione**: intercetta i tasti, tiene un **buffer** di testo mostrato in un
riquadro in sovraimpressione, e lo scrive nell'app attiva quando glielo chiedi.

## Le due finestre

- **Pannello**: un bottone per ogni funzione, più **Play/Pause** e **Modalità**. I
  bottoni si **attivano/disattivano** da soli secondo cosa ha senso in quel momento
  (es. *Roll* si accende solo se ci sono più parole possibili).
- **Riquadro** (overlay): mostra il buffer mentre scrivi; la parola **selezionata**
  è azzurra, quella **aperta** (in scrittura) è ambra. Sotto compare la **riga dei
  suggerimenti** (candidati o parola successiva). Sta **fermo** e lo puoi
  **trascinare** col mouse dove preferisci; sparisce quando il buffer è vuoto.

## Accendere e spegnere

- Premi **Play/Pause** sul pannello per accendere (intercetta la tastiera) o
  mettere in pausa (i tasti tornano normali).
- **Modalità** cicla tra **Assistita (T9)**, **Classica** e **Multi-tap**.

## Le tre modalità

- **Assistita (T9)**: come su un **cellulare**. Gli 8 tasti `w e / a s d / z x c`
  fanno da tasti **2–9** (`w`=2=abc, `e`=3=def, `a`=4=ghi, `s`=5=jkl, `d`=6=mno,
  `z`=7=pqrs, `x`=8=tuv, `c`=9=wxyz) e lo **Spazio** è lo `0`. Premi **un** tasto per
  lettera e il dizionario indovina la parola; **Roll** cicla le alternative.
- **Multi-tap (scorrimento lettere)**: stessi 8 tasti, ma **ripremendo** lo stesso
  tasto scorri le lettere del gruppo (come sui vecchi cellulari): `w`=a, `w w`=b,
  `w w w`=c. Aspetta un istante (o cambia tasto) per fissare la lettera e passare
  alla successiva. Serve per scrivere **esattamente** ciò che vuoi, es. nomi e parole
  fuori dizionario.
- **Classica**: ogni lettera `a`–`z` vale se stessa, con completamento; il primo
  candidato è sempre ciò che hai digitato.

## I tasti (modalità assistita)

Oltre ai bottoni del pannello, con l'assistente **acceso** valgono queste scorciatoie
(tutte a portata di una mano sinistra). Sono i **valori di fabbrica**: puoi
riassegnarle nel file `data/tasti.conf` (vedi [Personalizzazione](#personalizzazione-facoltativa)).

| Premi | Funzione |
|-------|----------|
| `w e a s d z x c` | compongono la parola (tasti 2–9 del cellulare) |
| **F** | **Roll** — scorre le parole possibili con la stessa sequenza |
| **G** | **Conferma** — chiude la parola e resta lì |
| **R** | **Avanti** — apre una nuova parola a destra |
| **Spazio** (lo `0`) | **Conferma continua** — chiude e va avanti (automatica a fine frase) |
| **T** | **Apri/Edit** — riapre la parola selezionata per correggerla |
| **V** / **B** | **Naviga** ◀ / ▶ tra le parole |
| **Backspace** / **Bloc Maiusc** | cancella **una lettera** della parola aperta |
| **Tab** | cancella **l'intera parola** selezionata |
| **Esc** | **Scarta** — butta via tutto il buffer (il riquadro sparisce) |
| **1 2 3 4** | punteggiatura `.` `,` `?` `!` |
| **5** | **Write** — scrive il buffer nell'app attiva e lo svuota |
| **` (grave)** | **Read** — carica nel buffer il testo copiato negli appunti |

> **Read/Write** passano dagli **appunti**: *Read* legge quello che hai copiato con
> `Ctrl+C`; *Write* incolla il buffer nell'app attiva (con `Ctrl+V`) e poi ripristina
> i tuoi appunti. Gli **spazi** e la **punteggiatura** tra le parole li mette il
> programma; in Classica i tasti-lettera occupano `R T F G V B`, quindi quelle
> funzioni si usano dai **bottoni**.

## Buono a sapersi

- **Gli spazi si mettono da soli.** Non esiste un tasto «spazio-lettera»: tu componi
  solo parole, e lo spazio tra una parola e l'altra lo mette il programma quando
  **confermi** (Spazio) e apre la parola dopo.

- **Correggere una parola già scritta.** Con **Naviga ◀ / ▶** ti sposti sulla parola
  prima o dopo e con **Apri/Edit** la **riapri**: torna modificabile e il **Roll** ti
  ripropone di nuovo le alternative (il programma ricorda quali tasti avevi premuto).
  Scrivendo nelle app esterne conviene procedere da destra verso sinistra.

> Con l'assistente acceso, i tasti mappati servono a comporre e comandare. Per usarli
> in modo normale, premi **Play/Pause** per mettere in pausa.
>
> Le scorciatoie con **Ctrl / Alt / Win** non vengono toccate: funzionano sempre.

---

# Parte 3 · Casi d'uso ed esempi

Ragionando col modello del **cellulare** (2=abc 3=def 4=ghi 5=jkl 6=mno 7=pqrs
8=tuv 9=wxyz), scrivi qualsiasi parola premendo una «cifra» per lettera. Sulla
tastiera dell'assistente quelle cifre **2–9 sono i tasti** `w e a s d z x c`
(`2`=`w`, `3`=`e`, `4`=`a`, `5`=`s`, `6`=`d`, `7`=`z`, `8`=`x`, `9`=`c`).

| Vuoi scrivere | «Cifre» del cellulare | Tasti da premere | Nota |
|---------------|-----------------------|------------------|------|
| **cosa**   | `2 6 7 2`     | `w d z w`     | c-o-s-a |
| **cane**   | `2 2 6 3`     | `w w d e`     | con **Roll** scegli tra cane / band… |
| **donna**  | `3 6 6 6 2`   | `e d d d w`   | d-o-n-n-a |
| **giorno** | `4 4 6 7 6 6` | `a a d z d d` | g-i-o-r-n-o |

Poi **Spazio** per confermare e andare avanti, **Roll** (`F`) se la parola mostrata
non è quella giusta.

> ⚠️ La fila dei numeri in alto **1–5** non compone lettere: nell'assistente fa
> punteggiatura e *Write* (vedi la tabella in Parte 2). Le lettere si compongono
> **solo** con `w e a s d z x c`.

---

# Personalizzazione (facoltativa)

### Rimappare i tasti alle funzioni

L'assistente Windows legge all'avvio il file **`data/tasti.conf`** (accanto
all'eseguibile) e associa ogni **funzione** a uno o più **tasti**. Lo apri con un
editor di testo, lo modifichi, **salvi e riavvii**. Se il file manca, valgono i
default di fabbrica (identici a quelli scritti nel file distribuito).

Una riga per funzione, `funzione = tasto [altro_tasto ...]`, con `#` per i
commenti. Estratto:

```
conferma_continua = Spazio
cancella_lettera  = Backspace BlocMaiusc
scarta            = Esc
punto             = 1
roll        = F
conferma    = G
avanti      = R
apri        = T
naviga_prev = V
naviga_next = B
```

Funzioni disponibili: `conferma_continua`, `cancella_parola`, `cancella_lettera`,
`scarta`, `punto`, `virgola`, `domanda`, `esclamativo`, `write`, `read`, `roll`,
`conferma`, `avanti`, `apri`, `naviga_prev`, `naviga_next`. Tasti ammessi: una
lettera `A..Z`, una cifra `0..9`, o un nome speciale (`Spazio`, `Tab`,
`Backspace`, `BlocMaiusc`, `Esc`, `Backtick`). Le funzioni su tasti-**lettera**
sono attive solo in modalità **Assistita** e **Multi-tap** (in Classica quelle
lettere si digitano). I tasti-gruppo T9 `w e a s d z x c` sono riservati alla
digitazione delle lettere e **non** vanno riassegnati.

> Il dizionario e il numero di alternative mostrate (8) sono fissati nel programma
> (`app/windows/main.cpp`). Il file `data/config.json` **non** viene letto
> dall'assistente: è solo un esempio del formato per la [C ABI](#per-chi-sviluppa).

### Usare un altro dizionario

L'assistente carica il file **`data/wordlist_it.txt`**: per cambiare dizionario ne
sostituisci il contenuto (stesso nome file). Deve avere **una parola per riga**:
solo la parola, oppure `parola`<kbd>TAB</kbd>`frequenza`: le parole più frequenti
vengono proposte per prime (e messe per prime dal Roll quando più parole
condividono la stessa sequenza di tasti). Righe vuote e righe che iniziano con `#`
sono ignorate. Quello incluso ha ~49.000 parole italiane.

---

# Per chi sviluppa

Dettagli tecnici, utili solo a chi mette le mani nel codice. Vedi anche
`docs/ARCHITETTURA.md` (i tre livelli CORE/MOTORE/FE) e `docs/CORE-nuova-concezione.md`.

## Il modello (T9 word-centric)

Tutto ruota attorno alla singola **Parola**. Ogni pressione è un **tasto** del
keymap, cioè un **gruppo di lettere**; il dizionario cerca le parole della stessa
lunghezza le cui lettere stanno, posizione per posizione, nei gruppi premuti, e le
ordina per frequenza (col contesto, via modello a bigrammi). Il **Roll** cicla le
collisioni. Gli **spazi non esistono come dato**: il documento è una lista di parole
e lo spazio tra loro è calcolato al momento di disegnare il testo.

## Com'è fatto il progetto

Il codice è diviso in tre livelli: **CORE** (logica pura di matching/predizione,
stateless), **MOTORE** (macchina a stati sopra il CORE, condivisa fra i frontend) e
**FE** (lo strato sottile legato al sistema operativo). Così il cuore si scrive una
volta sola e ogni sistema aggiunge solo il suo adattatore.

| Cartella / file | A cosa serve |
|-----------------|--------------|
| `core/src/sohw/` | **CORE** «nuova concezione»: facade stateless (`core.cpp`), provider dei candidati T9/Literal, modello a bigrammi e predittore. Nessuna dipendenza dal sistema operativo. |
| `core/src/motore/` | **MOTORE**: macchina a stati (`engine.cpp`) — documento a parole, cursori selezione/aperta, render con spazi derivati, azioni (Roll/Conferma/Naviga/Punteggiatura/Read/Write) e disponibilità azioni. |
| `core/` (resto) | Libreria di base `onehand_core`: keymap, dizionario T9 (`computeCandidates`), utf8, alterazioni. Testabile da sola (`core/tests/`, suite CTest). |
| `core/include/onehand/predictor.hpp` | Interfaccia **`Predictor`** per il ranking dei candidati e la parola successiva. Pensata per agganciare più avanti un ranking neurale (n-gram / ONNX) senza toccare il CORE. |
| `app/windows/main.cpp` | **FE Windows** (`sohw_assistant`): hook tastiera globale, pannello, overlay, Read/Write via appunti. Chiama `motore::Engine` direttamente. |
| `data/wordlist_it.txt` | Dizionario italiano (parola`TAB`frequenza, da OpenSubtitles 2018). Caricato per nome dall'assistente. |
| `data/it.bigrams.bin` | Modello a bigrammi per il ranking contestuale e la parola successiva (generato offline; non versionato). Se assente → ranking a sola frequenza. |
| `data/tasti.conf` | Mappatura **tasto → funzione** letta dall'assistente Windows (vedi [Personalizzazione](#personalizzazione-facoltativa)). Opzionale: se assente valgono i default cablati. |
| `data/config.json` | **Esempio** del formato JSON per la **C ABI** (`wordlist`, `max_candidates`, `keymap.letters`). Non è letto dall'assistente Windows: lo interpreta `parseConfig` solo quando un host esterno gli passa quel testo. |

> C'è una **C ABI** opzionale, non necessaria al FE Windows (che chiama il C++
> direttamente), per pilotare il motore da altri linguaggi (C#, Rust, Swift…):
> `core/include/sohw/smartcore_c.h` (CORE «nuova concezione») e la più vecchia
> `core/include/onehand/onehand_c.h`. Entrambe ricevono la config come **testo JSON**
> (parser tollerante `parseConfig`); non aprono file da sole.

### In arrivo (rinviato)

Maiuscole automatiche a inizio frase, ranking **neurale** (l'interfaccia `Predictor`
è già pronta) e overlay/lettura ancorati al cursore reale via UIA. Backlog completo
in `docs/ARCHITETTURA.md` §6.

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

**Costruire anche le C ABI** (librerie condivise, opzionali):

```bat
cmake -B build -DONEHAND_BUILD_C_ABI=ON -DSOHW_BUILD_C_ABI=ON
cmake --build build --config Release
```
