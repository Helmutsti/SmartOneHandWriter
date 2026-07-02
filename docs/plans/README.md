# Piani (docs/plans)

Qui vivono i **piani** del progetto SmartOneHandWriter, versionati col codice, così viaggiano
col repo e non restano solo sulla macchina di sviluppo.

## Convenzione
- I piani sono numerati in ordine cronologico (`01-`, `02-`, …) con un nome descrittivo.
- Quando si usa la **plan mode** di Claude Code (che scrive in `~/.claude/plans/` sul computer locale),
  al termine il piano va **copiato qui** in `docs/plans/` e committato. L'originale locale può essere
  rimosso: la copia nel repo è la fonte di verità.
- I piani sono storia/riferimento: non si riscrivono a posteriori. Lo stato *attuale* di un componente
  vive nei suoi documenti (es. `docs/CORE-nuova-concezione.md`, `docs/ARCHITETTURA.md`).
- **Piani completati**: quando un piano è interamente realizzato, il suo contenuto vivo (stato + backlog
  residuo) confluisce nei documenti di stato e il file di piano viene **rimosso** (resta nella storia git).

## Indice
Nessun piano attivo: i piani `01`–`04` (riscrittura core T9, CORE stateless, FE Windows, MOTORE
macchina a stati) sono **completati** e rimossi. Il risultato è la prima **versione avviabile**
dell'assistente Windows; stato e backlog residuo in `docs/ARCHITETTURA.md` (§5–§6) e, per il CORE,
`docs/CORE-nuova-concezione.md`. Lo storico dei piani è recuperabile da git (`git log -- docs/plans/`).
