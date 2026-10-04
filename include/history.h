/* PokePvP native MATCH HISTORY (Phase 8, ADR-168; reworked 2026-10-02,
 * ADR-321 -- real per-match log, not a per-opponent aggregate).
 *
 * The launcher fetches the account's real match log from apps/api
 * (GET /v1/account/matches, newest first) and pushes each entry down as a
 * POKEPVP_MSG_HISTORY_ENTRY record on the hostToRom ring (never a
 * credential -- Rule 14: auth stays in the launcher). This module owns
 * the ROM-side buffer those records land in -- pre-battle, like
 * team_builder.c's receive path -- and hands the MATCH HISTORY menu task
 * (main_menu.c) a read-only snapshot to render.
 *
 * The ROM renders only what this buffer holds; a record that fails any
 * check is dropped, never clamped (same trust-boundary posture as
 * team_builder's ValidateIncomingRecord).
 */
#ifndef POKEPVP_HISTORY_H
#define POKEPVP_HISTORY_H

#include "global.h"

#define POKEPVP_HISTORY_MAX_ENTRIES 8
#define POKEPVP_HISTORY_MAX_NAME_LEN 24

/* `result` is from this account's own point of view, matching
 * MatchHistoryEntry["result"] (packages/persistence/src/matches-
 * repository.ts) verbatim -- never recomputed on the ROM side. */
#define POKEPVP_HISTORY_RESULT_WIN  0
#define POKEPVP_HISTORY_RESULT_LOSS 1
#define POKEPVP_HISTORY_RESULT_TIE  2

/* `battleClass` mirrors apps/api/src/match-history.ts's own
 * battleClassForQueue -- EARLY/ELITE for a quick/custom queue, UNKNOWN
 * for anything else (practice, invite -- battleClassForQueue returns
 * null for those today). UNKNOWN suppresses the class suffix entirely
 * rather than rendering a guess. */
#define POKEPVP_HISTORY_CLASS_EARLY   0
#define POKEPVP_HISTORY_CLASS_ELITE   1
#define POKEPVP_HISTORY_CLASS_UNKNOWN 2

typedef struct
{
    u8 name[POKEPVP_HISTORY_MAX_NAME_LEN + 1];
    u8 nameLen;
    u8 result;
    u8 battleClass;
} PokePvPHistoryEntry;

/* Called from battle_controller_pokepvp.c's HandlePresentationRecord
 * when a POKEPVP_MSG_HISTORY_ENTRY record is drained (pre-battle safe).
 * A zero-length record terminates the list (resets the write index).
 * Bounds-checks everything; returns FALSE for a rejected record. */
bool8 PokePvPMatchHistory_ReceiveEntry(const u8 *payload, u16 length);

/* Returns the current entry count (0 if none / end-of-list was seen). */
u8 PokePvPMatchHistory_Count(void);

/* Copies entry i (0-based) into `out`. Returns FALSE if out of range. */
bool8 PokePvPMatchHistory_Get(u8 index, PokePvPHistoryEntry *out);

#endif /* POKEPVP_HISTORY_H */
