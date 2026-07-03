/* SmartOneHandWriter - C ABI del MOTORE (macchina a stati).
 *
 * Contratto FFI snello e stabile, tutto UTF-8 (char*), per frontend in altri
 * linguaggi (Swift/AppKit su macOS). Avvolge motore::Engine 1:1 con le chiamate
 * che il frontend Windows (app/windows/main.cpp) fa in C++ diretto, cosi' il FE
 * macOS puo' replicare TUTTE le funzionalita' senza reimplementare la logica.
 *
 * Distinto da smartcore_c.h (che avvolge il CORE stateless sohw::Core): qui
 * l'oggetto e' STATEFUL (possiede il documento). Modello d'uso:
 *   mo = mo_create();
 *   mo_keymap_clear(mo); mo_keymap_add(mo,"w","abc"); ...   // keymap T9 del FE
 *   mo_set_config(mo, 8);           // AZZERA dizionario: PRIMA di load_*
 *   mo_load_wordlist(mo, path); mo_load_bigrams(mo, path);
 *   mo_set_mode(mo, 1);             // 1 = assistita (T9), 0 = classica (Literal)
 *   ... azioni (mo_type_key, mo_roll, ...) ...
 *   mo_render(mo);                  // ricalcola e CACHA il modello di render
 *   ... i getter (span, suggerimenti, availability) leggono la cache ...
 *   mo_destroy(mo);
 *
 * Le stringhe restituite dai getter puntano a memoria posseduta dall'oggetto e
 * restano valide fino alla successiva mo_render()/azione o a mo_destroy().
 */
#ifndef SOHW_MOTORE_C_H
#define SOHW_MOTORE_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mo_engine mo_engine;

/* Evidenziazione di uno span (mirror di motore::Highlight). */
enum { MO_HL_NONE = 0, MO_HL_SELECTED = 1, MO_HL_OPEN = 2 };

/* Azioni valide adesso (mirror di motore::Availability). 1 = valida, 0 = no-op. */
typedef struct mo_avail {
    int navPrev, navNext, open, roll, confirm, advance;
    int deleteLetter, deleteWord, punct, read, write, discard;
} mo_avail;

/* --- ciclo di vita / risorse -------------------------------------------- */
mo_engine* mo_create(void);
void       mo_destroy(mo_engine* mo);

/* Keymap T9: accumula i gruppi (key/letters UTF-8) prima di mo_set_config. */
void mo_keymap_clear(mo_engine* mo);
void mo_keymap_add(mo_engine* mo, const char* key_utf8, const char* letters_utf8);

/* Applica keymap accumulata + maxCandidates. ATTENZIONE: azzera dizionario e
 * modello -> chiamare PRIMA di mo_load_wordlist / mo_load_bigrams. */
void mo_set_config(mo_engine* mo, int max_candidates);

/* Caricano da file (I/O lato C). Ritornano 1 in caso di successo, 0 altrimenti. */
int  mo_load_wordlist(mo_engine* mo, const char* path);
int  mo_load_bigrams(mo_engine* mo, const char* bin_path);

/* 1 = assistita (T9), 0 = classica (Literal). */
void mo_set_mode(mo_engine* mo, int assisted);
int  mo_assisted(const mo_engine* mo);

/* --- documento ---------------------------------------------------------- */
void mo_load_resolved(mo_engine* mo, const char* utf8_text);  /* Read (da clipboard) */
void mo_clear(mo_engine* mo);                                 /* Scarta */
int  mo_empty(const mo_engine* mo);

/* --- azioni ------------------------------------------------------------- */
void mo_type_key(mo_engine* mo, const char* sym_utf8);
void mo_navigate_prev(mo_engine* mo);
void mo_navigate_next(mo_engine* mo);
void mo_open_selected(mo_engine* mo);
void mo_roll(mo_engine* mo);
void mo_confirm(mo_engine* mo);
void mo_advance(mo_engine* mo);
void mo_confirm_continue(mo_engine* mo);
void mo_punct(mo_engine* mo, const char* sym_utf8);
void mo_delete_letter(mo_engine* mo);
void mo_delete_word(mo_engine* mo);
void mo_accept_suggestion(mo_engine* mo, int k);

/* Write: conferma l'aperta, svuota il buffer e ritorna il testo completo (UTF-8).
 * La stringa e' posseduta dall'oggetto (valida fino alla prossima chiamata). */
const char* mo_write(mo_engine* mo);

/* --- render (snapshot cachato) ------------------------------------------ */
/* Ricalcola il modello di render e lo cacha nell'oggetto. Chiamare dopo ogni
 * azione, prima di leggere gli span/suggerimenti/availability. */
void mo_render(mo_engine* mo);

/* Span del testo (dalla cache dell'ultimo mo_render). */
size_t      mo_span_count(const mo_engine* mo);
const char* mo_span_text(const mo_engine* mo, size_t i);          /* "" se fuori range */
int         mo_span_hl(const mo_engine* mo, size_t i);            /* MO_HL_* */
int         mo_span_space_before(const mo_engine* mo, size_t i);  /* 1/0 */
int         mo_span_typed(const mo_engine* mo, size_t i);         /* n. lettere digitate */

/* Riga suggerimenti (dalla cache). */
size_t      mo_suggestion_count(const mo_engine* mo);
const char* mo_suggestion_text(const mo_engine* mo, size_t i);    /* "" se fuori range */
int         mo_suggestion_sel(const mo_engine* mo);               /* -1 = nessuna */
int         mo_suggestions_are_next(const mo_engine* mo);         /* 1 = next-word */

/* Availability (dalla cache) nell'out-struct. */
void mo_availability(const mo_engine* mo, mo_avail* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOHW_MOTORE_C_H */
