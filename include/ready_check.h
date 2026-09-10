/* PokePvP ready-check buffer (UI plan slice 3, Phase C -- Build Plan §8
 * item 2: "`readyCheck` must be answered by a real player, not an auto
 * ack"). The launcher relays the gateway's readyCheck as
 * POKEPVP_MSG_READY_CHECK; this module buffers it (with the timeout
 * window in frames), and the menu wait tasks (main_menu.c) show the
 * prompt and emit POKEPVP_MSG_READY_CHOICE.
 *
 * Same trust-boundary posture as history.c/post_match.c: the ROM renders
 * only what this buffer holds; a malformed record is dropped; the
 * launcher's candidate id is never displayed (the player only sees
 * "READY?" text).
 */
#ifndef POKEPVP_READY_CHECK_H
#define POKEPVP_READY_CHECK_H

#include "global.h"

#define POKEPVP_READY_CHECK_MAX_TIMEOUT_FRAMES 1800 /* ~30s backstop */

/* Handles a POKEPVP_MSG_READY_CHECK record (host -> ROM). A zero-length
 * payload clears any pending prompt; otherwise the payload's u16 LE
 * timeoutMs becomes a frame budget (timeoutMs / 16, clamped to
 * POKEPVP_READY_CHECK_MAX_TIMEOUT_FRAMES). Returns FALSE for a rejected
 * record. Only the newest prompt is kept. */
bool8 PokePvPReadyCheck_Receive(const u8 *payload, u16 length);

/* True while a ready-check prompt is pending. */
bool8 PokePvPReadyCheck_IsPending(void);

/* Remaining frame budget (decremented by the menu task each frame it's
 * shown; 0 means the window lapsed -- treat as declined). */
u16 PokePvPReadyCheck_FramesLeft(void);

/* The menu task's per-frame tick while the prompt is on screen. Returns
 * FALSE once the budget hits 0 (caller should answer declined). */
bool8 PokePvPReadyCheck_Tick(void);

/* Clears the pending prompt (after the player answered). */
void PokePvPReadyCheck_Clear(void);

#endif /* POKEPVP_READY_CHECK_H */