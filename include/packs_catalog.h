/* PokePvP pack catalog buffer (UI plan slice 3, Phase C -- Build Plan §8
 * items 3/4: Quick Battle renders "Battle Class -> five Battle Packs ->
 * pack overview"). The launcher fetches GET /v1/battle-packs summaries
 * at boot and pushes each as a POKEPVP_MSG_PACK_CATALOG_ENTRY; this
 * module owns the ROM-side buffer the QUICK BATTLE picker renders, plus
 * the launch-config feature flags (POKEPVP_MSG_LAUNCH_CONFIG) that hide
 * modes the server has turned off.
 *
 * Same trust-boundary posture as history.c: a record that fails any
 * check is dropped; a zero-length record ends the burst and is the only
 * thing that resets the buffer.
 *
 * NOTE: this file is a manually-synced copy of `rom/pvp-gen3/
 * packs_catalog.h`, not a symlink (unlike `src/pokepvp/*.c`) -- see that
 * file's own doc comment and `pokepvp_team_builder.h`'s top-of-file note
 * for why `src/main_menu.c` needs its own copy of any header it reaches
 * directly. Keep the two in sync by hand; ADR-209 found `profile.h` had
 * already silently diverged this same way once.
 */
#ifndef POKEPVP_PACKS_CATALOG_H
#define POKEPVP_PACKS_CATALOG_H

#include "global.h"

#define POKEPVP_PACKS_PER_CLASS 4
#define POKEPVP_PACKS_MAX_TITLE_LEN 16
/* Pack overview (owner ask, 2026-09-13 revision): `difficulty` is gone
 * entirely (removed from the schema/YAML/wire -- it's no longer a real
 * field anywhere), and `playstyleLabel` is no longer a hand-authored
 * style label -- it's the server's own abbreviated species list for the
 * pack's team (`abbreviateTeamForPlaystyle`, packages/battle-packs), e.g.
 * "Sno/Tau/Gen/Ala/Sta/Rhy" for a 6-mon ELITE team or "Charman/Squirtl/
 * Bulbasa" for a 3-mon EARLY team -- sized so any team size 1-6 fits in
 * one line. Widened from 16 -> 24 to fit that; affordable because
 * deleting `difficulty` (9 bytes/entry) frees more than this costs (8
 * bytes/entry) -- net EWRAM effect is a small savings, confirmed by a
 * real `make firered-base` rebuild (see the ADR for this change's exact
 * before/after numbers). The free-text `description` field is removed
 * from the project entirely (owner ask: it was never rendered in-game)
 * -- there is nothing left to "deliberately not carry" here anymore. */
#define POKEPVP_PACKS_MAX_PLAYSTYLE_LEN 24

/* Flags bits for POKEPVP_MSG_LAUNCH_CONFIG (presentation_types.h). */
#define POKEPVP_FLAG_PRACTICE 0x01
#define POKEPVP_FLAG_INVITE 0x02
#define POKEPVP_FLAG_QUICK 0x04
#define POKEPVP_FLAG_CUSTOM 0x08

typedef struct
{
    u8 title[POKEPVP_PACKS_MAX_TITLE_LEN + 1]; /* charmap, EOS-terminated */
    u8 playstyle[POKEPVP_PACKS_MAX_PLAYSTYLE_LEN + 1]; /* charmap, EOS-terminated; empty if never sent */
} PokePvPPackEntry;

/* Handles a POKEPVP_MSG_PACK_CATALOG_ENTRY record (host -> ROM). A
 * zero-length payload resets the whole buffer (end of burst). Each real
 * entry is [class u8][index u8][titleLen u8][title bytes][playstyleLen
 * u8][playstyle bytes]; out-of-range class/index are dropped. The
 * playstyle field is optional on the wire -- a payload that ends right
 * after title (the pre-existing 3+titleLen shape) is still accepted,
 * leaving it empty, so an older host build talking to this ROM degrades
 * gracefully instead of rejecting the whole record. (`difficulty` used to
 * be a third optional field here; removed entirely -- owner ask,
 * 2026-09-13.) */
bool8 PokePvPPacks_ReceiveEntry(const u8 *payload, u16 length);

/* Handles a POKEPVP_MSG_LAUNCH_CONFIG record: sets the feature-flag
 * byte (bit0 practice, bit1 invite, bit2 quick, bit3 custom). A
 * zero-length payload resets to all-disabled. */
void PokePvPPacks_ReceiveLaunchConfig(const u8 *payload, u16 length);

/* The current feature flags byte (POKEPVP_FLAG_*). */
u8 PokePvPPacks_Flags(void);

/* The pack title at (battleClass 0 early /1 elite, index 0-4). Returns
 * FALSE when the slot is empty (no catalog pushed yet). */
bool8 PokePvPPacks_Get(u8 battleClass, u8 index, PokePvPPackEntry *out);

#endif /* POKEPVP_PACKS_CATALOG_H */
