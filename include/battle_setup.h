#ifndef GUARD_BATTLE_SETUP_H
#define GUARD_BATTLE_SETUP_H

#include "global.h"

void StartWildBattle(void);
void StartRoamerBattle(void);
// POKEPVP (ADR-063): debug-only trigger for a BATTLE_TYPE_POKEPVP battle,
// reusing DoStandardWildBattle's exact transition machinery. See its own
// doc comment in battle_setup.c for the safety/scope of this hook.
void StartPokePvPDebugBattle(void);
// POKEPVP (ADR-079/080, D7 step 1): called from main_menu.c's PokePvP
// menu "Start Match" option -- see battle_setup.c's own doc comment.
void StartPokePvPMenuMatch(void);
// POKEPVP (ADR-091, D7 refinement): AUTO-MATCH submenu option -- same warp
// as StartPokePvPMenuMatch but fires the battle on the first CB2_Overworld
// call, before any visible overworld frame renders. See battle_setup.c.
void StartPokePvPAutoMatch(void);
// POKEPVP (ADR-121): the real-opponent counterpart to StartPokePvPDebugBattle
// -- same transition machinery, but gEnemyParty[0] is a real species/level
// from a real gateway-paired opponent (PokePvP_GetRealOpponentSpecies/Level
// below) instead of a synthesized Rattata. Only ever called once
// PokePvP_IsRealOpponentReady() is true. See battle_setup.c.
void StartPokePvPRealMatch(void);
// POKEPVP (ADR-121): defined in battle_controller_pokepvp.c (that's where
// POKEPVP_MSG_REAL_OPPONENT_MON is decoded), declared here so
// main_menu.c's AUTO-MATCH wait task and this file's own
// StartPokePvPRealMatch can both use them without a battle-controller-
// specific header existing. See that file's own doc comment on why this
// state can arrive before any battle exists.
bool8 PokePvP_IsRealOpponentReady(void);
void PokePvP_ClearRealOpponent(void);
u16 PokePvP_GetRealOpponentSpecies(void);
u8 PokePvP_GetRealOpponentLevel(void);
// POKEPVP (ADR-121): true the instant a real gateway pairing succeeds,
// well before PokePvP_IsRealOpponentReady() above can be -- lets
// main_menu.c's AUTO-MATCH handler skip its own wait entirely (zero
// timing change from before ADR-121) whenever no real pairing exists at
// all, instead of waiting unconditionally on every press just to find
// that out.
// POKEPVP (ADR-124): true when this launcher was started against a real
// gateway at all -- the signal AUTO-MATCH needs to tell "wait for a real
// opponent, and say so if none comes" apart from "there is no gateway, run
// the offline AI debug battle." See POKEPVP_MSG_ONLINE_MODE's doc comment
// (presentation_types.h) for the owner-reported bug this closes.
// POKEPVP (ADR-125): how many gEnemyParty slots hold a real, disclosed
// opponent Pokemon rather than an unrevealed placeholder. Set by
// StartPokePvPRealMatch; read by the mid-match switch resolver.
void PokePvP_SetOpponentRevealedCount(u8 count);
bool8 PokePvP_IsOnlineMode(void);
bool8 PokePvP_IsRealMatchPending(void);
void PokePvP_ClearRealMatchPending(void);
// POKEPVP (ADR-122): set TRUE by StartPokePvPRealMatch, FALSE by
// StartPokePvPDebugBattle -- both are the only two real entry points for a
// new BATTLE_TYPE_POKEPVP battle, so exactly one of them is authoritative
// per battle regardless of controller-install timing. Consulted by
// PokePvPOpponentBufferRunCommand (battle_controller_pokepvp.c) so a real
// match's battler 1 always waits on the mailbox instead of ever falling
// through to FireRed's native AI -- fixes a real bug found live where the
// opponent visibly played like the AI, not the actual remote human.
void PokePvP_SetRealMatchActive(bool8 active);
// POKEPVP (ADR-124): the trainer identity a real match's opponent is drawn
// and named with -- defined in battle_controller_pokepvp.c (that's where
// POKEPVP_MSG_REAL_OPPONENT_NAME is decoded), declared here so the two
// vendored FireRed read sites (battle_controller_opponent.c's trainer-pic
// handlers, battle_message.c's B_TXT_TRAINER1_CLASS/NAME cases) can ask
// for it instead of indexing gTrainers[gTrainerBattleOpponent_A], which for
// a PvP match is pinned to TRAINER_NONE on purpose. See those functions'
// own doc comment for why a real human opponent must not borrow a scripted
// NPC's name, sprite, or save flags.
u8 PokePvP_GetOpponentTrainerPicId(void);
u8 PokePvP_GetOpponentTrainerClass(void);
const u8 *PokePvP_GetOpponentTrainerName(void);
void StartOldManTutorialBattle(void);
void StartScriptedWildBattle(void);
void StartMarowakBattle(void);
void StartSouthernIslandBattle(void);
void StartLegendaryBattle(void);
void StartGroudonKyogreBattle(void);
void StartRegiBattle(void);
u8 BattleSetup_GetTerrainId(void);
u8 BattleSetup_GetBattleTowerBattleTransition(void);
const u8 *BattleSetup_ConfigureTrainerBattle(const u8 *data);
void ConfigureAndSetUpOneTrainerBattle(u8 trainerEventObjId, const u8 *trainerScript);
bool32 GetTrainerFlagFromScriptPointer(const u8 *data);
void SetUpTrainerMovement(void);
u8 GetTrainerBattleMode(void);
u16 GetRivalBattleFlags(void);
void SetBattledTrainerFlag(void);
bool8 HasTrainerBeenFought(u16 trainerId);
void SetTrainerFlag(u16 trainerId);
void ClearTrainerFlag(u16 trainerId);
void StartTrainerBattle(void);
void StartRematchBattle(void);
void ShowTrainerIntroSpeech(void);
const u8 *BattleSetup_GetScriptAddrAfterBattle(void);
const u8 *BattleSetup_GetTrainerPostBattleScript(void);
void ShowTrainerCantBattleSpeech(void);
void PlayTrainerEncounterMusic(void);
const u8 *GetTrainerALoseText(void);
const u8 *GetTrainerWonSpeech(void);

#endif // GUARD_BATTLE_SETUP_H
