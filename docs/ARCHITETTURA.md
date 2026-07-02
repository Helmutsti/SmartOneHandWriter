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

**Cablatura scelta**: il FE C++ chiama `motore_core` **direttamente** (niente C ABI dedicata del MOTORE);
la C ABI del CORE (`smartcore_c`) resta per i linguaggi terzi. Build: `cmake -B build && cmake --build
build --config Release` → `build/app/windows/Release/sohw_assistant.exe`; suite test 7/7 verde.

## 6. Backlog / evoluzioni future

Consolidato dai piani (ora completati e rimossi; storia in git). Per priorità:

**FE / MOTORE**
- Editor UI di rimappatura tasti (fisico ↔ gruppo/funzione) + preset **tastierino numerico** (F16).
- Attivazione delle funzioni **solo da tastiera** (obiettivo uso a una mano); doppio-tap per funzioni
  extra; **hotkey globale** per Play/Pause (F17/F18). Oggi Play/Pause e le funzioni sono anche su bottoni.
- **Overlay near-caret** e `Read`/selezione via **UIA** (posizione reale del cursore dell'app), invece
  del near-mouse e della clipboard (E13/D10).
- **Iniezione incrementale** in-app via `Effects`/diff (oggi `Write` incolla il buffer "a blocco").
- **Maiuscole** (G20): auto a inizio frase (dopo `. ! ?`) + tasto "Maiuscola"; oggi tutto minuscolo.
- **C ABI del MOTORE** (`motore_c.h`) per frontend non-C++ (macOS/altro), se e quando servirà.
- Persistenza della config del FE (modalità, keymap).

**CORE** (dettagli in `CORE-nuova-concezione.md` §21.4)
- Trigrammi o backend **BERT** dietro `IPredictor` (contesto profondo/bidirezionale).
- Taratura pesi interpolazione e pruning; smoothing (Kneser-Ney) al posto dell'interpolazione lineare.
- Esporre i parametri di ranking da `config.json` (oggi solo via API `Core::set*`).
- Allineare il tokenizer del CORE alla Strategia A degli apostrofi del MOTORE.

**Portabilità / packaging**
- Verifica build su Linux/macOS (il CORE è portabile).
- Distribuzione dell'assistente: oggi i dati sono trovati via `SOHW_DATA_DIR` assoluto (build-tree);
  per un pacchetto standalone i dati (`wordlist_it.txt`, `it.bigrams.bin`) vanno accanto all'eseguibile.
