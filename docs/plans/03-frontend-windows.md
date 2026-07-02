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
- **Ambito**: system-wide. **Read** carica nel buffer il testo **presente nella clipboard** (l'utente
  copia a mano con `Ctrl+C` prima); **Write** scrive tutto il buffer nell'app attiva (via **clipboard +
  `Ctrl+V`**, con salvataggio/ripristino della clipboard) e lo svuota. Modello a **buffer canonico**
  (il MOTORE possiede il testo; il FE inietta).
- **Overlay**: un **box colorato senza bordi né chrome** — solo testo ASCII semplice — vicino al punto di
  digitazione. **Sparisce quando il buffer è vuoto**.
- **conferma / avanti / conferma continua**: la chiusura di una parola è **Conferma** (chiude, resta
  sul posto); **Avanti** sposta a destra e apre una nuova parola; **Conferma continua** è la combo
  (Conferma+Avanti) che **scatta in automatico a fine frase** (delimitata da punteggiatura `. ! ?`) ed è
  anche un bottone dedicato. In mezzo al testo si usa Conferma da sola.
- **cancella**: **due tasti separati** ora (un tasto per funzione). Le primitive `DeleteLetter`/
  `DeleteWord` restano atomiche nell'engine; l'eventuale "cancella intelligente" sarà logica FE futura.
- **read/load**: il testo caricato è dato per **già completato**; le parole caricate sono *risolte* e
  possono essere **navigate, cancellate, arricchite** (niente Roll: non hanno codifica T9). Per
  cambiarne una: `Cancella close` + ridigita. **Non si digita dentro una parola caricata** (D11-ii);
  arricchire = inserire parole nuove intorno.

## 3. Componenti UI
Due superfici **separate**:

### 3.1 Overlay del buffer (sovraimpressione)
- Finestra **topmost, senza bordi, click-through**, sfondo colorato pieno, testo ASCII del buffer.
- **Riga singola con scorrimento** (E14): il box resta basso e scorre orizzontalmente per tenere sempre
  visibile la **parola aperta**.
- Evidenziazioni (E15, default): sfondo box **grigio scuro semi-trasparente** (~#2B2B2B, ~85%), testo
  bianco; **parola selezionata** = azzurro tenue (#3A6EA5); **parola aperta** = ambra (#D9A441). Tutto
  poi configurabile.
- **Auto-hide** quando il buffer è vuoto; riappare al primo carattere / `Read`.
- **Posizione: vicino al cursore del mouse** (E13). Il near-caret (via UIA) è rinviato (§11).

### 3.2 Pannello tasti (finestra separata)
Un **bottone per funzione** (vedi §4) + un **Play/Pause** che attiva/disattiva intercettazione+overlay.
Quando in pausa, i tasti passano all'app normalmente.
- **Ora**: tutte le funzioni sono su **bottoni**; Play/Pause **solo bottone UI**.
- **Rinviato (§11)**: attivazione delle funzioni **solo da tastiera** (obiettivo per l'uso a una mano),
  eventuale **doppio-tap** di un tasto per funzioni extra, e **hotkey globale** per Play/Pause.

## 4. Funzioni → azioni dell'engine
| Bottone | Comportamento | Azione engine |
|---|---|---|
| Naviga ◀ / ▶ | sposta la **selezione** tra i token, **inclusa la punteggiatura** (l'apostrofo resta dentro la parola, vedi piano 04 §1.1) | `NavigatePrev` / `NavigateNext` |
| Apri/Edit | evidenzia la parola selezionata e abilita **digitazione/Roll** | `OpenSelected` |
| Roll | cicla i suggerimenti della parola **aperta** | `Roll` |
| Cancella open | parola **aperta**: cancella **una lettera** | `DeleteLetter` |
| Cancella close | parola **chiusa** selezionata: cancella **l'intera parola** | `DeleteWord` |
| Conferma | conferma/chiude la parola aperta (**resta** sul posto) | `Confirm` |
| Avanti | dopo la conferma: si sposta a destra e **apre una nuova parola** | `Advance` |
| Conferma continua | combo **Conferma+Avanti**; automatica a fine frase, o manuale | `ConfirmContinue` |
| Write | scrive tutto il buffer nell'app attiva e **svuota** | `Write` |
| Read | carica nel buffer il testo selezionato (parole risolte) | `Read(text)` |

Regola: **Conferma continua** scatta in automatico quando la parola si trova a **fine frase**
(delimitata da punteggiatura); altrimenti **Conferma** chiude soltanto e si usa **Avanti** per
proseguire. Questa logica sta nell'engine, non nel FE.

## 5. Modalità di digitazione
Scelta utente: **assistita** o **classica**.
- **Assistita (T9)**: i **gruppi di lettere** sono mappati a tasti fisici configurabili — preset:
  **tastierino numerico** (`2..9`) oppure **`q,w,e,a,s,d,z,x,c`** (9 tasti → 9 gruppi). Il FE traduce il
  tasto fisico nel simbolo/gruppo e lo manda all'engine (che disambigua col dizionario+contesto).
- **Classica**: i tasti sono lettere dirette; l'engine usa il CORE in modalità Literal (completamento per
  prefisso). Il **Roll cicla i completamenti**, ma il **primo elemento è sempre il testo digitato**
  (letterale): `Conferma` **senza** rollare mantiene ciò che hai scritto; rollare + `Conferma` scrive il
  completamento scelto (B6).

La mappa **tasto fisico → gruppo/lettera deve essere configurabile** (F16). **Per ora** si usa il blocco
**`q w e a s d z x c`** come default fisso; l'**editor di mapping** (UI per rimappare tasti↔gruppi, e il
preset numpad) è **rinviato** (§11).

**Punteggiatura e spazi (G19):**
- **Spazi**: non si digitano — lo spazio tra parole è derivato al render; a una nuova parola ci si arriva
  con **Avanti** / **Conferma continua**.
- **Punteggiatura**: **tasti dedicati** per i segni frequenti (almeno `.` `,` `?` `!`) che inseriscono un
  token `Punct`; i segni meno comuni via **modalità simboli** (set esatto rifinibile). Premere un segno
  **conferma** l'eventuale parola aperta e aggiunge il token; se il segno è **terminale** (`. ! ?`)
  scatta la **Conferma continua** (fine frase, §3 del piano 04).
- **Apostrofo**: non si digita — arriva dalle parole elise del dizionario (Strategia A, §1.1 piano 04).

### 5.1 Keymap di default (proposta, assistita, mano sinistra)
Vincolo: i tasti funzione sono **non-lettere** dove devono valere anche in **classica** (lì le lettere si
digitano). In **assistita** le lettere attorno al blocco sono libere (solo i 9 gruppi sono occupati).

Blocco gruppi T9 (fisso): `q w e / a s d / z x c` → 9 gruppi.

| Tasto | Funzione |
|---|---|
| **Spazio** | Conferma continua (finisci parola + vai) |
| **F** | Roll |
| **G** | Conferma (chiude, resta) |
| **R** | Avanti (apre nuova a destra) |
| **T** | Apri/Edit |
| **V** / **B** | Naviga ◀ / ▶ |
| **Bloc Maiusc** | Cancella open (lettera) |
| **Tab** | Cancella close (parola) |
| **1 / 2 / 3 / 4** | Punct `.` `,` `?` `!` (valida anche in classica) |
| **5** | Write |
| **` (grave)** | Read |
| **Ctrl+Alt** | Play/Pause *(rinviato: per ora bottone UI)* |
| **Shift** | *(riservato)* Maiuscola *(rinviato)* |

Loop assistita: gruppi `qweasdzxc` → **F** (Roll) → **Spazio** (conferma+continua), tutto a una mano.
**Classica**: `R T F G V B` sono lettere → quelle funzioni via **bottoni** o remappate su non-lettere;
restano valide in entrambe **Spazio, Tab, Bloc Maiusc, fila numeri, grave**. Tutto **configurabile**
(editor di mapping in §10). Questa è una **proposta di default**, non vincolante.

## 6. Interception & I/O (cuore del FE Windows)
- **Hook tastiera low-level** (`WH_KEYBOARD_LL`): quando attivo (Play), intercetta i tasti mappati e li
  traduce in azioni/lettere per l'engine invece di inviarli all'app; i tasti non mappati passano.
- **Iniezione** su `Write` (deciso D12 = **clipboard + incolla**): l'engine rende il buffer in stringa
  (regole §1.1), il FE **salva** la clipboard corrente, ci mette il testo, simula **`Ctrl+V`** (via
  `SendInput`, con firma sugli eventi iniettati) e **ripristina** la clipboard. Nessuna digitazione
  carattere-per-carattere.
- **Read** (deciso D10 = opzione 3): legge il **testo attualmente nella clipboard** (l'utente ha copiato
  a mano con `Ctrl+C`), poi tokenizza nel buffer come parole risolte (Strategia A, §1.1 del piano 04).
  Nessuna simulazione di tasti né UIA. (UIA per selezione+caret resta una possibile evoluzione futura.)
- **Play/Pause**: installa/rimuove l'hook e mostra/nasconde overlay+pannello.

## 7. Rendering & stato
Il FE **non calcola** cosa evidenziare: chiede all'engine un **modello di render** (testo completo +
confini di parola + indice selezionato + indice aperto) e colora di conseguenza. Vedi il contratto nel
piano 04.

## 8. Assunzioni e questioni aperte
- **Posizione overlay** (deciso E13): **vicino al cursore del mouse**. Il near-caret (via UIA) è
  rinviato (§10).
- ~~Load via clipboard~~ → **deciso (D10, opzione 3)**: `Read` legge la clipboard corrente (copia manuale).
- **Digitare quando nessuna parola è aperta** → apre automaticamente una nuova parola (confermato B4).
- **`Avanti`/nuova parola** viene creata **subito dopo** la parola corrente/selezionata (confermato B5).
- **Toolkit UI**: Win32 puro (coerente col repo, zero dipendenze; overlay = `WS_EX_LAYERED |
  WS_EX_TRANSPARENT | WS_EX_TOPMOST`). Da confermare vs alternativa.

## 9. Milestone (proposta)
1. Overlay borderless topmost con testo del buffer + auto-hide; pannello tasti separato + Play/Pause.
2. Hook tastiera + pass-through; mappatura tasti→azioni/gruppi (preset numpad e QWEASDZXC).
3. Cablatura al MOTORE (C ABI): azioni dei bottoni + digitazione → render model → disegno overlay
   (selezionata/aperta colorate).
4. `Write` (iniezione `SendInput`) e `Read` (clipboard) end-to-end.
5. Toggle assistita/classica; configurazione mappa tasti.
6. Rifiniture: posizione overlay/caret, persistenza config.

## 10. Rinviato / evoluzioni future (FE)
- **Editor di mapping tasti** (UI per rimappare fisico↔gruppo/funzione) + preset **numpad** (F16).
- **Attivazione delle funzioni solo da tastiera** — obiettivo finale per l'uso a una mano (i bottoni
  restano come fallback/debug) (F17).
- **Doppio-tap** di un tasto per funzioni aggiuntive (da valutare) (F17).
- **Hotkey globale** per Play/Pause (F18).
- **Overlay near-caret** e **`Read`/selezione via UIA** (posizione del cursore dell'app) (E13/D10).
- **Iniezione incrementale** in-app via `Effects`/diff (oggi `Write` è "a blocco" via clipboard).
- **Maiuscole** (G20): auto a inizio frase (dopo `. ! ?`) + tasto "Maiuscola" manuale; per ora tutto minuscolo.

## 11. Puntatori
- Architettura: `docs/ARCHITETTURA.md`. Engine: `docs/plans/04-engine-state-improvements.md`.
- CORE (già pronto, in pausa): `docs/CORE-nuova-concezione.md`.
