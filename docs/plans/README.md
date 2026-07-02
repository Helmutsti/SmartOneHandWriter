# Piani (docs/plans)

Qui vivono **tutti i piani** del progetto SmartOneHandWriter, versionati col codice, così viaggiano
col repo e non restano solo sulla macchina di sviluppo.

## Convenzione
- I piani sono numerati in ordine cronologico (`01-`, `02-`, …) con un nome descrittivo.
- Quando si usa la **plan mode** di Claude Code (che scrive in `~/.claude/plans/` sul computer locale),
  al termine il piano va **copiato qui** in `docs/plans/` e committato. L'originale locale può essere
  rimosso: la copia nel repo è la fonte di verità.
- I piani sono storia/riferimento: non si riscrivono a posteriori. Lo stato *attuale* di un componente
  vive nei suoi documenti (es. `docs/CORE-nuova-concezione.md`, `docs/ARCHITETTURA.md`).

## Indice
- `01-t9-rewrite.md` — riscrittura del core dal modello wildcard al modello T9 word-centric.
- `02-core-nuova-concezione.md` — CORE stateless (matching + predittivo) della "nuova concezione".
- _(prossimo)_ piano del **FE / MOTORE** — in arrivo.
