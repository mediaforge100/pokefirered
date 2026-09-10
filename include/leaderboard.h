/* PokePvP native LEADERBOARD (ADR-188).
 *
 * The launcher fetches GET /v1/leaderboard and pushes each ranked row
 * down as a POKEPVP_MSG_LEADERBOARD_ENTRY record on the hostToRom ring
 * (reset-then-fill burst, same contract as history.c). This module owns
 * the ROM-side buffer those records land in and hands the LEADERBOARD
 * menu task (main_menu.c) a read-only snapshot to render.
 *
 * The ROM renders only what this buffer holds; a record that fails any
 * check is dropped, never clamped (same trust-boundary posture as
 * history.c's ValidateIncomingRecord-equivalent checks).
 */
#ifndef POKEPVP_LEADERBOARD_H
#define POKEPVP_LEADERBOARD_H

#include "global.h"

#define POKEPVP_LEADERBOARD_MAX_ENTRIES 8
#define POKEPVP_LEADERBOARD_MAX_NAME_LEN 24

typedef struct
{
    u8 name[POKEPVP_LEADERBOARD_MAX_NAME_LEN + 1];
    u8 nameLen;
    u16 rating;
    u16 games;
} PokePvPLeaderboardEntry;

/* Called from battle_controller_pokepvp.c's HandlePresentationRecord
 * when a POKEPVP_MSG_LEADERBOARD_ENTRY record is drained (pre-battle
 * safe). A zero-length record terminates the list (resets the write
 * index). Bounds-checks everything; returns FALSE for a rejected
 * record. */
bool8 PokePvPLeaderboard_ReceiveEntry(const u8 *payload, u16 length);

/* Returns the current entry count (0 if none / end-of-list was seen). */
u8 PokePvPLeaderboard_Count(void);

/* Copies entry i (0-based, rank i+1) into `out`. Returns FALSE if out of
 * range. */
bool8 PokePvPLeaderboard_Get(u8 index, PokePvPLeaderboardEntry *out);

#endif /* POKEPVP_LEADERBOARD_H */
