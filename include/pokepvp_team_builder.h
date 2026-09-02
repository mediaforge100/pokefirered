#ifndef GUARD_POKEPVP_TEAM_BUILDER_H
#define GUARD_POKEPVP_TEAM_BUILDER_H

#include "global.h"

// POKEPVP (ADR-093): the Team Builder's public surface. The implementation
// lives in src/pokepvp/team_builder.c, alongside mailbox.c and
// battle_controller_pokepvp.c; this header exists so src/main_menu.c can
// reach it without gaining src/pokepvp/ on its include path (the same
// reason battle_setup.h declares StartPokePvPAutoMatch).
//
// The wire-level record this produces is rom/pvp-gen3/team_types.h's
// PokePvPTeamMemberRecord, which deliberately has no pokefirered types in
// it and is not exposed here.

#define POKEPVP_TEAM_SLOTS 5

// Number of members currently held in a team slot (0-6). Safe for any
// slot value; an out-of-range slot reads 0 rather than off the end.
u8 PokePvPTeamBuilder_MemberCount(u8 slot);

// TRUE once the legal-species roster has been built into the boxes. The
// roster is built once per session and cached.
bool8 PokePvPTeamBuilder_RosterReady(void);

// Builds one roster species per call and returns percent complete (0-100).
// Call once per frame until it returns 100, keeping a real screen drawn
// while it runs -- doing the whole roster in one frame is ~11 seconds of
// CPU on this build, which is indistinguishable from a hang.
u8 PokePvPTeamBuilder_BuildRosterStep(void);

// Enters FireRed's own PC box screen in MOVE POKéMON mode with the legal
// species roster loaded into the boxes and `slot`'s current members loaded
// into the party. On exit the party is written back into `slot`, the team
// is emitted to the launcher over the mailbox, and `returnCallback` runs.
//
// Destroys the caller's tasks (EnterPokeStorage calls ResetTasks), so the
// caller must free its own window buffers and stop touching its task after
// this returns -- exactly the sequence StartPokePvPAutoMatch already uses.
void PokePvPTeamBuilder_Open(u8 slot, MainCallback returnCallback);

// POKEPVP (ADR-095): loads `slot`'s stored team into gPlayerParty without
// opening the PC screen -- for the AUTO-MATCH team-selector, which needs
// the chosen team's real Pokemon in the party before the battle starts, not
// a UI to edit them. A no-op (party left untouched) if `slot` is
// out-of-range or empty; the caller (main_menu.c's selector) already
// refuses to let a player pick an empty slot, so this is a second,
// fail-closed guard rather than the only one.
void PokePvPTeamBuilder_LoadTeamForBattle(u8 slot);

// --- Move editing -------------------------------------------------------
//
// A team slot *is* the team document (see team_builder.c), so moves are
// edited on the record directly; the party is only ever a materialized
// view of one, rebuilt on the way into the PC.

// Species of a team member, or SPECIES_NONE for an empty/out-of-range slot.
u16 PokePvPTeamBuilder_MemberSpecies(u8 slot, u8 index);

// Move currently in one of a member's four slots (MOVE_NONE if empty).
u16 PokePvPTeamBuilder_MemberMove(u8 slot, u8 index, u8 moveSlot);

// Writes `move` into a member's move slot, and its full PP with it. Passing
// MOVE_NONE clears the slot. Refuses a move the species cannot legally
// learn -- a UX guard, not the legality authority; the server revalidates.
// Returns TRUE if the write happened.
bool8 PokePvPTeamBuilder_SetMemberMove(u8 slot, u8 index, u8 moveSlot, u16 move);

// Does `species` legally learn `move`? Level-up (up to the format's level),
// TM, HM, or tutor -- every route this ROM ships data for.
bool8 PokePvPTeamBuilder_SpeciesCanLearn(u16 species, u16 move);

// Fills `dest` with every move `species` can learn (level-up up to the
// format's level, TM, HM, and tutor), de-duplicated and in alphabetical
// order by move name -- the only ordering a player scrolling a several-
// dozen-entry movepool can actually navigate by.
// Returns how many were written, capped at `capacity`.
u16 PokePvPTeamBuilder_LegalMoves(u16 species, u16 *dest, u16 capacity);

// A safe upper bound on what PokePvPTeamBuilder_LegalMoves can return, for
// sizing a caller's buffer: the whole level-up learnset plus every TM, HM
// and tutor move.
#define POKEPVP_MAX_LEGAL_MOVES 128

// Emits `slot`'s current contents to the launcher as one
// POKEPVP_MSG_TEAM_MEMBER record per member. Called automatically on exit
// from the box screen; exposed for tests and for a future "re-sync all
// teams" path. An empty slot emits one record with memberCount 0.
void PokePvPTeamBuilder_SendTeam(u8 slot);

#endif // GUARD_POKEPVP_TEAM_BUILDER_H
