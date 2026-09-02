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
