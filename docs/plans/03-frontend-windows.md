# Piano 03 — Frontend Windows

> Piano del frontend Windows dell'assistente di digitazione. Il FE è un **adattatore sottile** di solo
> I/O (vedi `docs/ARCHITETTURA.md`): la logica di comportamento vive nel MOTORE condiviso (vedi
> `docs/plans/04-engine-state-improvements.md`, prodotto insieme a questo). Questo documento serve anche
> da **input per la macchina a stati** dell'engine.

## 1. Contesto e scopo
Piccola utility Windows che assiste la digitazione in **qualsiasi applicazione**: intercetta i tasti,
mantiene un **buffer** di testo mostrato in una finestrella in sovraimpressione, e scrive il testo
nell'app attiva a richiesta. Deve garantire un comportamento coerente (la logica sta nel MOTORE, non nel
FE) ed essere una *struttura di funzionamento*, non solo un tool di suggerimenti.

## 2. Decisioni prese (con l'utente)
- **Ambito**: system-wide. Un tasto di **input/load** legge il testo selezionato da qualunque app nel
  buffer; **complete** scrive tutto il buffer nell'app attiva e lo svuota. Modello a **buffer canonico**
  (il MOTORE possiede il testo; il FE inietta).
- **Overlay**: un **box colorato senza bordi né chrome** — solo testo ASCII semplice — vicino al punto di
  digitazione. **Sparisce quando il buffer è vuoto**.
- **inserisci automatico**: chiudere una parola apre auto una nuova parola **solo se è l'ultima della
  frase**; in mezzo, chiude e basta (la selezione avanza).
- **cancella**: **due tasti separati** ora (un tasto per funzione). Le primitive `DeleteLetter`/
  `DeleteWord` restano atomiche nell'engine; l'eventuale "cancella intelligente" sarà logica FE futura.
- **load**: il testo caricato è dato per **già completato**; le parole caricate sono *risolte* e possono
  essere **navigate, cancellate, completate/arricchite** (niente Roll: non hanno codifica T9; per
  riscriverne una si cancella e si ridigita).

## 3. Componenti UI
Due superfici **separate**:

### 3.1 Overlay del buffer (sovraimpressione)
- Finestra **topmost, senza bordi, click-through**, sfondo colorato pieno, testo ASCII del buffer intero.
- Evidenziazioni: **parola selezionata** = sfondo colore A; **parola aperta** = sfondo colore B (diverso).
- **Auto-hide** quando il buffer è vuoto; riappare al primo carattere/`load`.
- Posizione: sotto/sopra il caret dell'app attiva (vedi §7, questione aperta sul tracking del caret).

### 3.2 Pannello tasti (finestra separata)
Un **bottone per funzione** (vedi §4) + un **Play/Pause** che attiva/disattiva intercettazione+overlay.
Quando in pausa, i tasti passano all'app normalmente.

## 4. Funzioni → azioni dell'engine
| Bottone | Comportamento | Azione engine (proposta) |
|---|---|---|
| Naviga ◀ / ▶ | sposta la **selezione** tra le parole del testo | `NavigatePrev` / `NavigateNext` |
| Apri/Edit | apre la parola selezionata per l'editing (Roll, digitazione) | `OpenSelected` |
| Roll | cicla i suggerimenti della parola **aperta** | `Roll` |
| Cancella (open) | se una parola è aperta, cancella **una lettera** | `DeleteLetter` |
| Cancella (close) | se la parola selezionata è chiusa, cancella **l'intera parola** | `DeleteWord` |
| Inserisci | si sposta a destra e apre una **nuova parola** ovunque tu sia | `Insert` |
| Complete | scrive tutto il buffer nell'app attiva e **svuota** il buffer | `Complete` |
| Load | carica il testo selezionato dall'app nel buffer (parole risolte) | `Load(text)` |

Regola: la chiusura di una parola (Conferma) chiama `Insert` **in automatico solo se è l'ultima** della
frase (§2). Questa logica sta nell'engine, non nel FE.

## 5. Modalità di digitazione
Scelta utente: **assistita** o **classica**.
- **Assistita (T9)**: i **gruppi di lettere** sono mappati a tasti fisici configurabili — preset:
  **tastierino numerico** (`2..9`) oppure **`q,w,e,a,s,d,z,x,c`** (9 tasti → 9 gruppi). Il FE traduce il
  tasto fisico nel simbolo/gruppo e lo manda all'engine (che disambigua col dizionario+contesto).
- **Classica**: i tasti sono lettere dirette; l'engine usa il CORE in modalità Literal (completamento per
  prefisso); Roll cicla i completamenti. Conferma chiude la parola.

La mappa **tasto fisico → gruppo/lettera** è configurabile (default proposti sopra).

## 6. Interception & I/O (cuore del FE Windows)
- **Hook tastiera low-level** (`WH_KEYBOARD_LL`): quando attivo (Play), intercetta i tasti mappati e li
  traduce in azioni/lettere per l'engine invece di inviarli all'app; i tasti non mappati passano.
- **Iniezione** (`SendInput`) su `Complete`: scrive il testo del buffer nell'app attiva (con firma
  sugli eventi iniettati per non ri-catturarli, come nel legacy `main_win32.cpp`).
- **Load**: legge il **testo selezionato** dall'app attiva. Approccio proposto: simulare `Ctrl+C` e
  leggere la **clipboard** (semplice, universale), poi tokenizzare nel buffer come parole risolte.
- **Play/Pause**: installa/rimuove l'hook e mostra/nasconde overlay+pannello.

## 7. Rendering & stato
Il FE **non calcola** cosa evidenziare: chiede all'engine un **modello di render** (testo completo +
confini di parola + indice selezionato + indice aperto) e colora di conseguenza. Vedi il contratto nel
piano 04.

## 8. Assunzioni e questioni aperte
- **Posizione del caret** dell'app esterna: ottenerla system-wide è non banale (`GetGUIThreadInfo` /
  UIA). *Assunzione MVP*: overlay in posizione ancorata (es. vicino al cursore mouse o posizione fissa
  configurabile), con tracking del caret come miglioria successiva.
- **Load via clipboard**: usa `Ctrl+C`+clipboard (semplice) invece di UIA. Da confermare se accettabile.
- **Digitare quando nessuna parola è aperta**: *assunzione* → apre automaticamente una nuova parola
  (equivale a `Insert` implicito) così si può iniziare a digitare.
- **Toolkit UI**: Win32 puro (coerente col repo, zero dipendenze; overlay = `WS_EX_LAYERED |
  WS_EX_TRANSPARENT | WS_EX_TOPMOST`). Da confermare vs alternativa.

## 9. Milestone (proposta)
1. Overlay borderless topmost con testo del buffer + auto-hide; pannello tasti separato + Play/Pause.
2. Hook tastiera + pass-through; mappatura tasti→azioni/gruppi (preset numpad e QWEASDZXC).
3. Cablatura al MOTORE (C ABI): azioni dei bottoni + digitazione → render model → disegno overlay
   (selezionata/aperta colorate).
4. `Complete` (iniezione `SendInput`) e `Load` (clipboard) end-to-end.
5. Toggle assistita/classica; configurazione mappa tasti.
6. Rifiniture: posizione overlay/caret, persistenza config.

## 10. Puntatori
- Architettura: `docs/ARCHITETTURA.md`. Engine: `docs/plans/04-engine-state-improvements.md`.
- CORE (già pronto, in pausa): `docs/CORE-nuova-concezione.md`.
