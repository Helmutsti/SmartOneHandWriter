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

/* Deve combaciare con onehand::Action (stessi valori numerici: MAI rinumerare).
 * Il frontend risolve quale tasto fisico attiva ciascuna azione. ACCEPT =
 * Conferma; ROLLING = Roll; WILDCARD e' deprecato (no-op). Le azioni OPEN_* e
 * CONFIRM_NEW_WORD sono la nuova concezione a Parola. */
enum {
    ONEHAND_ACTION_LETTER          = 0,   /* 'letter' e' il TASTO del keymap */
    ONEHAND_ACTION_WILDCARD        = 1,   /* deprecato: no-op */
    ONEHAND_ACTION_ACCEPT          = 2,   /* Conferma */
    ONEHAND_ACTION_ROLLING         = 3,   /* Roll */
    ONEHAND_ACTION_DELETE_CHAR     = 4,
    ONEHAND_ACTION_DELETE_WORD     = 5,
    ONEHAND_ACTION_FINALIZE        = 6,
    ONEHAND_ACTION_CONFIRM_NEW_WORD= 7,
    ONEHAND_ACTION_OPEN_PREV_WORD  = 8,
    ONEHAND_ACTION_OPEN_NEXT_WORD  = 9,
    ONEHAND_ACTION_OPEN_WORD_AT    = 10   /* usa onehand_on_action_index */
};

/* Struct C "piatta" per la configurazione (stringhe come const wchar_t*). */
typedef struct {
    int            max_candidates;
    int            double_press_ms;
    const wchar_t* wordlist_name;
} OnehandConfig;

/* Ciclo di vita. */
OnehandEngine* onehand_create(void);
void           onehand_destroy(OnehandEngine* e);

/* Configurazione e dizionario. Il file lo apre questo strato C, non il core.
 * onehand_load_wordlist_file restituisce 1 se ok, 0 se non apribile. */
void onehand_set_config(OnehandEngine* e, const OnehandConfig* cfg);
int  onehand_load_wordlist_file(OnehandEngine* e, const char* path);

/* Applica la configurazione leggendo direttamente il testo di config.json
 * (stesso parser tollerante usato dal frontend Windows: onehand::parseConfig).
 * Un'unica implementazione condivisa da tutti i frontend che leggono il file
 * cosi' com'e', invece di riempire a mano OnehandConfig. */
void onehand_apply_config_json(OnehandEngine* e, const char* json_utf8);

/* Letture della configurazione applicata (valide finche' non se ne applica
 * un'altra): servono al frontend per popolare la propria UI. */
const wchar_t* onehand_config_wordlist_name(OnehandEngine* e);
int            onehand_config_double_press_ms(OnehandEngine* e);

/* Eventi: calcolano gli effetti, poi leggibili con le funzioni qui sotto. */
void onehand_on_key(OnehandEngine* e, int kind, wchar_t letter);
void onehand_on_timeout(OnehandEngine* e);
void onehand_reset(OnehandEngine* e);

/* Percorso principale (vedi ONEHAND_ACTION_*). Per ACTION_LETTER, 'letter' e' il
 * TASTO del keymap premuto (non la lettera finale). */
void onehand_on_action(OnehandEngine* e, int action, wchar_t letter);
/* Azione con indice intero (per ONEHAND_ACTION_OPEN_WORD_AT: accesso casuale). */
void onehand_on_action_index(OnehandEngine* e, int action, int index);
/* Deprecato (modello wildcard rimosso): no-op, resta per compat. */
void onehand_preview_wildcard(OnehandEngine* e);

/* Introspezione del documento (per l'editor interno). */
int            onehand_word_count(OnehandEngine* e);
int            onehand_open_index(OnehandEngine* e);   /* -1 = caret in coda */
int            onehand_caret(OnehandEngine* e);         /* offset in onehand_render_text */
const wchar_t* onehand_render_text(OnehandEngine* e);   /* valido fino al prossimo evento */

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
