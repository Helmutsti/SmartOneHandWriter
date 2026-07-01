/* OneHand core - C ABI opzionale.
 *
 * Guscio sottile e stabile (extern "C") sopra la classe C++ onehand::Engine,
 * per chiamare il motore da altri linguaggi via FFI (Swift, C#, Rust, ...).
 * NON e' necessaria al frontend C++, che usa direttamente onehand::Engine.
 *
 * Modello "pull": si chiama onehand_on_key()/on_timeout()/reset(), poi si
 * leggono gli effetti con le funzioni onehand_edit_, onehand_popup_ e
 * onehand_timer_.
 */
#ifndef ONEHAND_C_H
#define ONEHAND_C_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Handle opaco: l'interno (C++) non e' visibile al chiamante. */
typedef struct OnehandEngine OnehandEngine;

/* Deve combaciare con onehand::KeyKind (stesso ordine). */
enum {
    ONEHAND_KEY_LETTER    = 0,
    ONEHAND_KEY_SPACE     = 1,
    ONEHAND_KEY_BACKSPACE = 2,
    ONEHAND_KEY_TAB       = 3,
    ONEHAND_KEY_ENTER     = 4
};

/* Azione richiesta sul timer del doppio-tap. */
enum {
    ONEHAND_TIMER_NONE   = 0,
    ONEHAND_TIMER_START  = 1,
    ONEHAND_TIMER_CANCEL = 2
};

/* Struct C "piatta" per la configurazione (stringhe come const wchar_t*). */
typedef struct {
    const wchar_t* available_keys;
    int            wildcard_any;
    int            max_candidates;
    int            double_press_ms;
    const wchar_t* punctuation;
    const wchar_t* wordlist_name;
} OnehandConfig;

/* Ciclo di vita. */
OnehandEngine* onehand_create(void);
void           onehand_destroy(OnehandEngine* e);

/* Configurazione e dizionario. Il file lo apre questo strato C, non il core.
 * onehand_load_wordlist_file restituisce 1 se ok, 0 se non apribile. */
void onehand_set_config(OnehandEngine* e, const OnehandConfig* cfg);
int  onehand_load_wordlist_file(OnehandEngine* e, const char* path);

/* Eventi: calcolano gli effetti, poi leggibili con le funzioni qui sotto. */
void onehand_on_key(OnehandEngine* e, int kind, wchar_t letter);
void onehand_on_timeout(OnehandEngine* e);
void onehand_reset(OnehandEngine* e);

/* Lettura degli effetti dell'ultimo evento. */
int            onehand_edit_count(OnehandEngine* e);
int            onehand_edit_backspaces(OnehandEngine* e, int i);
const wchar_t* onehand_edit_insert(OnehandEngine* e, int i);   /* valido fino al prossimo evento */
int            onehand_popup_visible(OnehandEngine* e);
const wchar_t* onehand_popup_text(OnehandEngine* e);
int            onehand_timer_action(OnehandEngine* e);         /* ONEHAND_TIMER_* */
int            onehand_timer_ms(OnehandEngine* e);
int            onehand_pass_through(OnehandEngine* e);

#ifdef __cplusplus
}
#endif

#endif /* ONEHAND_C_H */
