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

## 1. Scarica e compila

Ti serve **Windows** e un compilatore C++ (i *Visual Studio Build Tools*, carico
di lavoro «Sviluppo C++»).

```bash
git clone <url-del-repo>
cd writer
```

Apri **«x64 Native Tools Command Prompt for VS»** in questa cartella e lancia:

```bash
build.bat
```

Crea `onehand.exe`. Fine: non c'è nessuna installazione, basta che `config.json`
e `wordlist_it.txt` restino nella stessa cartella dell'eseguibile.

**Per avviarlo:** doppio clic su `onehand.exe`. Compare una finestrella con un
pulsante **▶ Play**.

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

### Maiuscole e punteggiatura

Tre comodità che funzionano da sole:

- **Maiuscola automatica**: dopo un punto `.` `!` `?` (e a inizio testo), la
  prima lettera della parola successiva diventa **maiuscola**.
- **Punteggiatura**: a inizio parola premi **Tab**, scorri fino al segno che vuoi
  e conferma con **spazio ×2**. Il segno si **attacca alla parola precedente** e
  lo spazio passa dopo (`ciao,` e poi `ciao, come` — mai `ciao ,`).
- **Maiuscola di una lettera**: se hai scritto una **sola lettera**, con **Tab**
  scegli tra minuscola e **MAIUSCOLA** (es. `a` → `A`).

---

## 3. Personalizza: il file `config.json`

È un file di testo accanto all'eseguibile. Lo modifichi, **salvi e riavvii**
OneHand. Ecco com'è fatto:

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
| `wordlist` | Il dizionario da usare (un file di parole). |
| `wildcard_matches` | `"unavailable"` = il jolly è solo una lettera che non puoi digitare (consigliato). `"any"` = qualsiasi lettera. |
| `max_candidates` | Quante alternative mostrare quando premi Tab. |
| `punctuation` | I segni che compaiono (nell'ordine) quando apri la punteggiatura con Tab. |
| `double_press_ms` | Quanto sei veloce con la doppia pressione (millisecondi). Più alto = più tempo per il «×2». |

Regole rapide del formato: virgolette `"..."` per il testo, numeri senza
virgolette, virgola tra una voce e l'altra ma **non** dopo l'ultima.

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
per scrivere o cancellare in modo normale, premi **⏹ Stop**. Le scorciatoie con
**Ctrl / Alt / Win** funzionano sempre.
