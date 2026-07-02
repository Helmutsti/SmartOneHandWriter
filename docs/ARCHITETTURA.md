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

## 5. Stato

- **CORE**: implementato e testato (M1–M7 + gruppo A + prestazioni), **in pausa**. Vedi
  `CORE-nuova-concezione.md`.
- **MOTORE / FE**: in fase di pianificazione (il piano del FE arriva a breve e finirà in
  `docs/plans/`). Questo documento fissa la decisione strutturale di fondo.
