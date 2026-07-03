# SmartOneHandWriter — Architettura del sistema

> Documento di architettura complessiva e delle **decisioni strutturali** trasversali (CORE, MOTORE,
> Frontend). Per il dettaglio del CORE vedi `CORE-nuova-concezione.md`; per lo storico dei piani
> `docs/plans/`.

## 1. I tre livelli

```
┌─────────────────────────────────────────────────────────────────────┐
│ FRONTEND (per piattaforma) — SOLO I/O nativo                          │
│   cattura tasti · iniezione testo · disegno popup/overlay · permessi  │
└───────────────▲───────────────────────────────────┬──────────────────┘
                │ azioni (semantiche)                │ Effects (diff, popup, segnali)
┌───────────────┴───────────────────────────────────▼──────────────────┐
│ MOTORE (C++ condiviso) — macchina a stati + documento + regole editing│
│   (stato, azione) -> (nuovo stato, Effects). Fonte unica del comportam.│
└───────────────▲───────────────────────────────────┬──────────────────┘
                │ contesto + parola codificata       │ match + predizioni
┌───────────────┴───────────────────────────────────▼──────────────────┐
│ CORE (C++ condiviso, stateless) — matching + predittivo               │
│   Core::process(contesto, codificata, modo) -> match + next-word       │
└───────────────────────────────────────────────────────────────────────┘
```

Tutto ciò che è **logica/comportamento** sta nel C++ condiviso (CORE + MOTORE) ed è esposto via **C ABI**.
Il frontend è un **adattatore sottile** che fa solo I/O specifico della piattaforma.

## 2. Decisione: il MOTORE sta nel layer condiviso (NON nel FE)

**Il MOTORE è implementato una sola volta in C++, attorno/sopra il CORE, ed esposto via C ABI. NON va
reimplementato in ciascun frontend.**

Motivazioni:
- **Coerenza garantita**: una sola implementazione ⇒ comportamento identico su tutte le piattaforme.
  Reimplementare per FE porta a deriva e bug divergenti — l'esatto rischio da evitare.
- **Testabilità headless**: una macchina a stati pura `(stato, azione) → (nuovo stato, Effects)` si
  testa senza GUI, in CI. Logica sparsa nei FE non è testabile in modo uniforme.
- **Fonte unica delle regole**: composizione, Roll/ciclo alternative, conferma, cancellazioni,
  spaziatura, punteggiatura, capitalizzazione — definite una volta.
- **Struttura, non un tool**: così ai FE si fornisce un *funzionamento* coerente, non solo suggerimenti.
- **Precedente in repo**: il legacy `core/src/engine.cpp` è già questa forma (stato + `Effects` a diff
  minimo + C ABI, con Win32/AppKit come adapter). Il nuovo MOTORE starà sopra il CORE stateless.

### Confine di responsabilità

| Layer condiviso (CORE + MOTORE) | Frontend per piattaforma |
|---|---|
| Macchina a stati, documento, regole di editing | Cattura tasti (hook / IME) |
| Chiamate al CORE (match + predizioni) | Iniezione testo (SendInput / AppKit / Accessibility) |
| `Effects` = diff minimo (N backspace + inserimento) + popup + segnali | Disegno popup/overlay nativi |
| Keymap e semantica delle azioni | Solo *quale tasto fisico → quale azione* (config utente) |

Il FE decide "Tab = Roll", **non** cosa fa il Roll.

## 3. Contratto Effects (diff-based)

Il MOTORE possiede il testo canonico ed emette un **diff minimo** da applicare: un numero di
backspace seguito dal testo da inserire (più stato popup e segnali come pass-through). Il FE applica il
diff con le API native — contratto indipendente dalla piattaforma. (Vedi `Effects` in
`core/include/onehand/types.hpp` del MOTORE legacy come riferimento.)

## 4. Nodi di progettazione aperti (da decidere per il piano del FE/MOTORE)

1. **Ambito d'uso**: solo **editor interno** (finestra propria, controllo totale) oppure anche
   **system-wide** (scrivere in app di terzi via iniezione)? Incide molto sulla complessità del FE.
2. **Modello di contesto** — chi conosce il testo di destinazione:
   - **(A) Buffer canonico + iniezione diff**: il MOTORE possiede il testo, il FE inietta
     backspace+inserimenti. Semplice, portabile, funziona ovunque; rischio di disallineamento se
     l'utente edita a mano nell'app esterna. *(usato dal legacy; consigliato come default)*
   - **(B) Lettura dall'app**: il MOTORE legge testo circostante/caret via API di accessibilità.
     Robusto ma fortemente per-piattaforma e con permessi (Accessibility su macOS, UIA su Windows).
3. **Piattaforme target** e ordine (Windows, macOS, altro) → dimensiona l'adapter.

Orientamento iniziale suggerito: **(A)** buffer canonico, con un **editor interno** come contesto
primario e iniezione best-effort verso app esterne; ambito e piattaforme da confermare.

**Risolto (stato attuale)**: ambito **system-wide** su entrambe le piattaforme (hook globale +
iniezione via clipboard/Cmd+V/Ctrl+V), modello di contesto **(A)** buffer canonico. Piattaforme target:
**Windows** (`app/windows`) e **macOS** (`app/macos`).

## 5. Stato — versione avviabile ✅

I tre livelli sono implementati e la prima **versione avviabile** dell'assistente Windows esiste.

- **CORE** (`sohw::Core`): implementato e testato (M1–M7 + gruppo A + prestazioni). Vedi
  `CORE-nuova-concezione.md`.
- **MOTORE** (`motore::Engine`, `core/src/motore/`): macchina a stati sopra il CORE. Implementate
  navigazione/apertura/Roll, digitazione (T9 e classica "letterale primo"), Confirm/Advance/
  ConfirmContinue, Punct (con conferma continua automatica a fine frase), DeleteLetter/DeleteWord,
  Read (`loadResolved`) e Write. Tokenizzazione Strategia A + regole di spaziatura. Test `motore_tests`
  verdi (M1–M4 + integrazione reale col CORE).
- **FE Windows** (`app/windows/`, target `sohw_assistant`): adattatore di solo I/O sopra `motore_core`.
  Tre modalità: Assistita (T9), Classica, Multi-tap (scorrimento lettere lato FE su modo Literal, per
  parole fuori dizionario). Pannello (un bottone per funzione + Play/Pause + toggle Modalità), overlay topmost
  fisso e trascinabile (WS_EX_NOACTIVATE, drag via HTCAPTION) con evidenziazione selezionata/aperta
  e auto-hide a buffer vuoto, hook tastiera
  `WH_KEYBOARD_LL` (keymap stile cellulare `weasdzxc` = tasti 2–9, Spazio = 0; + funzioni;
  Esc = Scarta), Read/Write via clipboard (+`Ctrl+V`).
- **Mappatura tasto→funzione esternalizzata**: il FE non ha più il binding cablato nello `switch`;
  legge all'avvio `data\tasti.conf` (`loadBindings`, con fallback `defaultBindings` se il file manca),
  costruisce `g_keyToAct` (VK→azione) e in `handleKeyDown` traduce il tasto premuto nel comando da
  inviare al MOTORE. Formato `funzione = tasto [tasto...]` con commenti `#`; più tasti sulla stessa
  funzione; i tasti-lettera fanno da funzione solo in Assistita/Multi-tap (in Classica si digitano).
  I tasti-gruppo T9 (`weasdzxc`) restano gestiti a parte (input lettere), non nel file.
- **Attivazione bottoni per stato**: il MOTORE espone in `RenderModel.actions` (`motore::Availability`)
  quali comandi hanno senso nello stato corrente (calcolo in `Engine::availability`, riusa i
  suggerimenti del render). Il FE, a ogni `refreshOverlay`, chiama `updateButtonStates` →
  `EnableWindow` sui bottoni: quelli non applicabili diventano grigi mentre si scrive (es. Roll solo
  con ≥2 candidati/next-word, Naviga ▶ solo se non sei all'ultima parola, Canc. lettera solo con
  parola aperta). Logica condivisa nel MOTORE, non nel FE. Coperto da `motore_tests`.

**Cablatura scelta (Windows)**: il FE C++ chiama `motore_core` **direttamente** (niente marshalling);
la C ABI del CORE (`smartcore_c`) resta per i linguaggi terzi. Build: `cmake -B build && cmake --build
build --config Release` → `build/app/windows/Release/sohw_assistant.exe`.

- **FE macOS** (`app/macos/`, Swift + AppKit): stesso adattatore di solo I/O, ma verso `motore_core`
  attraverso la **C ABI del MOTORE** `motore_c` (`core/include/motore/motore_c.h`, target statico
  `motore_c`), dato che Swift non può linkare il C++ direttamente. Assistente di sistema completo:
  hook globale via `CGEventTap` (equiv. `WH_KEYBOARD_LL`, richiede il permesso **Accessibilità**),
  incolla in app terze via Cmd+V sintetico marcato come iniettato, overlay `NSPanel .nonactivatingPanel`
  (non ruba il focus), pannello `NSButton` con abilitazione da `Availability`, tre modalità e binding da
  `data/tasti.conf` (nomi di tasto tradotti nei keycode macOS). Mappa 1:1 di `app/windows/main.cpp`; vedi
  `app/macos/README.md`. Build con `app/macos/build_macos.sh` (fuori Mac App Store: l'event tap che
  assorbe tasti è incompatibile con la sandbox). La C ABI è coperta da `motore_c_tests`.

## 6. Backlog / evoluzioni future

Consolidato dai piani (ora completati e rimossi; storia in git). Per priorità:

**FE / MOTORE**
- Editor UI di rimappatura tasti (fisico ↔ gruppo/funzione) + preset **tastierino numerico** (F16).
  Base già pronta: la mappa tasto→funzione è nel file `data\tasti.conf` (vedi §5); un editor UI
  scriverebbe quel file. Manca ancora la rimappatura dei **tasti-gruppo T9** (`weasdzxc`), oggi cablati
  in `buildKeymap()`.
- Attivazione delle funzioni **solo da tastiera** (obiettivo uso a una mano); doppio-tap per funzioni
  extra; **hotkey globale** per Play/Pause (F17/F18). Oggi Play/Pause e le funzioni sono anche su bottoni.
- **Overlay near-caret** e `Read`/selezione via **UIA** (posizione reale del cursore dell'app), invece
  del near-mouse e della clipboard (E13/D10).
- **Iniezione incrementale** in-app via `Effects`/diff (oggi `Write` incolla il buffer "a blocco").
- **Maiuscole** (G20): auto a inizio frase (dopo `. ! ?`) + tasto "Maiuscola"; oggi tutto minuscolo.
- ✅ **C ABI del MOTORE** (`motore_c.h`) per frontend non-C++: fatta; usata dal FE macOS (Swift).
- Persistenza della config del FE (modalità, keymap).

**CORE** (dettagli in `CORE-nuova-concezione.md` §21.4)
- Trigrammi o backend **BERT** dietro `IPredictor` (contesto profondo/bidirezionale).
- Taratura pesi interpolazione e pruning; smoothing (Kneser-Ney) al posto dell'interpolazione lineare.
- Esporre i parametri di ranking da `config.json` (oggi solo via API `Core::set*`).
- Allineare il tokenizer del CORE alla Strategia A degli apostrofi del MOTORE.

**Portabilità / packaging**
- Verifica build su Linux/macOS (il CORE è portabile).
- ✅ Distribuzione standalone dell'assistente Windows: fatta (vedi §7). Runtime statico + dati
  risolti accanto all'exe; nessun redistributable né `SOHW_DATA_DIR` sul PC di destinazione.

## 7. Deploy (assistente Windows)

**Cosa distribuire** (pacchetto ricollocabile, ~20 MB):
```
sohw_assistant.exe        ← runtime C++ statico: nessuna DLL/redistributable da installare
data\
  wordlist_it.txt         (dizionario)
  it.bigrams.bin          (modello bigrammi; se assente → ranking a sola frequenza, niente next-word)
  tasti.conf              (mappatura tasto→funzione del FE; opzionale: se assente valgono i default)
```
- La cartella `data\` va **accanto all'exe**. `main.cpp::dataPath()` risolve i file relativamente alla
  cartella dell'eseguibile (`GetModuleFileNameW`), non alla working directory; se lì non trova i dati
  ripiega sul path `SOHW_DATA_DIR` compilato (utile solo in build-tree di sviluppo).
- `tasti.conf` è **opzionale**: l'assistente lo legge all'avvio per mappare i tasti alle funzioni; se
  manca (o è illeggibile) usa i default di fabbrica cablati (`defaultBindings()`), identici al file
  distribuito. Includerlo permette all'utente di rimappare i tasti senza ricompilare.
- `config.json` **non** va incluso: l'assistente non lo legge (keymap e config sono in `main.cpp`).
- Target **Windows x64**. Dipendenze residue: solo `USER32/GDI32/KERNEL32` (DLL di sistema).

**Come produrre la build di distribuzione** (separata dalla build di sviluppo, con runtime statico):
```
cmake -S <repo> -B C:\shwb-dist -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
      -DSOHW_BUILD_TOOLS=OFF -DSOHW_BUILD_GUI=OFF
cmake --build C:\shwb-dist --config Release --target sohw_assistant
```
Poi assembla il pacchetto: copia `C:\shwb-dist\app\windows\Release\sohw_assistant.exe` e la cartella
`data\` (`wordlist_it.txt`, `it.bigrams.bin`) in una cartella pulita, zippala e spediscila.
Verifica: `dumpbin -dependents sohw_assistant.exe` deve elencare solo le 3 DLL di sistema.
