/* SPDX-License-Identifier: MIT */
#ifndef HAX_TERMINAL_INTERRUPT_H
#define HAX_TERMINAL_INTERRUPT_H

#include <stddef.h>

/* Initialize bare-Esc detection and terminal restoration. Detection is available only when stdin
 * and stdout are TTYs; otherwise the watcher API is inert and request queries return false. */
void interrupt_init(void);

/* Install fatal-signal handlers without starting the TTY watcher. The handlers invoke the optional
 * hook, restore terminal state, and re-raise the signal with its default disposition. */
void interrupt_install_fatal_signal_handlers(void);

/* The hook runs from a signal handler and must be async-signal-safe. */
void interrupt_set_fatal_signal_hook(void (*hook)(void));

/* Install handlers that translate driver signals into requests for headless runs: SIGINT and
 * SIGTERM latch an abort — a repeat escalates to the fatal path — and SIGUSR1 latches a pause.
 * Overrides the fatal handlers for these signals, so install those first for the escalation
 * (and the remaining fatal signals) to keep their cleanup. */
void interrupt_install_request_signal_handlers(void);

/* Start bare-Esc detection. The first Esc requests a pause and the second requests an immediate
 * abort. Arming is idempotent and does not clear requests left by an earlier cycle. */
void interrupt_arm(void);

/* Stop detection, wait until the watcher no longer owns stdin, and restore terminal mode. Input
 * received while armed is kept as type-ahead rather than discarded; if an Esc requested a pause
 * or abort, its Enters become newlines so it waits in the editor instead of submitting.
 * Disarming is idempotent. */
void interrupt_disarm(void);

/* Pop the oldest byte typed while armed: ordinary keys and bracketed pastes, without Escs or other
 * escape sequences. Return 1 with the byte in `*byte`, or 0 when none is queued. The editor replays
 * these before reading the terminal, so a message typed with Enter during a turn submits after it.
 * Call only while disarmed. */
int interrupt_typeahead_pop(unsigned char *byte);

/* Requests remain latched until interrupt_clear_requests(). Abort implies pause. These queries are
 * safe from any thread. */
int interrupt_pause_requested(void);
int interrupt_abort_requested(void);
void interrupt_clear_requests(void);

/* Wait briefly for a recently received Esc to be classified. Call before an irreversible decision
 * based on the request flags, such as starting a tool or sending another model request. */
void interrupt_resolve_pending_escape(void);

/* Byte classifier exposed so its terminal-independent behavior can be tested directly. */
enum interrupt_classifier_state {
    INTERRUPT_CLASSIFIER_IDLE,
    INTERRUPT_CLASSIFIER_ESCAPE_PENDING,
    INTERRUPT_CLASSIFIER_CSI,
    INTERRUPT_CLASSIFIER_SS3,
};

struct interrupt_classifier {
    enum interrupt_classifier_state state;
};

void interrupt_classifier_init(struct interrupt_classifier *classifier);

/* Return true when this byte confirms that the preceding Esc was bare. */
int interrupt_classifier_feed(struct interrupt_classifier *classifier, unsigned char byte);

/* Return true when the timeout confirms a pending Esc was bare. */
int interrupt_classifier_timeout(struct interrupt_classifier *classifier);

/* Type-ahead filter over the classifier, exposed for direct testing like it. */
struct buf;

struct interrupt_typeahead_filter {
    struct interrupt_classifier classifier;
    char sequence[8]; /* the escape sequence in progress, truncated past its size */
    size_t sequence_len;
    size_t paste_end_matched; /* bytes of the paste end marker seen so far */
    int in_paste;
};

void interrupt_typeahead_filter_init(struct interrupt_typeahead_filter *filter);

/* Append the type-ahead in `bytes` to `out`: ordinary bytes, plus bracketed pastes verbatim with
 * their markers. Bare Escs and every other escape sequence are dropped, and bytes inside a paste
 * are never classified as Esc. Return how many bare Escs the bytes confirmed. */
int interrupt_typeahead_filter_feed(struct interrupt_typeahead_filter *filter,
                                    const unsigned char *bytes, size_t len, struct buf *out);

#endif /* HAX_TERMINAL_INTERRUPT_H */
