/* SmartOneHandWriter - C ABI del CORE "nuova concezione".
 *
 * Contratto FFI snello e stabile, tutto UTF-8 (char*), per frontend in altri
 * linguaggi (Swift/C#/Rust/Python). Separato dal legacy onehand_c.h (che e'
 * legato alla macchina a stati e ai suoi enum "mai rinumerare").
 *
 * Modello: si crea un core, si caricano dizionario e (opzionale) modello
 * bigrammi, poi si chiama sc_process() per ogni (contesto, parola codificata).
 * Il risultato e' un oggetto opaco da interrogare e poi liberare.
 */
#ifndef SOHW_SMARTCORE_C_H
#define SOHW_SMARTCORE_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sc_core   sc_core;
typedef struct sc_result sc_result;

/* config_json puo' essere NULL (default). */
sc_core* sc_create(const char* config_json);
void     sc_destroy(sc_core* core);

/* mode: 0 = T9 (matching attivo), 1 = Literal (digitazione classica). */
void sc_set_mode(sc_core* core, int mode);
int  sc_get_mode(const sc_core* core);

/* Caricano da file (I/O lato C). Ritornano 1 in caso di successo, 0 altrimenti. */
int sc_load_wordlist(sc_core* core, const char* path);
int sc_load_bigrams(sc_core* core, const char* bin_path);

/* Elabora contesto + parola codificata. left/right/encoded sono UTF-8 (NULL = "").
 * topK: quanti match ricevono le predizioni di parola successiva (<0 = tutti).
 * nextN: quante parole successive per match. Il risultato va liberato con
 * sc_free_result(). Ritorna NULL solo se 'core' e' NULL. */
sc_result* sc_process(sc_core* core, const char* left, const char* right,
                      const char* encoded, int topK, int nextN);
void       sc_free_result(sc_result* res);

/* (3) match decodificati/ordinati. */
size_t      sc_match_count(const sc_result* res);
const char* sc_match_word(const sc_result* res, size_t i);   /* "" se fuori range */
float       sc_match_score(const sc_result* res, size_t i);  /* 0 se fuori range */

/* (4) ventaglio di parole successive per il match 'matchIdx'. */
size_t      sc_next_count(const sc_result* res, size_t matchIdx);
const char* sc_next_word(const sc_result* res, size_t matchIdx, size_t j);
float       sc_next_score(const sc_result* res, size_t matchIdx, size_t j);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOHW_SMARTCORE_C_H */
