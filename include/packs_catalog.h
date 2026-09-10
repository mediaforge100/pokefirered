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
 */
#ifndef POKEPVP_PACKS_CATALOG_H
#define POKEPVP_PACKS_CATALOG_H

#include "global.h"

#define POKEPVP_PACKS_PER_CLASS 5
#define POKEPVP_PACKS_MAX_TITLE_LEN 16

/* Flags bits for POKEPVP_MSG_LAUNCH_CONFIG (presentation_types.h). */
#define POKEPVP_FLAG_PRACTICE 0x01
#define POKEPVP_FLAG_INVITE 0x02
#define POKEPVP_FLAG_QUICK 0x04
#define POKEPVP_FLAG_CUSTOM 0x08

typedef struct
{
    u8 title[POKEPVP_PACKS_MAX_TITLE_LEN + 1]; /* charmap, EOS-terminated */
} PokePvPPackEntry;

/* Handles a POKEPVP_MSG_PACK_CATALOG_ENTRY record (host -> ROM). A
 * zero-length payload resets the whole buffer (end of burst). Each real
 * entry is [class u8][index u8][titleLen u8][title bytes]; out-of-range
 * class/index are dropped. */
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