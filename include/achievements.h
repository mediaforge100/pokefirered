/* PokePvP ACHIEVEMENTS + TEAM ACHIEVEMENTS (ADR-316).
 *
 * The launcher fetches the two catalogs (GET /v1/achievements,
 * /v1/team-achievements) plus what this account has actually earned
 * (GET /v1/account/achievements, /v1/account/team-achievements),
 * merges each catalog row with its earned flag, and pushes the result
 * down as a POKEPVP_MSG_ACHIEVEMENT_ENTRY / POKEPVP_MSG_TEAM_ACHIEVEMENT_
 * ENTRY burst -- same reset-then-fill shape as history.c. Separate
 * buffers, never merged (they render as two separate list screens under
 * PROFILE -> ACHIEVEMENTS).
 *
 * The "unlocked" notice (ADR-316 §5) is a third, independent buffer:
 * GET /v1/account/achievements/unlocked, pushed as a
 * POKEPVP_MSG_ACHIEVEMENT_UNLOCKED burst of plain titles (the ROM never
 * needs to know which catalog an unlocked title came from to show it).
 * Surfaced only on the top menu (main_menu.c's Task_HandleMenuInput),
 * same interruptibility rule as the challenge inbox -- never mid-battle.
 *
 * The ROM renders only what these buffers hold; a record that fails any
 * check is dropped, never clamped-and-kept (same trust-boundary posture
 * as history.c/profile.c).
 */
#ifndef POKEPVP_ACHIEVEMENTS_H
#define POKEPVP_ACHIEVEMENTS_H

#include "global.h"

#define POKEPVP_ACHIEVEMENTS_MAX_ENTRIES 8
/* Migration 013 seeds exactly 2 team achievements today; 4 gives the
 * catalog a little real headroom to grow without a code change, while
 * staying far smaller than ACHIEVEMENTS_MAX_ENTRIES -- IWRAM is at
 * ~100% on this ROM (CLAUDE.md's own pinned-versions note), so this
 * buffer's static footprint is a real, binding constraint, not a
 * nicety. */
#define POKEPVP_TEAM_ACHIEVEMENTS_MAX_ENTRIES 4
#define POKEPVP_ACHIEVEMENT_MAX_TITLE_LEN 20
#define POKEPVP_ACHIEVEMENT_MAX_DESC_LEN 48
/* Same IWRAM-budget reasoning as TEAM_ACHIEVEMENTS_MAX_ENTRIES above.
 * More than 2 can genuinely unlock from one match (e.g. a win that is
 * both a round-number milestone and a team-composition threshold); a
 * burst past this cap just drops the extra ones (ReceiveEntry's own
 * `*count >= maxEntries` guard, same fail-closed shape as every other
 * buffer here) rather than crashing -- losing an occasional third pop-in
 * line is a fine trade against this ROM's real IWRAM ceiling. */
#define POKEPVP_ACHIEVEMENT_UNLOCKED_MAX_ENTRIES 2

typedef struct
{
    u8 title[POKEPVP_ACHIEVEMENT_MAX_TITLE_LEN + 1];
    u8 description[POKEPVP_ACHIEVEMENT_MAX_DESC_LEN + 1];
    bool8 earned;
} PokePvPAchievementEntry;

/* Handles a POKEPVP_MSG_ACHIEVEMENT_ENTRY record. A zero-length payload
 * resets the buffer (reset-then-fill, same as history.c). Payload:
 * [earned u8][titleLen u8][title][descLen u8][desc]. Returns FALSE for
 * a rejected record. */
bool8 PokePvPAchievements_ReceiveEntry(const u8 *payload, u16 length);
u8 PokePvPAchievements_Count(void);
bool8 PokePvPAchievements_Get(u8 index, PokePvPAchievementEntry *out);

/* Same contract as the ACHIEVEMENTS trio above, for the separate TEAM
 * ACHIEVEMENTS catalog/buffer (POKEPVP_MSG_TEAM_ACHIEVEMENT_ENTRY). */
bool8 PokePvPTeamAchievements_ReceiveEntry(const u8 *payload, u16 length);
u8 PokePvPTeamAchievements_Count(void);
bool8 PokePvPTeamAchievements_Get(u8 index, PokePvPAchievementEntry *out);

/* The main-menu "unlocked" notice buffer. A zero-length payload resets
 * it (burst reset-then-fill, same as the two catalogs above). Payload:
 * [titleLen u8][title]. */
bool8 PokePvPAchievementUnlocked_ReceiveEntry(const u8 *payload, u16 length);
u8 PokePvPAchievementUnlocked_Count(void);
bool8 PokePvPAchievementUnlocked_GetTitle(u8 index, u8 *outTitle, u8 maxLen);

/* Clears the unlocked-notice buffer -- called once the player has
 * dismissed the notice screen, so it never re-shows the same titles on
 * the next top-menu visit (the launcher's own ack, over HTTP, is what
 * stops the SERVER from sending them again; this is the ROM-side half
 * of "never re-fire," for the case the player is still sitting on the
 * top menu when a second, unrelated push arrives empty). */
void PokePvPAchievementUnlocked_Clear(void);

#endif /* POKEPVP_ACHIEVEMENTS_H */
