/* PokePvP PROFILE screen buffer (UI plan slice 5, Phase E -- features
 * plan §7 / Build Plan §10: "Show ID, display name, trainer sprite, and
 * selected badges on the Profile main screen and Trainer Card").
 *
 * The launcher fetches GET /v1/account/profile + /stats + /recent-
 * opponents at boot and pushes them as POKEPVP_MSG_PROFILE (53) and a
 * POKEPVP_MSG_RECENT_OPPONENT (54) burst -- the same trust boundary as
 * history.c: the ROM renders only what this buffer holds, a record that
 * fails any check is dropped, and a zero-length record resets.
 *
 * The player's own display name already lives in the save block
 * (gSaveBlock2Ptr->playerName); this buffer adds the permanent NAME#1234
 * tag, the sprite id, the competitive stats (matches/wins/losses/ties,
 * qualifying matches only -- practice/forfeit excluded server-side), the
 * most-used species, and the recent-opponents list. The sprite *picker*
 * (choosing a new sprite id) is deliberately deferred (Build Plan §10
 * item 3's "move existing player-name and sprite settings into Profile"
 * -- name entry already exists via ADR-113; sprite selection is the
 * remaining human-gated piece, tracked in the UI plan).
 */
#ifndef POKEPVP_PROFILE_H
#define POKEPVP_PROFILE_H

#include "global.h"

#define POKEPVP_PROFILE_MAX_TAG_LEN 16
#define POKEPVP_PROFILE_MAX_TOP_SPECIES 3
#define POKEPVP_PROFILE_MAX_RECENT 4
#define POKEPVP_PROFILE_MAX_OPP_NAME_LEN 16
#define POKEPVP_PROFILE_MAX_OPP_TAG_LEN 16

typedef struct
{
    u8 tag[POKEPVP_PROFILE_MAX_TAG_LEN + 1]; /* charmap, EOS-terminated */
    u8 spriteId;
    u16 matches;
    u16 wins;
    u16 losses;
    u16 ties;
    u8 topCount;
    u16 topSpecies[POKEPVP_PROFILE_MAX_TOP_SPECIES];
} PokePvPProfile;

typedef struct
{
    u8 name[POKEPVP_PROFILE_MAX_OPP_NAME_LEN + 1];
    u8 tag[POKEPVP_PROFILE_MAX_OPP_TAG_LEN + 1];
} PokePvPRecentOpponent;

/* Handles a POKEPVP_MSG_PROFILE record (host -> ROM). A zero-length
 * payload clears the profile. Returns FALSE for a rejected record. */
bool8 PokePvPProfile_Receive(const u8 *payload, u16 length);

/* Copies the profile into `out`. Returns FALSE if none received yet. */
bool8 PokePvPProfile_Get(PokePvPProfile *out);

/* Handles a POKEPVP_MSG_RECENT_OPPONENT record (host -> ROM). A
 * zero-length payload resets the list (burst reset-first, same contract
 * as history.c). Returns FALSE for a rejected record. */
bool8 PokePvPProfile_ReceiveRecent(const u8 *payload, u16 length);

/* Current recent-opponent count. */
u8 PokePvPProfile_RecentCount(void);

/* Copies recent opponent i (0-based) into `out`. FALSE if out of range. */
bool8 PokePvPProfile_GetRecent(u8 index, PokePvPRecentOpponent *out);

#endif /* POKEPVP_PROFILE_H */