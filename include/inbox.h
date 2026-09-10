/* PokePvP challenge inbox buffer (UI plan slice 4, Phase H -- features
 * plan §10 / Build Plan §13 item 2: "a small pending challenge inbox so
 * an invitation is not lost merely because the recipient was navigating
 * another menu").
 *
 * The launcher caches the real challenge ids; the ROM holds only the
 * DISPLAY data it renders -- [slot u8][flags u8][nameLen u8][name][tagLen
 * u8][tag] per POKEPVP_MSG_CHALLENGE_ARRIVED record, exactly like
 * history.c's trust boundary (a record that fails any check is dropped;
 * a zero-length record ends the burst and resets the buffer; the ROM
 * echoes the *slot* back in POKEPVP_MSG_CHALLENGE_ACTION, never an id it
 * does not own).
 *
 * Interruptibility (features plan §10 item 4 / Build Plan §13 item 4):
 * the inbox is surfaced by main_menu.c's menu tasks -- only whenever
 * the player reaches an interruptible screen (the top menu / START
 * MATCH submenu). A challenge arriving mid-battle or in a destructive
 * flow stays queued here and appears at the next menu entry; it is
 * never lost and never pops over a battle.
 */
#ifndef POKEPVP_INBOX_H
#define POKEPVP_INBOX_H

#include "global.h"

#define POKEPVP_INBOX_MAX 3
#define POKEPVP_INBOX_MAX_NAME_LEN 16
#define POKEPVP_INBOX_MAX_TAG_LEN 16

/* Flags byte bits (payload[1] of CHALLENGE_ARRIVED). */
#define POKEPVP_INBOX_FLAG_REMATCH 0x01u
#define POKEPVP_INBOX_FLAG_EARLY 0x02u /* battle class early (quick-early queue) */

typedef struct
{
    u8 name[POKEPVP_INBOX_MAX_NAME_LEN + 1]; /* charmap, EOS-terminated */
    u8 tag[POKEPVP_INBOX_MAX_TAG_LEN + 1];   /* charmap, EOS-terminated */
    u8 flags;
} PokePvPInboxEntry;

/* Handles a POKEPVP_MSG_CHALLENGE_ARRIVED record (host -> ROM). A
   zero-length payload ends the burst and resets the buffer (stale
   entries are never shown). Entry payload: [slot u8][flags u8][nameLen
   u8][name][] tagLen u8][tag]; out-of-range slot or oversized strings
   are rejected. Returns FALSE for a rejected record. */
bool8 PokePvPInbox_ReceiveEntry(const u8 *payload, u16 length);

/* Current entry count (0 if none / end-of-burst seen). */
u8 PokePvPInbox_Count(void);

/* Copies entry i (0-based) into `out`. Returns FALSE if out of range. */
bool8 PokePvPInbox_Get(u8 index, PokePvPInboxEntry *out);

/* Removes entry `index`, compacting the buffer down (call after the
   player acted on it). Returns FALSE if out of range. */
bool8 PokePvPInbox_Remove(u8 index);

#endif /* POKEPVP_INBOX_H */