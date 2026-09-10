/* PokePvP native POST-MATCH screen (UI plan slice 2, Phase I -- features
 * plan §11 / Build Plan §14).
 *
 * The launcher owns the real match result: it sees `matchEnd` (winner +
 * endReason), knows this side's own `side` from `matchStart`, and pulls the
 * authoritative row (duration, turn count, remaining Pokemon per side,
 * opponent identity) from apps/api's match history. It pushes all of that
 * down as POKEPVP_MSG_POST_MATCH records on the hostToRom ring -- the same
 * trust-boundary shape as history.c: the ROM renders only what this buffer
 * holds, a record that fails any check is dropped, and a zero-length
 * record clears the session.
 *
 * The ROM-side screen (main_menu.c) is entered from CB2_InitMainMenu's
 * startup chain when a session is pending, offers REMATCH / PLAY AGAIN /
 * ADD RIVAL / EXIT, and reports the player's choice back as
 * POKEPVP_MSG_POST_MATCH_ACTION. POKEPVP_MSG_POST_MATCH_RESULT (host ->
 * ROM) carries the outcome of a non-battle action (rival added, opponent
 * unavailable, request failed) for the screen to show.
 *
 * This module never interprets a match result itself -- it stores exactly
 * what the host sent, bounds-checked, and hands the menu task a read-only
 * snapshot (same posture as PokePvPMatchHistory_Get).
 */
#ifndef POKEPVP_POST_MATCH_H
#define POKEPVP_POST_MATCH_H

#include "global.h"

#define POKEPVP_POST_MATCH_MAX_OPP_NAME 16
#define POKEPVP_POST_MATCH_MAX_OPP_TAG 16
#define POKEPVP_POST_MATCH_MAX_DURATION 5999 /* 99:59, clamped */
#define POKEPVP_POST_MATCH_MAX_TURNS 99

/* Outcome values, player-perspective (battler 0). Matches the launcher's
 * own decode of matchEnd's `winner` against this side's own `side`. */
#define POKEPVP_POST_MATCH_WIN 0
#define POKEPVP_POST_MATCH_LOSS 1
#define POKEPVP_POST_MATCH_DRAW 2

/* Action values the post-match screen can report (MSG_POST_MATCH_ACTION). */
#define POKEPVP_POST_MATCH_ACTION_REMATCH 0
#define POKEPVP_POST_MATCH_ACTION_PLAY_AGAIN 1
#define POKEPVP_POST_MATCH_ACTION_ADD_RIVAL 2
#define POKEPVP_POST_MATCH_ACTION_EXIT 3

/* Result values the host can send back (MSG_POST_MATCH_RESULT). */
#define POKEPVP_POST_MATCH_RESULT_OK 0
#define POKEPVP_POST_MATCH_RESULT_OPPONENT_UNAVAILABLE 1
#define POKEPVP_POST_MATCH_RESULT_RATE_LIMITED 2
#define POKEPVP_POST_MATCH_RESULT_FAILED 3

typedef struct
{
    u8 outcome;             /* POKEPVP_POST_MATCH_WIN/LOSS/DRAW */
    u16 durationSec;        /* clamped to POKEPVP_POST_MATCH_MAX_DURATION */
    u8 turnCount;           /* clamped to POKEPVP_POST_MATCH_MAX_TURNS */
    u8 myRemaining;         /* this side's remaining Pokemon at the end */
    u8 oppRemaining;        /* opponent's remaining Pokemon at the end */
    u8 oppName[POKEPVP_POST_MATCH_MAX_OPP_NAME + 1]; /* charmap, EOS-terminated */
    u8 oppTag[POKEPVP_POST_MATCH_MAX_OPP_TAG + 1];   /* charmap, EOS-terminated */
} PokePvPPostMatch;

/* Handles a POKEPVP_MSG_POST_MATCH record (host -> ROM). A zero-length
 * payload clears any pending session; otherwise the new session replaces
 * the old. Bounds-checks everything; returns FALSE for a rejected record.
 * The '#' of a NAME#1234 player tag has no FireRed glyph (charmap.txt has
 * no '#'), so it is stored as a space -- "ASH 1234" renders instead of a
 * dropped-character mash. */
bool8 PokePvPPostMatch_Receive(const u8 *payload, u16 length);

/* True while a post-match session is pending (the menu task should show
 * the post-match screen instead of the top-level menu). */
bool8 PokePvPPostMatch_IsPending(void);

/* Copies the pending session into `out`. Returns FALSE if none pending. */
bool8 PokePvPPostMatch_Get(PokePvPPostMatch *out);

/* Clears the pending session (used when the player leaves the screen --
 * EXIT, or a new battle starts consuming it -- so the next menu entry is
 * the plain top-level menu). */
void PokePvPPostMatch_Clear(void);

/* Sets the pending result code (POKEPVP_MSG_POST_MATCH_RESULT handler).
 * Only the newest code is kept; the screen consumes it once. Ignored when
 * no post-match session is pending. */
void PokePvPPostMatch_SetResult(u8 result);

/* Consumes the pending result code into `out`; returns FALSE if none. */
bool8 PokePvPPostMatch_ConsumeResult(u8 *out);

#endif /* POKEPVP_POST_MATCH_H */