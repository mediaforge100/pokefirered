#ifndef GUARD_BATTLE_MAIN_H
#define GUARD_BATTLE_MAIN_H

#include "constants/abilities.h"

struct TrainerMoney
{
    u8 classId;
    u8 value;
};

struct MultiBattlePokemonTx
{
    /*0x00*/ u16 species;
    /*0x02*/ u16 heldItem;
    /*0x04*/ u8 nickname[POKEMON_NAME_LENGTH + 1];
    /*0x0F*/ u8 level;
    /*0x10*/ u16 hp;
    /*0x12*/ u16 maxhp;
    /*0x14*/ u32 status;
    /*0x18*/ u32 personality;
    /*0x1C*/ u8 gender;
    /*0x1D*/ u8 language;
};

#define TYPE_NAME_LENGTH 6
#define ABILITY_NAME_LENGTH 12

// defines for the u8 array gTypeEffectiveness
#define TYPE_EFFECT_ATK_TYPE(i)((gTypeEffectiveness[i + 0]))
#define TYPE_EFFECT_DEF_TYPE(i)((gTypeEffectiveness[i + 1]))
#define TYPE_EFFECT_MULTIPLIER(i)((gTypeEffectiveness[i + 2]))

// defines for the gTypeEffectiveness multipliers
#define TYPE_MUL_NO_EFFECT          0
#define TYPE_MUL_NOT_EFFECTIVE      5
#define TYPE_MUL_NORMAL             10
#define TYPE_MUL_SUPER_EFFECTIVE    20

// special type table Ids
#define TYPE_FORESIGHT  0xFE
#define TYPE_ENDTABLE   0xFF

// defines for the 'DoBounceEffect' function
#define BOUNCE_MON          0x0
#define BOUNCE_HEALTHBOX    0x1

extern const struct SpriteTemplate gUnknownDebugSprite;
extern const struct OamData gOamData_BattlerOpponent;
extern const struct OamData gOamData_BattlerPlayer;
extern const u8 gTypeNames[][TYPE_NAME_LENGTH + 1];
extern const u8 gStatusConditionString_PoisonJpn[8];
extern const u8 gStatusConditionString_SleepJpn[8];
extern const u8 gStatusConditionString_ParalysisJpn[8];
extern const u8 gStatusConditionString_BurnJpn[8];
extern const u8 gStatusConditionString_IceJpn[8];
extern const u8 gStatusConditionString_ConfusionJpn[8];
extern const u8 gStatusConditionString_LoveJpn[8];
extern const u8 *const gStatusConditionStringsTable[7][2];
extern const u8 gTypeEffectiveness[336];
extern const struct TrainerMoney gTrainerMoneyTable[];
extern const u8 *const gAbilityDescriptionPointers[ABILITIES_COUNT];
extern const u8 gAbilityNames[ABILITIES_COUNT][ABILITY_NAME_LENGTH + 1];

void CB2_InitBattle(void);
void BattleMainCB2(void);
void FreeRestoreBattleData(void);
void VBlankCB_Battle(void);
void SpriteCB_VsLetterDummy(struct Sprite *sprite);
void SpriteCB_VsLetterInit(struct Sprite *sprite);
void CB2_InitEndLinkBattle(void);
u32 GetBattleBgTemplateData(u8 arrayId, u8 caseId);
void SpriteCB_EnemyMon(struct Sprite *sprite);
void SpriteCallbackDummy_2(struct Sprite *sprite);
void SpriteCB_FaintOpponentMon(struct Sprite *sprite);
void SpriteCB_ShowAsMoveTarget(struct Sprite *sprite);
void SpriteCB_HideAsMoveTarget(struct Sprite *sprite);
void SpriteCB_AllyMon(struct Sprite *sprite);
void SetIdleSpriteCallback(struct Sprite *sprite);
void SpriteCB_FaintSlideAnim(struct Sprite *sprite);
void DoBounceEffect(u8 battler, u8 which, s8 delta, s8 amplitude);
void EndBounceEffect(u8 battler, u8 which);
void SpriteCB_PlayerThrowInit(struct Sprite *sprite);
void UpdatePlayerPosInThrowAnim(struct Sprite *sprite);
void BeginBattleIntroDummy(void);
void BeginBattleIntro(void);
void SwitchInClearSetData(void);
void FaintClearSetData(void);
void BattleTurnPassed(void);
u8 IsRunningFromBattleImpossible(void);
void UpdatePartyOwnerOnSwitch_NonMulti(u8 battler);
void SwapTurnOrder(u8 id1, u8 id2);
u8 GetWhoStrikesFirst(u8 battler1, u8 battler2, bool8 ignoreChosenMoves);
void RunBattleScriptCommands_PopCallbacksStack(void);
void RunBattleScriptCommands(void);
bool8 TryRunFromBattle(u8 battler);

/* POKEPVP (ADR-104): PokePvP_WaitForMailboxTurnResolution is defined in
 * rom/pvp-gen3/battle_controller_pokepvp.c and installed as
 * gBattleMainFunc in place of RunTurnActionsFunctions for a
 * BATTLE_TYPE_POKEPVP battle (battle_main.c's
 * CheckFocusPunch_ClearVarsBeforeTurnStarts). PokePvP_SetBattleOutcomeAndEndTurn
 * is defined here in battle_main.c and called from that same PvP
 * controller file's POKEPVP_MSG_BATTLE_OUTCOME handler, to end the
 * battle via FireRed's own real sEndTurnFuncsTable dispatch without ever
 * running RunTurnActionsFunctions. */
void PokePvP_WaitForMailboxTurnResolution(void);
void PokePvP_SetBattleOutcomeAndEndTurn(u8 outcome);
/* ADR-106: defined in battle_controller_pokepvp.c. TRUE once this battle
 * has actually consumed a real mailbox CHOICE_ACTION/CHOICE_MOVE record
 * (a fixture, or eventually a real server) -- FALSE (every new battle's
 * default) means plain local play (no fixture, no server) runs FireRed's
 * own real RunTurnActionsFunctions unmodified, same as before ADR-104.
 * Checked in CheckFocusPunch_ClearVarsBeforeTurnStarts instead of just
 * BATTLE_TYPE_POKEPVP, so a real button press in the FIGHT menu doesn't
 * hang forever waiting on a mailbox nothing is driving yet. */
bool8 PokePvP_ShouldSkipLocalResolution(void);
/* ADR-108: the counterpart to PokePvP_SetBattleOutcomeAndEndTurn for the
 * "this turn ended, the battle didn't" case -- called from
 * battle_controller_pokepvp.c's POKEPVP_MSG_TURN_CONTINUE handler.
 * Reproduces exactly the bookkeeping-only tail of BattleTurnPassed's own
 * gBattleOutcome == 0 branch (clearing per-turn scratch state,
 * resetting chosen actions, re-rolling gRandomTurnNumber) and installs
 * HandleTurnActionSelectionState as gBattleMainFunc -- deliberately
 * skips BattleTurnPassed's own DoFieldEndTurnEffects/
 * DoBattlerEndTurnEffects/HandleFaintedMonActions/
 * HandleWishPerishSongOnTurnEnd calls, since those are exactly the local
 * weather/status/faint computation this project's server is supposed to
 * own instead. Fainted-mon switch-in is explicitly out of scope (see
 * ADR-108) -- this only re-arms action selection for the next turn. */
void PokePvP_ContinueNextTurn(void);

#endif // GUARD_BATTLE_MAIN_H
