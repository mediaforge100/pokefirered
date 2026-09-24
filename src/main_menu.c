#include "global.h"
#include "gflib.h"
#include "scanline_effect.h"
#include "task.h"
#include "save.h"
#include "event_data.h"
#include "menu.h"
#include "link.h"
#include "oak_speech.h"
#include "overworld.h"
#include "battle_setup.h" // POKEPVP (ADR-079/080): StartPokePvPMenuMatch
#include "option_menu.h" // POKEPVP (ADR-112): OPTIONS reuses FireRed's own real screen
#include "naming_screen.h" // POKEPVP (ADR-113): PLAYER SETTINGS name entry
#include "trainer_pokemon_sprites.h" // POKEPVP (ADR-209): trainer-sprite picker preview (CreateTrainerPicSprite)
#include "pokepvp_team_builder.h" // POKEPVP (ADR-093): TEAM BUILDER
#include "pokepvp/mailbox.h" // POKEPVP (UI plan slice 2): ROM->host post-match action records (quoted-relative to src/, as battle_controller_pokepvp.c does)
#include "pokepvp/presentation_types.h" // POKEPVP (UI plan slice 2): POST_MATCH_ACTION message id
#include "post_match.h" // POKEPVP (UI plan slice 2): POST-MATCH screen buffer
#include "ready_check.h" // POKEPVP (UI plan slice 3): ready-check prompt buffer
#include "packs_catalog.h" // POKEPVP (UI plan slice 3): pack catalog + launch-config flags
#include "inbox.h" // POKEPVP (UI plan slice 4): challenge inbox buffer
#include "profile.h" // POKEPVP (UI plan slice 5): PROFILE screen buffer
#include "pokepvp/social.h" // POKEPVP (UI plan slice 6): SOCIAL screen buffer (friends/rivals/blocks)
#include "history.h" // POKEPVP (ADR-168): MATCH HISTORY buffer
// leaderboard.h intentionally NOT included here anymore (ADR-189) -- this
// file no longer has a LEADERBOARD screen; the buffer module itself is
// still built (rom/pvp-gen3/leaderboard.c/h), just unreferenced from the
// ROM's UI. See DrawPokePvPMenuItems's comment.
#include "list_menu.h" // POKEPVP (ADR-093): the move editor's scrolling lists
#include "data.h"        // POKEPVP (ADR-093): gSpeciesNames, gMoveNames
#include "pokemon.h"     // POKEPVP (ADR-096): gBattleMoves, for the move info panel
#include "battle_main.h" // POKEPVP (ADR-096): gTypeNames, for the move info panel
#include "pokemon_summary_screen.h" // POKEPVP (ADR-217): gMoveDescriptionPointers, for the move info panel's page 2
#include "pokemon_storage_system.h" // POKEPVP (ADR-217): BOX_NAME_LENGTH, for the ADD FRIEND naming-screen buffer
#include "constants/moves.h"
#include "constants/species.h" // POKEPVP (UI plan slice 5): NUM_SPECIES for the PROFILE top-species bounds
#include "constants/trainers.h" // POKEPVP (ADR-209): TRAINER_PIC_RED/LEAF/RS_BRENDAN_1/RS_MAY_1 for the sprite picker
#include "quest_log.h"
#include "mystery_gift_menu.h"
#include "strings.h"
#include "title_screen.h"
#include "help_system.h"
#include "pokedex.h"
#include "text_window.h"
#include "text_window_graphics.h"
#include "new_menu_helpers.h" // POKEPVP (main-menu backdrop redraw): DecompressAndCopyTileDataToVram
#include "constants/songs.h"

enum MainMenuType
{
    MAIN_MENU_NEWGAME = 0,
    MAIN_MENU_CONTINUE,
    MAIN_MENU_MYSTERYGIFT,
    // POKEPVP (ADR-085, D7): the real 5-item boot menu. Task_SetWin0BldRegsAndCheckSaveFile
    // now always selects this type -- NEWGAME/CONTINUE/MYSTERYGIFT above are FireRed's
    // originals, left compiled but unreachable (D7 has no Continue/Mystery Gift concept).
    MAIN_MENU_POKEPVP
};

enum MainMenuWindow
{
    MAIN_MENU_WINDOW_NEWGAME_ONLY = 0,
    MAIN_MENU_WINDOW_CONTINUE,
    MAIN_MENU_WINDOW_NEWGAME,
    MAIN_MENU_WINDOW_MYSTERYGIFT,
    // POKEPVP (ADR-085, extended ADR-188, reverted ADR-189): the five
    // real menu slots, top to bottom. Comments below reflect current
    // real behavior, not the ADR-085-era stub labels this enum was
    // first written with.
    MAIN_MENU_WINDOW_POKEPVP_0, // START MATCH
    MAIN_MENU_WINDOW_POKEPVP_1, // TEAM BUILDER
    MAIN_MENU_WINDOW_POKEPVP_2, // PROFILE
    MAIN_MENU_WINDOW_POKEPVP_3, // MATCH HISTORY
    MAIN_MENU_WINDOW_POKEPVP_4, // OPTIONS
    // POKEPVP (ADR-188, reverted ADR-189): a 6th slot ADR-188 briefly
    // used for LEADERBOARD. Kept (unused, always blank) rather than
    // removed -- see docs/adr/189 and DrawPokePvPMenuItems's comment.
    MAIN_MENU_WINDOW_POKEPVP_5,
    MAIN_MENU_WINDOW_ERROR,
    MAIN_MENU_WINDOW_COUNT
};

#define tMenuType  data[0]
#define tCursorPos data[1]
// POKEPVP (ADR-091): AUTO-MATCH/INVITE MATCH submenu cursor, separate from
// the top-level 5-item menu's tCursorPos so returning to the top menu
// doesn't need to remember/restore a shared field.
#define tSubCursorPos data[2]
// POKEPVP (Phase J v2): which submenu item opened the team selector.
#define POKEPVP_MATCH_MODE_AUTO     0u
#define POKEPVP_MATCH_MODE_INVITE   1u
#define POKEPVP_MATCH_MODE_PRACTICE 2u
// POKEPVP (UI plan slice 4): the team selector opened from the inbox's
// ACCEPT action -- its A-press sends the slot's team records + a
// CHALLENGE_ACTION accept instead of MATCH_REQUEST/MATCH_CONFIG.
#define POKEPVP_MATCH_MODE_ACCEPT_CHALLENGE 3u
// POKEPVP (UI plan slice 6): the team selector opened from a social
// list's CHALLENGE action -- its A-press sends the slot's team records
// + a SOCIAL_ACTION challenge instead of MATCH_REQUEST/MATCH_CONFIG.
#define POKEPVP_MATCH_MODE_CHALLENGE_TARGET 4u
// POKEPVP (UI plan slice 3, Phase C): the picker tree's explicit modes
// (Build Plan §8 item 2: Quick / Custom / Invite / Practice). MATCH_CONFIG
// carries these to the host; the submenu now hides rows whose server flag
// is off (POKEPVP_FLAG_*, packs_catalog.h).
#define POKEPVP_MATCH_MODE_QUICK_EARLY 11u
#define POKEPVP_MATCH_MODE_QUICK_ELITE 12u
#define POKEPVP_MATCH_MODE_CUSTOM_ELITE 13u
// Picker tree stages (tPickerStage): 0 = START MATCH submenu, 2 = pack
// picker, 3 = team selector. tPickerStage is write-only bookkeeping (never
// branched on), so this is documentation, not control flow. Stage 1 (the
// former battle-class picker) is retired -- ADR-192 removed that screen;
// the value is left unused rather than renumbered, so it stays a stable
// marker in any historical trace log that still names it.
#define POKEPVP_PICKER_STAGE_MENU 0u
#define POKEPVP_PICKER_STAGE_PACK 2u
#define POKEPVP_PICKER_STAGE_TEAM 3u
// Pack picker rows beyond the 5 catalog rows: RANDOM (the host picks a
// pack of this class). A dedicated RULES row was designed and dropped in
// the same slice: its static rules line (team size/level/ranked status,
// Build Plan §8 item 6) is rendered as the picker's summary line instead,
// leaving the row count at POKEPVP_PACKS_PER_CLASS packs + RANDOM -- an
// extra row would only have duplicated RANDOM's behavior, since A on it
// resolved to the same "unpicked" state (dead UI). If a richer full-screen
// rules view is ever wanted it is a new stage, not an extra row.
// Kept as its own literal (not `POKEPVP_PACKS_PER_CLASS`) because the two
// happen to move together only by content coincidence -- see
// packs_catalog.h's own count; update both together if that count changes.
#define POKEPVP_PICKER_ROW_RANDOM 4u
#define POKEPVP_PICKER_ROW_COUNT 5u

// POKEPVP (ADR-093): move-editor state. data[3..7] are unused by every
// other menu type here, and the editor is only ever reachable from the
// team list, so it does not have to coexist with anything.
#define tTeamSlot        data[3]
#define tMemberIndex     data[4]
#define tMoveSlot        data[5]
#define tListTaskId      data[6]
#define tListWindowId    data[7]

// POKEPVP (Phase J v2): which START MATCH submenu item led into the team
// selector (AUTO MATCH / INVITE MATCH / PRACTICE). The selector's A-press
// sends MATCH_REQUEST for the first two and PRACTICE_REQUEST for practice.
#define tSubMode         data[8]
#define tMGErrorMsgState data[9]
#define tMGErrorType     data[10]
// POKEPVP (UI plan slice 3): picker-tree state for the START MATCH flow
// (data[14]/data[15] were unused): which stage the walk is on, and the
// battle-class byte the QUICK flow carries into the pack picker and then
// into MATCH_CONFIG. data[3] (tTeamSlot) is free while this task chain
// owns the screen and is reused for the picked pack row.
#define tPickerStage     data[14]
#define tPickerClass     data[15]
// POKEPVP (UI plan slice 4): which inbox entry the inbox task is showing.
// data[14] (tPickerStage) belongs to the START MATCH picker tasks, which
// are never running while the inbox owns the screen -- ACCEPT hands off
// to the team selector without those tasks.
#define tInboxSlot       data[14]
// POKEPVP (UI plan slice 6, moved up from its original site alongside
// SendSocialAction/DrawSocialListItems for ADR-193/Gap 2's
// Task_PokePvPInviteTargetPicker, which needs it before that point in the
// file): which social list (0 friends, 1 rivals, 2 blocks) a list-showing
// task is working with. Same data[15] slot as tPickerClass above -- never
// live at the same time (the START MATCH picker tasks and the SOCIAL
// tasks are mutually exclusive screens), same reuse precedent this file
// already establishes for data[14]/data[15].
#define tSocialList      data[15]

// POKEPVP (ADR-096): the move info panel shown alongside the move-slot and
// movepool lists. tMoveInfoWindowId is WINDOW_NONE when no panel is open
// (the species/member lists don't get one) -- ClosePokePvPList checks this
// to know whether there is a second window to tear down. tLastInfoMoveId
// starts at -1 (no real move id is negative) so the first frame after
// opening always draws, then only redraws on an actual hover change.
#define tMoveInfoWindowId data[11]
#define tLastInfoMoveId   data[12]

// POKEPVP (ADR-217): which of the move info panel's two pages is showing
// (0 = stats, 1 = description). Lives on data[13] -- free within this
// task's own context (Task_PokePvPPickMoveSlot/Task_PokePvPPickMove never
// use tWaitFrames/tScreenDrawn/tPostMatchWaitFrames's slot, all of which
// belong to entirely different screens' task functions; same documented
// per-screen multiplexing precedent as tPickerStage/tInboxSlot/
// tSocialList above). Reset to 0 on open (OpenPokePvPList) and on every
// real hover change (UpdatePokePvPMoveInfo), so flipping to page 2 for
// one move never leaks into the next move hovered.
#define tMoveInfoPage data[13]

// POKEPVP (ADR-121): Task_PokePvPWaitForRealOpponent's own bounded-wait
// frame counter. Reuses tMGErrorMsgState for its 0/1/2 state machine, same
// shape as Task_PokePvPPrepareRoster's.
#define tWaitFrames data[13]

// POKEPVP (ADR-189): PROFILE / MATCH HISTORY / LEADERBOARD's own "have I
// drawn my content yet" latch, checked/set as gTasks[taskId].data[0] == 0
// / = 1 before this fix. data[0] IS tMenuType (see above) -- on the very
// first tick after Task_ExecuteMainMenuSelection dispatched into one of
// these three screens, tMenuType still held MAIN_MENU_POKEPVP (3), so the
// "== 0" guard was permanently false and the draw block never ran at all:
// the screen faded out, then back in over the *unerased* top-menu
// tilemap, looking exactly like a dead button. Worse, the unconditional
// "= 1" write corrupted tMenuType to 1 (MAIN_MENU_CONTINUE) the one time
// the guard's initial state ever let it through, breaking
// Task_UpdateVisualSelection's own tMenuType-gated redraw for the rest of
// the session. Multiplexed onto tWaitFrames's slot (data[13]) instead --
// none of these three screens ever waits on real-opponent polling, the
// same reuse precedent tPickerStage/tInboxSlot (data[14]) and
// tPickerClass/tSocialList (data[15]) already establish below. Explicitly
// zeroed at every dispatch site in Task_ExecuteMainMenuSelection rather
// than trusted to already be 0 -- a stale nonzero value can carry over
// from an earlier Task_PokePvPWaitForRealOpponent run in the same menu
// session.
#define tScreenDrawn data[13]

// POKEPVP (ADR-196): Task_WaitFadeAndPrintMainMenuText's own bounded wait
// for a POST_MATCH write that may still be in flight after a real battle
// (see the function itself). Safe to multiplex onto the same data[13]
// slot as tWaitFrames/tScreenDrawn above -- this function only ever runs
// once per CB2_InitMainMenu entry, immediately after MainMenuGpuInit's
// ResetTasks()+CreateTask() (fresh, zeroed task data), strictly before
// any of tWaitFrames's or tScreenDrawn's own screens are ever dispatched
// into on this task. 0 means "not waiting" (either no battle just ended,
// or the wait already resolved); any nonzero value is "armed" and counts
// up from 1, never wrapping back to 0 while waiting (see the >=
// POKEPVP_POST_MATCH_ARRIVAL_WAIT_FRAMES check below).
#define tPostMatchWaitFrames data[13]

static bool32 MainMenuGpuInit(u8 a0);
static void Task_SetWin0BldRegsAndCheckSaveFile(u8 taskId);
static void PrintSaveErrorStatus(u8 taskId, const u8 *str);
static void Task_SaveErrorStatus_RunPrinterThenWaitButton(u8 taskId);
static void Task_SetWin0BldRegsNoSaveFileCheck(u8 taskId);
static void Task_WaitFadeAndPrintMainMenuText(u8 taskId);
static void Task_PrintMainMenuText(u8 taskId);
static void Task_WaitDma3AndFadeIn(u8 taskId);
static void Task_UpdateVisualSelection(u8 taskId);
static void Task_HandleMenuInput(u8 taskId);
static void Task_ExecuteMainMenuSelection(u8 taskId);
static void Task_MysteryGiftError(u8 taskId);
static void Task_PokePvPMenuStub(u8 taskId);
static void Task_PokePvPMatchHistory(u8 taskId);
static void Task_PokePvPReturnToTopMenuFromHistory(u8 taskId);
// POKEPVP (ADR-189): Task_PokePvPLeaderboard / Task_PokePvPReturnToTop-
// MenuFromLeaderboard (ADR-188) removed with the top menu's 6th row --
// see docs/adr/189 and DrawPokePvPMenuItems's own comment.
static void Task_PokePvPNoOpponentFound(u8 taskId); // POKEPVP (ADR-124)
// POKEPVP (UI plan slice 4, Phase H): the challenge inbox -- surfaced
// from Task_HandleMenuInput while the player idles on the top menu and a
// challenge is buffered (and not snoozed). ACCEPT hands to the team
// selector; DECLINE/BLOCK send a CHALLENGE_ACTION; B snoozes.
static void Task_PokePvPInbox(u8 taskId);
// POKEPVP (UI plan slice 5, Phase E): the PROFILE screen -- tag, stats,
// most-used species, recent opponents. Reached from the top menu's
// PROFILE row (cursor 2); B returns to the top menu.
static void Task_PokePvPProfile(u8 taskId);
static void DrawProfileItems(u8 selectedIdx);
static void Task_PokePvPPlayerMenu(u8 taskId);
static void DrawPlayerMenuItems(u8 selectedIdx);
// POKEPVP (UI plan slice 6, Phase G): the SOCIAL screen (friends /
// rivals / blocks / name) and the per-list screens with per-row
// CHALLENGE / REMOVE / UNBLOCK actions.
static void Task_PokePvPSocial(u8 taskId);
static void Task_PokePvPSpritePicker(u8 taskId);
static void Task_PokePvPSocialList(u8 taskId);
static void Task_PokePvPSocialRowMenu(u8 taskId);
static void Task_PokePvPSocialRowMenuDismiss(u8 taskId);
static void Task_PokePvPAddFriendResultDismiss(u8 taskId);
static void Task_PokePvPAddFriendPrompt(u8 taskId);
static void ShowAddFriendResultIfPending(u8 taskId);
static void CB2_PokePvPAddFriendNameEntered(void);
static void DrawSocialItems(u8 selectedIdx);
static void DrawSocialListItems(u8 list, u8 selectedIdx, bool8 addFriendFirst);
static void DrawSocialRowMenuItems(u8 list, u8 selectedIdx);
// POKEPVP (UI plan slice 2, Phase I): the post-match screen -- result +
// REMATCH / PLAY AGAIN / ADD RIVAL / EXIT, reached from
// Task_WaitFadeAndPrintMainMenuText whenever a post-match session is
// pending (a match just ended; the launcher holds the authoritative
// result). The wait tasks below park the player while the host acts on
// their choice (rematch/requeue = waiting-for-opponent, add-rival =
// API round trip), and surface POKEPVP_MSG_POST_MATCH_RESULT messages.
static void Task_PokePvPPostMatch(u8 taskId);
static void Task_PokePvPPostMatchWait(u8 taskId);
static void Task_PokePvPPostMatchRivalWait(u8 taskId);
static void Task_PokePvPReturnToTopMenuFromPostMatch(u8 taskId);
static void SendLeaveQueue(void);
static void SendSetSprite(u8 spriteId);
static void DrawPokePvPMenuItems(u8 selectedIdx);
// POKEPVP (ADR-091): START MATCH -> AUTO-MATCH/INVITE MATCH submenu.
static void DrawStartMatchSubmenuItems(u8 selectedIdx);
static u8 StartMatchRowCount(void);
static u8 StartMatchModeForRow(u8 row);
// POKEPVP (UI plan slice 3, simplified ADR-192): the START MATCH mode
// tree -- pack picker (QUICK), plus the ready-check prompt state; see each
// function below. The separate EARLY/ELITE class-picker screen this used
// to have is gone (ADR-192): QUICK EARLY/QUICK ELITE now go straight to
// their own pack list.
static void DrawPackPickerItems(u8 selectedIdx, u8 battleClass);
static void Task_PokePvPPackPicker(u8 taskId);
static void Task_PokePvPStartMatchSubmenu(u8 taskId);
// POKEPVP (ADR-193, Gap 2, extended HANDOFF item 23 follow-up): INVITE
// MATCH's real target picker -- a friends/rivals-list reuse of the SOCIAL
// screen's own list rendering (DrawSocialListItems, forward-declared below
// at its own existing site) and its existing CHALLENGE pipeline
// (tSocialList/tInboxSlot -> POKEPVP_MATCH_MODE_CHALLENGE_TARGET ->
// Task_PokePvPTeamSelector), not a second targeting mechanism. See
// Task_PokePvPInviteTargetPicker's own doc comment for the full design.
// Task_PokePvPInviteTargetTypePicker is the new FRIENDS/RIVALS chooser in
// front of it, mirroring Task_PokePvPSocial's own two-level shape.
static void DrawInviteTargetTypeItems(u8 selectedIdx);
static void Task_PokePvPInviteTargetTypePicker(u8 taskId);
static void Task_PokePvPInviteTargetPicker(u8 taskId);
// POKEPVP (ADR-192): shared match-start sequence, defined alongside
// Task_PokePvPTeamSelector below but called earlier in the file too (by
// Task_PokePvPPackPicker's QUICK auto-select path) -- forward-declared here
// for that call.
static void StartPokePvPMatchWithTeam(u8 taskId, u8 slot);
// POKEPVP (UI plan slice 3): ready-check prompt helpers, used by the
// wait tasks above before their own definition below.
static bool8 PokePvP_IsReadyCheckCancelled(void);
static bool8 TickReadyCheckPrompt(u8 taskId);
static void Task_PokePvPReturnToTopMenuFromSubmenu(u8 taskId);
// POKEPVP (ADR-095): AUTO-MATCH -> team-selector screen, a deliberately
// separate picker from TEAM BUILDER's own team-slot list below -- no
// EDIT TEAM/EDIT MOVES submenu, A here starts the match directly.
static void DrawOneSelectorRow(u8 windowId, u8 slot, bool8 selected);
static void DrawTeamSelectorItems(u8 selectedIdx);
static void Task_PokePvPTeamSelector(u8 taskId);
// POKEPVP (ADR-093): TEAM BUILDER -> team-slot list.
static void DrawOneTeamRow(u8 windowId, u8 slot, bool8 selected);
static void DrawTeamListItems(u8 selectedIdx);
static void Task_PokePvPTeamList(u8 taskId);
static void Task_PokePvPReturnToTopMenuFromTeamList(u8 taskId);
static void Task_PokePvPPrepareRoster(u8 taskId);
// POKEPVP (ADR-093): the move editor -- slot submenu, then three nested
// ListMenu pickers (member -> move slot -> legal move).
static void DrawSlotMenuItems(u8 selectedIdx);
static void Task_PokePvPSlotMenu(u8 taskId);
// Team management options: RENAME's naming-screen round trip, COPY TO...'s
// destination picker, and the two new ROM -> host sends.
static void CB2_PokePvPTeamRenamed(void);
static void Task_PokePvPTeamCopyPicker(u8 taskId);
static void SendTeamRename(u8 slot, const u8 *name);
static void SendActiveTeamSlot(u8 slot);
static void Task_PokePvPPickMember(u8 taskId);
static void Task_PokePvPReturnToTeamListFromSlotMenu(u8 taskId);
static void Task_PokePvPEmptyTeamMessage(u8 taskId);
static void Task_PokePvPPickMoveSlot(u8 taskId);
static void Task_PokePvPPickMove(u8 taskId);
static bool8 AllocPokePvPList(void);
static void OpenPokePvPList(u8 taskId, u16 count, bool8 withMoveInfo);
// POKEPVP (ADR-096): the move info panel -- Type/Power/Accuracy/PP for the
// currently-hovered row, in the unused ~80px right of the move-slot and
// movepool lists (not shown for the member/species list).
static void DrawPokePvPMoveInfo(u8 windowId, u16 move, u8 page);
static u16 GetHoveredMoveId(u8 taskId, bool8 isMoveSlotList);
static void UpdatePokePvPMoveInfo(u8 taskId, bool8 isMoveSlotList);
static void ClosePokePvPList(u8 taskId);
static void ReturnToSlotMenu(u8 taskId);
static void BuildMemberList(u8 taskId);
static void BuildMoveSlotList(u8 taskId);
static u16 BuildLegalMoveList(u8 taskId);
static void Task_ReturnToTileScreen(u8 taskId);
static void MoveWindowByMenuTypeAndCursorPos(u8 menuType, u8 cursorPos);
static bool8 HandleMenuInput(u8 taskId);
static void PrintMessageOnWindow4(const u8 *str);
static void PrintContinueStats(void);
static void PrintPlayerName(void);
static void PrintPlayTime(void);
static void PrintDexCount(void);
static void PrintBadgeCount(void);
static void LoadUserFrameToBg(u8 bgId);
static void SetStdFrame0OnBg(u8 bgId);
static void MainMenu_DrawWindow(const struct WindowTemplate * template);
static void MainMenu_EraseWindow(const struct WindowTemplate * template);

// POKEPVP (ADR-093): set before the Team Builder hands off to the PC box
// screen, consumed on the way back in. The box screen returns through
// CB2_InitMainMenu (a full, self-contained re-init -- the only clean way
// back into this screen from a foreign CB2), which would otherwise land
// the player on the top-level menu, two levels away from where they were.
static EWRAM_DATA bool8 sPokePvPReturnToTeamList = FALSE;

// POKEPVP (ADR-233, owner-reported live: "the ADD FRIEND error only
// shows after re-entering the FRIENDS menu, not right after submitting
// the username"): same shape and same reason as
// sPokePvPReturnToTeamList just above -- set right before
// CB2_PokePvPAddFriendNameEntered hands off to CB2_InitMainMenu (the
// naming screen's own round trip always lands there, never directly back
// into this screen), consumed in Task_UpdateVisualSelection to skip the
// top menu entirely and land straight back on FRIENDS with the result
// message already showing.
static EWRAM_DATA bool8 sPokePvPReturnToSocialFriends = FALSE;

// POKEPVP (ADR-238): same shape again -- set right before the PLAYER
// submenu's own NAME action hands off to CB2_InitMainMenu via
// DoNamingScreen, consumed in Task_UpdateVisualSelection to land straight
// back on the PLAYER submenu (SPRITE/NAME) instead of the top menu.
static EWRAM_DATA bool8 sPokePvPReturnToPlayerMenu = FALSE;

/* POKEPVP (UI plan slice 4): set when the player dismisses the challenge
 * inbox with B ("later"); cleared on every fresh menu init
 * (MainMenuGpuInit) so an unanswered challenge re-surfaces at the next
 * menu entry but never nags the idle frame loop. ACCEPT/DECLINE/BLOCK
 * act on the entry itself (removing it), so they leave this unset. */
static EWRAM_DATA bool8 gPokePvPInboxSnoozed = FALSE;

/* POKEPVP (UI plan slice 2): the one mailbox per PokePvP battle, owned
 * by the ROM at the linker-reserved gPokePvPMailbox symbol (ld_script.ld
 * -- see battle_controller_pokepvp.c's identical extern for the full
 * contract). main_menu.c only ever *writes* rom->host records (post-match
 * actions); the presentation pump is the reader. */
extern PokePvPMailbox gPokePvPMailbox;

static const u8 sString_Dummy[] = _("");
static const u8 sString_Newline[] = _("\n");
// POKEPVP (ADR-079/080, D7 step 1): replaces "NEW GAME" -- this screen's
// existing New-Game slot is retargeted to StartPokePvPMenuMatch instead
// of StartNewGameScene (see Task_ExecuteMainMenuSelection below), and
// the label needs to match. A local string, not an edit to the shared
// gText_NewGame (strings.c), since that constant may be referenced
// elsewhere in ways this change has no business touching.
static const u8 sText_StartMatch[] = _("START MATCH");
// alpha-build-plan P6 (docs/adr/263): a restrained identity tag for the
// top menu's own unused 6th row (window 5) -- see DrawPokePvPMenuItems's
// own comment for why this window, not a new one.
static const u8 sText_Wordmark[] = _("POKEPVP");
// POKEPVP (ADR-085, D7): the real five-item menu. Team Builder/Player
// Settings/Leaderboard/Options are stubs (Task_PokePvPMenuStub) -- real
// implementations are Phase 5-8 work, out of scope here (ADR-079's own
// "Non-goals").
static const u8 sText_TeamBuilder[] = _("TEAM BUILDER");
// Renamed from PLAYER SETTINGS / LEADERBOARD to match the owner's
// BattleDex creative (pokepvp_title1.png) -- labels only, same stubs.
static const u8 sText_Profile[] = _("PROFILE");
static const u8 sText_MatchHistory[] = _("MATCH HISTORY");
static const u8 sText_HistoryWSep[] = _("   W: ");
static const u8 sText_HistoryL[] = _("  L: ");
static const u8 sText_HistoryEmpty[] = _("No matches yet.");
// POKEPVP (ADR-238): divider label ahead of the per-opponent list, now
// that MATCH HISTORY also shows PROFILE's old tag/stats/species/recent
// rows above it.
static const u8 sText_MatchHistoryDivider[] = _("RECORD:");
// POKEPVP (ADR-189): sText_Leaderboard/sText_LeaderboardRSep/
// sText_LeaderboardEmpty (ADR-188) removed with the top menu's 6th row
// and Task_PokePvPLeaderboard -- see docs/adr/189.
static const u8 sText_Options[] = _("OPTIONS");
static const u8 sText_NotYetImplemented[] = _("Not yet implemented.");
// POKEPVP (UI plan slice 2, Phase I): the post-match screen -- result
// line and the four actions the Build Plan §14 post-match set defines
// (Replay stays hidden until Phase K's playback gate passes). " vs "
// joins result and opponent name; the name/tag themselves are charmap
// text already (post_match.c converts), and the line is clipped to the
// window's 24 tiles rather than wrapping into the 16px line below.
static const u8 sText_PostMatchWin[] = _("YOU WIN");
static const u8 sText_PostMatchLose[] = _("YOU LOSE");
static const u8 sText_PostMatchDraw[] = _("DRAW");
static const u8 sText_PostMatchVs[] = _(" vs ");
static const u8 sText_Rematch[] = _("REMATCH");
static const u8 sText_PlayAgain[] = _("PLAY AGAIN");
static const u8 sText_AddRival[] = _("ADD RIVAL");
static const u8 sText_Exit[] = _("EXIT");
static const u8 sText_AddingRival[] = _("Adding rival…");
static const u8 sText_RivalAdded[] = _("Rival added.");
static const u8 sText_OpponentUnavailable[] = _("Opponent unavailable.");
static const u8 sText_TooManyRequests[] = _("Too many requests.");
static const u8 sText_RequestFailed[] = _("Request failed.");
// POKEPVP (ADR-091, D7 refinement): START MATCH now opens a submenu
// instead of acting directly. AUTO-MATCH is the only one wired to real
// (well, real-server-integration-pending -- see battle_setup.c) behavior;
// INVITE MATCH is a stub, same pattern as ADR-085's four top-level stubs.
// POKEPVP (ADR-093): the team-slot list reuses the same five windows the
// top-level menu and the START MATCH submenu already use, so it needs no
// new window templates and no new GPU setup -- the same reuse that made
// ADR-091's submenu safe.
static const u8 sText_Team[] = _("TEAM ");
static const u8 sText_TeamEmpty[] = _("EMPTY");
// POKEPVP (ADR-095): the selector's own row label -- deliberately not the
// builder's exact "x/6" member count. A picker only needs to say whether a
// team can be used at all.
static const u8 sText_TeamReady[] = _("READY");
// POKEPVP (ADR-093): shown while the legal-species roster is built into
// the PC boxes -- see PokePvPTeamBuilder_BuildRosterStep for why that is
// not instantaneous and must not be done in one frame.
static const u8 sText_PreparingTeamBuilder[] = _("Preparing team builder…");
// POKEPVP (ADR-121): shown while Task_PokePvPWaitForRealOpponent gives a
// real gateway-paired opponent a bounded chance to show up before falling
// back to the debug battle.
// POKEPVP (ADR-129): not static any more -- battle_controller_player.c
// reuses this exact string for the in-battle wait too (see
// PrintLinkStandbyMsg's own POKEPVP note), rather than duplicating the
// same text as a second constant.
const u8 gText_PokePvPWaitingForOpponent[] = _("Waiting for opponent…");
// POKEPVP (ADR-124): the AUTO-MATCH wait's honest timeout message.
static const u8 sText_NoOpponentFound[] = _("No opponent found.");
// POKEPVP (ADR-207, HANDOFF item 7): the AUTO-MATCH/INVITE wait's honest
// decline message -- distinct text from sText_NoOpponentFound just above
// so a real, immediate decline never reads as an indistinguishable timeout.
static const u8 sText_ChallengeDeclined[] = _("Challenge declined.");
// ADR-242: distinct text for the gateway's own opponent_busy rejection --
// the target is already in another match/queue right now, not a real
// decline and not unreachable.
static const u8 sText_OpponentBusy[] = _("Opponent is busy.");
// POKEPVP (ADR-193, Gap 2): INVITE MATCH's target picker, empty-list case
// -- same "say so instead of doing nothing" shape as sText_TeamIsEmpty.
static const u8 sText_NoFriendsToInvite[] = _("Add a FRIEND from SOCIAL first.");
static const u8 sText_NoRivalsToInvite[] = _("Add a RIVAL from SOCIAL first.");
static const u8 sText_EditTeam[] = _("EDIT TEAM");
static const u8 sText_EditMoves[] = _("EDIT MOVES");
// Team management options (owner ask): DrawSlotMenuItems' own explicit
// BACK row (sText_Back) is gone -- these five fill all its rows instead.
static const u8 sText_RenameTeam[] = _("RENAME");
static const u8 sText_CopyTeamTo[] = _("COPY TO...");
static const u8 sText_SetActiveTeam[] = _("SET AS ACTIVE");
static const u8 sText_TeamIsEmpty[] = _("This team has no POKéMON yet.");
// The empty move slot, and the "clear this slot" row. One dash, used as
// both -- a slot showing "-" and a choice reading "-" are the same idea.
static const u8 sText_NoMove[] = _("-");
// POKEPVP (ADR-096): the move info panel's field labels.
static const u8 sText_MoveInfoType[] = _("TYPE");
static const u8 sText_MoveInfoPower[] = _("POWER");
static const u8 sText_MoveInfoAcc[] = _("ACC.");
static const u8 sText_MoveInfoPP[] = _("PP");
// POKEPVP (ADR-099): the panel's header stripe.
static const u8 sText_MoveInfoHeader[] = _("MOVE INFO");
static const u8 sText_MoveInfoPage1[] = _("1/2"); // POKEPVP (ADR-217)
static const u8 sText_MoveInfoPage2[] = _("2/2"); // POKEPVP (ADR-217)
static const u8 sText_AutoMatch[] = _("AUTO-MATCH");
// POKEPVP (UI plan slot 3, Phase C): the START MATCH submenu now serves
// the Build Plan §8 mode tree -- QUICK EARLY / QUICK ELITE (battle class
// picker -> 5 packs + RANDOM), CUSTOM ELITE (saved team), INVITE
// MATCH, PRACTICE. Rows whose server feature flag is off stay hidden.
static const u8 sText_QuickEarly[] = _("QUICK EARLY");
static const u8 sText_QuickElite[] = _("QUICK ELITE");
static const u8 sText_CustomElite[] = _("CUSTOM ELITE");
static const u8 sText_InviteMatch[] = _("INVITE MATCH");
static const u8 sText_PracticeMatch[] = _("PRACTICE");
// POKEPVP (owner ask, 2026-09-13, item 6): the START MATCH submenu never
// told the player what each row actually means before committing to it
// (team size / level / premade-vs-own-team). One static description per
// mode, shown in the ERROR band below the row list -- same window/spot
// the pack picker's own rules line already uses, just for this screen's
// own five rows instead of a hovered pack.
static const u8 sText_ModeDescQuickEarly[] = _("3v3, Level 20, Premade");
static const u8 sText_ModeDescQuickElite[] = _("6v6, Level 100, Premade");
static const u8 sText_ModeDescCustomElite[] = _("6v6, Level 100, Custom");
static const u8 sText_ModeDescInvite[] = _("6v6, Level 100, Custom");
static const u8 sText_ModeDescPractice[] = _("6v6, Level 100, Practice");
// POKEPVP (UI plan slice 3): the pack-picker rows. (ADR-192: the separate
// "EARLY PACKS"/"ELITE PACKS" class-picker header strings this file used
// to carry are gone along with that now-removed screen.)
static const u8 sText_RandomPack[] = _("RANDOM");
// POKEPVP (owner playtest round 4, ADR-192): this overview line used to be
// one fixed string shown for both classes -- correct for EARLY (the
// pokepvp-frlg-kanto-early format really is teamSize 3, formats/
// pokepvp-frlg-kanto-early-v1.yaml) but wrong for ELITE, whose packs
// (formats/battle-packs/elite/*.yaml) all declare
// formatId: pokepvp-frlg-kanto-singles -- teamSize 6, per that format's own
// YAML. Split into a per-class pair so the label always matches what the
// class's own packs actually are, not just what EARLY's happens to be.
// POKEPVP (owner ask, 2026-09-13): dropped "FROZEN" (a leftover from an
// earlier draft, never a real ROM/protocol guarantee this text needs to
// assert) and fixed EARLY's own level -- ADR-219 moved
// pokepvp-frlg-kanto-early to level 20 (`defaultLevel`/`levelRange`
// 100 -> 20) but this literal string was never updated to match, so it
// was quietly showing the wrong level ever since. ELITE is genuinely
// still level 100 -- only the FROZEN suffix drops there.
static const u8 sText_QuickRulesLineEarly[] = _("3v3 LVL20");
static const u8 sText_QuickRulesLineElite[] = _("6v6 LVL100");
// POKEPVP (UI plan slice 3): the ready-check prompt (Build Plan §8 item
// 2 -- both players must confirm within the window; the gateway's
// timeout frame budget is held ROM-side by ready_check.c).
// POKEPVP (ADR-199, owner UX report): B here has never answered a plain
// "no" to a yes/no question -- both call sites (TickReadyCheckPrompt) send
// SendReadyChoice(0) *and* return the player to their previous screen
// (the START MATCH submenu), the same shape as every other cancel in this
// file. "CANCEL" says what actually happens; "NO" implied the prompt
// would just reappear or continue waiting, which it doesn't.
static const u8 sText_ReadyPrompt[] = _("READY?  A=YES B=CANCEL");
// HANDOFF item 37 (owner UX ask, deferred at ADR-199 pending a way to
// live-verify a timing change without recreating a real match -- the
// firered-real-match-slice recalibration this same session did (see
// SIX_TEAM_SCRIPT's own comment) now gives exactly that harness, so this
// is built and live-verified here rather than staying deferred).
static const u8 sText_ReadyForBattle[] = _("READY FOR BATTLE!");
// POKEPVP (UI plan slice 4): the challenge inbox (Build Plan §13 item 3:
// challenger identity, category, expiry, Accept, Decline, Block). The
// expiry clock is the gateway's own (late accepts are rejected there);
// the ROM shows the requester and the matching type line.
static const u8 sText_ChallengeFrom[] = _("CHALLENGE FROM");
static const u8 sText_RematchLabel[] = _("REMATCH");
static const u8 sText_EarlyLabel[] = _("QUICK EARLY");
static const u8 sText_EliteLabel[] = _("QUICK ELITE");
static const u8 sText_Accept[] = _("ACCEPT");
static const u8 sText_Decline[] = _("DECLINE");
static const u8 sText_BlockPlayer[] = _("BLOCK");
static const u8 sText_ChallengeBlocked[] = _("Player blocked.");
// POKEPVP (UI plan slice 5): the PROFILE screen (Build Plan §10: ID,
// display name, stats, most-used Pokemon, recent opponents). The sprite
// picker is deferred; the sprite id is shown as a number.
static const u8 sText_ProfileHeader[] = _("PROFILE");
static const u8 sText_ProfileW[] = _("W:");
static const u8 sText_ProfileL[] = _("L:");
static const u8 sText_ProfileT[] = _("T:");
static const u8 sText_ProfileM[] = _("M:");
static const u8 sText_ProfileTop[] = _("TOP: ");
static const u8 sText_ProfileRecent[] = _("RECENT: ");
static const u8 sText_ProfileEmpty[] = _("No matches yet.");
static const u8 sText_ProfileStatSep[] = _("  ");
static const u8 sText_ProfileSpeciesSep[] = _(", ");
static const u8 sText_ProfileNoTag[] = _("(no tag yet)");
static const u8 sText_ProfileNoSpecies[] = _("(none yet)");
static const u8 sText_ProfileNoOpponent[] = _("(none yet)");
// POKEPVP (UI plan slice 6): the SOCIAL screen (Build Plan §12) --
// friends / rivals / blocks lists with per-row CHALLENGE and
// REMOVE/UNBLOCK, plus the NAME action moved here from the old
// PLAYER SETTINGS row. Presence shows as a letter after a friend's tag
// (O online, S searching, B in battle, Y busy, - offline).
static const u8 sText_Social[] = _("SOCIAL");
static const u8 sText_Friends[] = _("FRIENDS");
static const u8 sText_Rivals[] = _("RIVALS");
static const u8 sText_Blocks[] = _("BLOCKS");
// POKEPVP (ADR-238, owner-directed PROFILE restructure): PROFILE's own
// two real rows, and the PLAYER submenu's own two -- see Task_PokePvP
// Profile/Task_PokePvPPlayerMenu's own doc comments.
static const u8 sText_ProfileSocialRow[] = _("SOCIAL");
static const u8 sText_ProfilePlayerRow[] = _("PLAYER");
static const u8 sText_PlayerMenuSprite[] = _("SPRITE");
static const u8 sText_PlayerMenuName[] = _("NAME");
static const u8 sText_Challenge[] = _("CHALLENGE");
static const u8 sText_Remove[] = _("REMOVE");
static const u8 sText_Unblock[] = _("UNBLOCK");
static const u8 sText_Empty[] = _("EMPTY");
static const u8 sText_Removed[] = _("Removed.");
static const u8 sText_ChallengeSent[] = _("Challenge sent.");
// POKEPVP (ADR-217): SOCIAL FRIENDS' "ADD FRIEND" row + its naming-screen
// round trip's real response cases.
static const u8 sText_AddFriendRow[] = _("ADD FRIEND");
static const u8 sText_AddFriendPrompt[] = _("Sending...");
static const u8 sText_AddFriendSuccess[] = _("Friend added!");
static const u8 sText_AddFriendUnknown[] = _("No player has\nthat username.");
static const u8 sText_AddFriendSelf[] = _("You can't add\nyourself.");
static const u8 sText_AddFriendBlocked[] = _("Couldn't add\nthat player.");
static const u8 sText_AddFriendFailed[] = _("No response.\nTry again later.");
// POKEPVP (ADR-226): the one real invalid-input case left once the
// numeric-only requirement was dropped is an empty/all-unconvertible
// result (nothing legible was typed) -- BOX_NAME_LENGTH itself already
// bounds the naming screen to 8 characters, so "too long" can't happen.
static const u8 sText_AddFriendInvalid[] = _("Enter a\nusername.");
// POKEPVP (ADR-224 follow-up): shown before the naming screen opens --
// ADR-224 named "no on-screen indication of what to type until after a
// bad entry" as a real, small, unbuilt gap. NAMING_SCREEN_BOX's own
// vendored title text (gText_BoxName, "BOX's name?") is shared with Team
// Builder's RENAME and can't be changed for just this use without editing
// naming_screen.c (prohibition #16) -- so the explanation is printed here,
// on SOCIAL's own screen, immediately before the handoff instead.
static const u8 sText_AddFriendLeadIn[] = _("Enter the exact\nusername to add.");
static const u8 sText_PresenceOnline[] = _("O");
static const u8 sText_PresenceSearching[] = _("S");
static const u8 sText_PresenceBattle[] = _("B");
static const u8 sText_PresenceBusy[] = _("Y");

static const struct WindowTemplate sWindowTemplate[] = {
    [MAIN_MENU_WINDOW_NEWGAME_ONLY] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 1,
        .width = 24,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x001
    }, 
    [MAIN_MENU_WINDOW_CONTINUE] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 1,
        .width = 24,
        .height = 10,
        .paletteNum = 15,
        .baseBlock = 0x001
    }, 
    [MAIN_MENU_WINDOW_NEWGAME] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 13,
        .width = 24,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x0f1
    }, 
    [MAIN_MENU_WINDOW_MYSTERYGIFT] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 17,
        .width = 24,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x121
    }, 
    // POKEPVP (main-menu backdrop redraw): five equal slots, packed tight
    // (2 tiles/16px each, no gap) into screen rows 6-15. Previously
    // 1,5,9,13,17 with gaps spanning the whole screen (ADR-085); Move-
    // WindowByMenuTypeAndCursorPos's MAIN_MENU_POKEPVP case must stay in
    // lockstep with this spacing. baseBlocks unchanged -- they address
    // tile storage, not screen position, so the reflow doesn't disturb
    // them. Rows 0-5/16-19 previously held the BG2 backdrop art (removed,
    // ADR-186) and are now simply blank -- free for a 6th row if a future
    // slice needs one.
    [MAIN_MENU_WINDOW_POKEPVP_0] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 6, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x151
    },
    [MAIN_MENU_WINDOW_POKEPVP_1] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 8, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x181
    },
    [MAIN_MENU_WINDOW_POKEPVP_2] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 10, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x1b1
    },
    [MAIN_MENU_WINDOW_POKEPVP_3] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 12, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x1e1
    },
    [MAIN_MENU_WINDOW_POKEPVP_4] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 14, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x211
    },
    // POKEPVP (ADR-188): a 6th slot for LEADERBOARD, packed the same way
    // as POKEPVP_0..4 -- rows 16-17 were freed by ADR-186's backdrop
    // removal. Only the top-menu screen (DrawPokePvPMenuItems) uses this
    // window; PROFILE's own screen deliberately does not (its ERROR-band
    // use for extra recent opponents would visually collide with these
    // same rows -- see Task_PokePvPProfile's comment).
    [MAIN_MENU_WINDOW_POKEPVP_5] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 16, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x241
    },
    [MAIN_MENU_WINDOW_ERROR] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 15,
        .width = 24,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x001 // unchanged from original -- proven safe to alias CONTINUE's block, mutually exclusive draw
    },
    [MAIN_MENU_WINDOW_COUNT] = DUMMY_WIN_TEMPLATE
};

// POKEPVP (menu redesign): geometry-only -- never AddWindow'd, just handed
// to MainMenu_DrawWindow to draw ONE frame around the whole 5-row block
// (rows 6-15) instead of a separate border per row. tilemapLeft/Top/width/
// height must bound the POKEPVP_0..4 windows exactly (left=3, top=6,
// spanning to top+height=16, matching POKEPVP_4's tilemapTop 14 + height 2).
// Deliberately NOT widened for ADR-188's 6th row (window 5) -- every
// *other* screen in this file still only ever populates 5 rows and
// shares this exact template, and widening it to 12 was found (via a
// golden-slice regression, see docs/adr/188) to corrupt those screens'
// own frame graphics elsewhere in this VRAM-tile-budget-tight ROM.
// DrawPokePvPMenuItems alone uses sPokePvPMenuPanelTemplateWide below.
static const struct WindowTemplate sPokePvPMenuPanelTemplate = {
    .bg = 0, .tilemapLeft = 3, .tilemapTop = 6, .width = 24, .height = 10
};

// POKEPVP (ADR-188): the top-menu screen's own wider panel, covering the
// real 6th row (window 5) ADR-186's backdrop removal freed. Kept
// separate from sPokePvPMenuPanelTemplate above rather than widening it
// globally -- see that template's own comment for why.
static const struct WindowTemplate sPokePvPMenuPanelTemplateWide = {
    .bg = 0, .tilemapLeft = 3, .tilemapTop = 6, .width = 24, .height = 12
};

// POKEPVP (ADR-093): the move editor's list window. Added and removed on
// demand (AddWindow/RemoveWindow) rather than living in sWindowTemplate:
// at 18x18 tiles its buffer is ~10KB of heap, which there is no reason to
// hold for the whole life of a menu that mostly is not the move editor.
// baseBlock 0x241 starts past MAIN_MENU_WINDOW_POKEPVP_4's own tiles
// (0x211 + 24*2), the same "fresh blocks past everything already in use"
// rule ADR-085 followed for the five menu slots. ADR-188's window 5
// deliberately reuses this SAME address (see that window's own comment)
// rather than taking a fresh one -- a wider shared range was tried and
// found (via a golden-slice regression) to push the move-info window
// and the frame-graphics base tile far enough out to corrupt rendering
// elsewhere in this VRAM-tile-budget-tight ROM.
//
// POKEPVP (menu redesign): tilemapTop 1 / height 18 assumed the screen
// had no header/footer competing for rows -- true before the backdrop
// redraw, false after (rows 0-5 are the header art, 16-19 the footer).
// Left at the old size this window drew across both bands, which is
// the "crooked Team Builder" bug reported after that redraw. Clamped to
// the same rows 6-15 the top-level menu itself uses, so it sits inside
// the one panel frame rather than spilling past it.
static const struct WindowTemplate sPokePvPListWindowTemplate = {
    .bg = 0, .tilemapLeft = 2, .tilemapTop = 6, .width = 18, .height = 10,
    .paletteNum = 15, .baseBlock = 0x241
};

// Rows visible at once: two tiles per row over a 10-tile-tall window.
#define POKEPVP_LIST_ROWS 5

// POKEPVP (ADR-096/097): the move info panel, in the ~80px (tilemapLeft
// 20..29) the list window's own 18-tile width leaves unused on the
// right. 10x9 (90 tiles, baseBlock 0x385..0x3DF) stays comfortably clear
// of the list window's own tile budget (ADR-096). Four 16px-tall lines
// is all this needs (ADR-097). Same on-demand Add/RemoveWindow lifetime
// as the list window, opened only for the move-slot and movepool lists
// (not the species list, which has no move stats to show).
//
// POKEPVP (menu redesign): tilemapTop/height clamped to rows 6-15 for
// the same reason as sPokePvPListWindowTemplate above.
static const struct WindowTemplate sPokePvPMoveInfoWindowTemplate = {
    .bg = 0, .tilemapLeft = 20, .tilemapTop = 6, .width = 10, .height = 10,
    .paletteNum = 15, .baseBlock = 0x385
};

// POKEPVP (ADR-093): the move editor's working memory, on the heap rather
// than in EWRAM_DATA -- EWRAM is at 99.2% and this is needed only while
// the editor is actually open. Labels are pointers into gSpeciesNames /
// gMoveNames, so nothing is copied; only the item array and the move-id
// list are really stored.
struct PokePvPListData
{
    struct ListMenuItem items[POKEPVP_MAX_LEGAL_MOVES + 1];
    u16 moves[POKEPVP_MAX_LEGAL_MOVES];
};

static EWRAM_DATA struct PokePvPListData *sPokePvPList = NULL;

static const u16 sBg_Pal[] = INCBIN_U16("graphics/main_menu/bg.gbapal");
static const u16 sTextbox_Pal[] = INCBIN_U16("graphics/main_menu/textbox.gbapal");

// POKEPVP (ADR-186, de-skin): the base VRAM tile this menu's window
// frame loads at. Comfortably past every baseBlock this file uses
// (highest is the move info window's 0x385 + up to 100 tiles) -- kept as
// a plain offset constant even though the frame graphics themselves are
// now FireRed's own stock GetUserWindowGraphics() output (see
// LoadUserFrameToBg/SetStdFrame0OnBg below), not a hand-drawn asset.
// ADR-188: deliberately NOT pushed out further for window 5 (which
// reuses 0x241, not a fresh address) -- see that window's own comment.
#define POKEPVP_PANEL_FRAME_BASE_TILE 0x400

static const u8 sTextColor1[] = { 10, 11, 12 };
// POKEPVP (menu redesign): the selected row's red-bar/white-text style,
// bank 15 indices 13-15 (previously unused padding) -- see textbox.pal.
static const u8 sTextColorSelected[] = { 13, 14, 15 };

static const u8 sTextColor2[] = { 10,  1, 12 };

// POKEPVP (ADR-186, de-skin): the per-move-type accent palette (8 hand-
// invented hues, dynamically overwriting two "reclaimed black" indices
// via LoadPokePvPExtraTypeColors) is removed -- move-info text now uses
// the same plain sTextColor1 every other value on this panel already
// uses. Minimalistic means one text color, not a bespoke type-color
// system vanilla FireRed never had here either.

static const struct BgTemplate sBgTemplate[] = {
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 30,
        .priority = 0
    }
    // POKEPVP (ADR-186, de-skin): the BattleDex arena BG2 backdrop is
    // removed -- it ate screen rows 0-5/16-19 on every menu screen for
    // decorative art with no functional value, and per the owner's
    // request a minimalistic UI should not have anything competing with
    // the actual menu content for the screen. BG0 (the real window/list
    // layer) is the only background this menu uses now.
};

static const u8 sMenuCursorYMax[] = { 0, 1, 2, 4 }; // POKEPVP (ADR-189): back to 5 items, cursor 0-4

static void CB2_MainMenu(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB_MainMenu(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_InitMainMenu(void)
{
    MainMenuGpuInit(1);
}

static void CB2_InitMainMenu_2(void)
{
    MainMenuGpuInit(1);
}

// POKEPVP (ADR-159): in vanilla the player name is always written by the
// New Game naming flow. PokePvP boots straight into this menu (ADR-085) and
// never runs it, so a fresh save's `playerName` can be raw 0xFF garbage with
// no EOS -- and unbounded StringCopy consumers (e.g. the summary screen's
// OT-name compare) then walk ~20KB through the whole save block, smashing
// the heap into the black screen. Clamp the field to a valid, terminated
// name at every menu entry so no consumer ever sees garbage.
static void PokePvP_SanitizePlayerName(void)
{
    u8 i;

    for (i = 0; i < PLAYER_NAME_LENGTH; i++)
    {
        if (gSaveBlock2Ptr->playerName[i] == EOS)
            return;
        if (gSaveBlock2Ptr->playerName[i] > 0x3F) // not a FireRed charmap name char (0xFF = empty save)
        {
            gSaveBlock2Ptr->playerName[i] = EOS;
            return;
        }
    }

    gSaveBlock2Ptr->playerName[PLAYER_NAME_LENGTH] = EOS;
}

static bool32 MainMenuGpuInit(u8 a0)
{
    u8 taskId;

    PokePvP_SanitizePlayerName(); /* POKEPVP (ADR-159) */
    gPokePvPInboxSnoozed = FALSE; /* POKEPVP (UI plan slice 4): a fresh menu init re-arms the inbox popup for an unanswered challenge */
    SetVBlankCallback(NULL);
    SetGpuReg(REG_OFFSET_DISPCNT, 0);
    SetGpuReg(REG_OFFSET_BG2CNT, 0);
    SetGpuReg(REG_OFFSET_BG1CNT, 0);
    SetGpuReg(REG_OFFSET_BG0CNT, 0);
    SetGpuReg(REG_OFFSET_BG2HOFS, 0);
    SetGpuReg(REG_OFFSET_BG2VOFS, 0);
    SetGpuReg(REG_OFFSET_BG1HOFS, 0);
    SetGpuReg(REG_OFFSET_BG1VOFS, 0);
    SetGpuReg(REG_OFFSET_BG0HOFS, 0);
    SetGpuReg(REG_OFFSET_BG0VOFS, 0);
    DmaFill16(3, 0, (void *)VRAM, VRAM_SIZE);
    DmaFill32(3, 0, (void *)OAM, OAM_SIZE);
    DmaFill16(3, 0, (void *)(PLTT + 2), PLTT_SIZE - 2);
    ScanlineEffect_Stop();
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    ResetPaletteFade();
    ResetBgsAndClearDma3BusyFlags(FALSE);
    InitBgsFromTemplates(0, sBgTemplate, NELEMS(sBgTemplate));
    ChangeBgX(0, 0, 0);
    ChangeBgY(0, 0, 0);
    ChangeBgX(1, 0, 0);
    ChangeBgY(1, 0, 0);
    ChangeBgX(2, 0, 0);
    ChangeBgY(2, 0, 0);
    InitWindows(sWindowTemplate);
    DeactivateAllTextPrinters();
    LoadPalette(sBg_Pal, BG_PLTT_ID(0), sizeof(sBg_Pal));
    LoadPalette(sTextbox_Pal, BG_PLTT_ID(15), sizeof(sTextbox_Pal));
    SetGpuReg(REG_OFFSET_WIN0H, 0);
    SetGpuReg(REG_OFFSET_WIN0V, 0);
    SetGpuReg(REG_OFFSET_WININ, 0);
    SetGpuReg(REG_OFFSET_WINOUT, 0);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_BLDALPHA, 0);
    SetGpuReg(REG_OFFSET_BLDY, 0);
    SetMainCallback2(CB2_MainMenu);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON | DISPCNT_WIN0_ON);
    taskId = CreateTask(Task_SetWin0BldRegsAndCheckSaveFile, 0);
    gTasks[taskId].tCursorPos = 0;
    gTasks[taskId].tSubMode = a0;
    return FALSE;
}

/*
 * The entire screen is darkened slightly except at WIN0 to indicate
 * the player cursor position.
 */

static void Task_SetWin0BldRegsAndCheckSaveFile(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetGpuReg(REG_OFFSET_WIN0H, 0);
        SetGpuReg(REG_OFFSET_WIN0V, 0);
        SetGpuReg(REG_OFFSET_WININ, 0x0001);
        SetGpuReg(REG_OFFSET_WINOUT, 0x0021);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(0, 0));
        SetGpuReg(REG_OFFSET_BLDY, 0); // POKEPVP: selection is a real color swap now, not a darken
        // POKEPVP (ADR-085, D7): this product has no Continue/Mystery Gift
        // concept -- every save-status branch now lands on the real 5-item
        // menu (MAIN_MENU_POKEPVP) instead of FireRed's NEWGAME/CONTINUE/
        // MYSTERYGIFT split. Genuine save-hardware error text (deleted/
        // corrupted save, no flash chip) is still shown first where it was
        // before -- only what happens *after* acknowledging it changes.
        switch (gSaveFileStatus)
        {
        case SAVE_STATUS_OK:
            LoadUserFrameToBg(0);
            gTasks[taskId].tMenuType = MAIN_MENU_POKEPVP;
            gTasks[taskId].func = Task_SetWin0BldRegsNoSaveFileCheck;
            break;
        case SAVE_STATUS_INVALID:
            SetStdFrame0OnBg(0);
            gTasks[taskId].tMenuType = MAIN_MENU_POKEPVP;
            PrintSaveErrorStatus(taskId, gText_SaveFileHasBeenDeleted);
            break;
        case SAVE_STATUS_ERROR:
            SetStdFrame0OnBg(0);
            gTasks[taskId].tMenuType = MAIN_MENU_POKEPVP;
            PrintSaveErrorStatus(taskId, gText_SaveFileCorrupted);
            break;
        case SAVE_STATUS_EMPTY:
        default:
            LoadUserFrameToBg(0);
            gTasks[taskId].tMenuType = MAIN_MENU_POKEPVP;
            gTasks[taskId].func = Task_SetWin0BldRegsNoSaveFileCheck;
            break;
        case SAVE_STATUS_NO_FLASH:
            SetStdFrame0OnBg(0);
            gTasks[taskId].tMenuType = MAIN_MENU_POKEPVP;
            PrintSaveErrorStatus(taskId, gText_1MSubCircuitBoardNotInstalled);
            break;
        }
    }
}

static void PrintSaveErrorStatus(u8 taskId, const u8 *str)
{
    PrintMessageOnWindow4(str);
    gTasks[taskId].func = Task_SaveErrorStatus_RunPrinterThenWaitButton;
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    ShowBg(0);
    SetVBlankCallback(VBlankCB_MainMenu);
}

static void Task_SaveErrorStatus_RunPrinterThenWaitButton(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR) && JOY_NEW(A_BUTTON))
        {
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            LoadUserFrameToBg(0);
            if (gTasks[taskId].tMenuType == MAIN_MENU_NEWGAME)
                gTasks[taskId].func = Task_SetWin0BldRegsNoSaveFileCheck;
            else
                gTasks[taskId].func = Task_PrintMainMenuText;
        }
    }
}

static void Task_SetWin0BldRegsNoSaveFileCheck(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetGpuReg(REG_OFFSET_WIN0H, 0);
        SetGpuReg(REG_OFFSET_WIN0V, 0);
        SetGpuReg(REG_OFFSET_WININ, 0x0001);
        SetGpuReg(REG_OFFSET_WINOUT, 0x0021);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(0, 0));
        SetGpuReg(REG_OFFSET_BLDY, 0); // POKEPVP: selection is a real color swap now, not a darken
        if (gTasks[taskId].tMenuType == MAIN_MENU_NEWGAME)
            gTasks[taskId].func = Task_ExecuteMainMenuSelection;
        else
            gTasks[taskId].func = Task_WaitFadeAndPrintMainMenuText;
    }
}

// POKEPVP (ADR-196): bounded grace window for a real, in-flight POST_MATCH
// write after a real battle ends. Sized well above the launcher's own
// worst-case matchEnd -> fetch_last_match (up to a documented 300ms retry
// sleep, ADR-195) -> POST_MATCH mailbox-write pipeline, with real margin
// for gateway/mailbox round-trip jitter, while staying far below
// POKEPVP_AUTO_MATCH_WAIT_FRAMES/POKEPVP_POST_MATCH_RESULT_WAIT_FRAMES
// (30s/10s) below -- this is a one-time post-battle pause, not a
// user-cancellable wait state, so it must stay short. 120 frames = 2s at
// 60fps.
#define POKEPVP_POST_MATCH_ARRIVAL_WAIT_FRAMES 120

static void Task_WaitFadeAndPrintMainMenuText(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        /* POKEPVP (UI plan slice 2): a pending post-match session (a
         * match just ended and CB2_EndPokePvPBattle re-init'ed this
         * menu) routes to the post-match screen instead of the top-level
         * menu. Checked here, not in CB2_InitMainMenu, because the whole
         * save-status chain above must run unchanged first (a corrupted
         * save still reports before anything else); this task is the
         * first point every valid-save path converges on. The screen
         * draws and fades in itself; the top menu stays untouched below
         * it. */
        if (PokePvPPostMatch_IsPending())
        {
            gTasks[taskId].func = Task_PokePvPPostMatch;
            return;
        }

        /* POKEPVP (ADR-196): this check above is one-shot per
         * CB2_InitMainMenu entry -- ADR-195 root-caused, live, that a real
         * match's own POST_MATCH write (matchEnd -> fetch_last_match ->
         * mailbox write on the launcher side) can still be genuinely
         * in-flight the very first time this task reaches this point,
         * because the ROM only needs the player's own single dismissal of
         * FireRed's vanilla trainer-defeat message to get here. Rather
         * than committing to the ordinary top menu immediately, give a
         * real battle end (and ONLY a real battle end -- every other path
         * into CB2_InitMainMenu, cold boot included, never sets this flag
         * and pays zero extra frames below) a short, bounded window to
         * let that write land, re-checking IsPending() every frame. Bound
         * enforced explicitly: this can never become a permanent stall
         * even if POST_MATCH never arrives at all (ADR-191/194's own
         * standing lesson against exactly that failure shape). */
        if (PokePvPPostMatch_ConsumeJustEndedBattle())
        {
            gTasks[taskId].tPostMatchWaitFrames = 1;
            DebugPrintf("POKEPVP: ADR-196 real battle end -- arming post-match arrival wait");
        }

        if (gTasks[taskId].tPostMatchWaitFrames != 0)
        {
            if (gTasks[taskId].tPostMatchWaitFrames >= POKEPVP_POST_MATCH_ARRIVAL_WAIT_FRAMES)
            {
                DebugPrintf("POKEPVP: ADR-196 post-match arrival wait expired (%d frames), falling to ordinary menu", POKEPVP_POST_MATCH_ARRIVAL_WAIT_FRAMES);
                gTasks[taskId].tPostMatchWaitFrames = 0;
                Task_PrintMainMenuText(taskId);
                return;
            }
            gTasks[taskId].tPostMatchWaitFrames++;
            return;
        }

        Task_PrintMainMenuText(taskId);
    }
}

static void Task_PrintMainMenuText(u8 taskId)
{
    u16 pal;
    SetGpuReg(REG_OFFSET_WIN0H, 0);
    SetGpuReg(REG_OFFSET_WIN0V, 0);
    SetGpuReg(REG_OFFSET_WININ, 0x0001);
    SetGpuReg(REG_OFFSET_WINOUT, 0x0021);
    SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN);
    SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(0, 0));
    SetGpuReg(REG_OFFSET_BLDY, 0); // POKEPVP: selection is a real color swap now, not a darken
    if (gSaveBlock2Ptr->playerGender == MALE)
        pal = RGB(4, 16, 31);
    else
        pal = RGB(31, 3, 21);
    LoadPalette(&pal, BG_PLTT_ID(15) + 1, PLTT_SIZEOF(1));
    switch (gTasks[taskId].tMenuType)
    {
    case MAIN_MENU_POKEPVP:
    default:
        DrawPokePvPMenuItems(gTasks[taskId].tCursorPos);
        break;
    case MAIN_MENU_NEWGAME:
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_NEWGAME_ONLY, PIXEL_FILL(10));
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_NEWGAME_ONLY, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_StartMatch);
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_NEWGAME_ONLY]);
        PutWindowTilemap(MAIN_MENU_WINDOW_NEWGAME_ONLY);
        CopyWindowToVram(MAIN_MENU_WINDOW_NEWGAME_ONLY, COPYWIN_FULL);
        break;
    case MAIN_MENU_CONTINUE:
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_CONTINUE, PIXEL_FILL(10));
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_NEWGAME, PIXEL_FILL(10));
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 2, 2, sTextColor1, -1, gText_Continue);
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_NEWGAME, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_StartMatch);
        PrintContinueStats();
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_CONTINUE]);
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_NEWGAME]);
        PutWindowTilemap(MAIN_MENU_WINDOW_CONTINUE);
        PutWindowTilemap(MAIN_MENU_WINDOW_NEWGAME);
        CopyWindowToVram(MAIN_MENU_WINDOW_CONTINUE, COPYWIN_GFX);
        CopyWindowToVram(MAIN_MENU_WINDOW_NEWGAME, COPYWIN_FULL);
        break;
    case MAIN_MENU_MYSTERYGIFT:
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_CONTINUE, PIXEL_FILL(10));
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_NEWGAME, PIXEL_FILL(10));
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_MYSTERYGIFT, PIXEL_FILL(10));
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 2, 2, sTextColor1, -1, gText_Continue);
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_NEWGAME, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_StartMatch);
        gTasks[taskId].tMGErrorType = 1;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_MYSTERYGIFT, FONT_NORMAL, 2, 2, sTextColor1, -1, gText_MysteryGift);
        PrintContinueStats();
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_CONTINUE]);
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_NEWGAME]);
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_MYSTERYGIFT]);
        PutWindowTilemap(MAIN_MENU_WINDOW_CONTINUE);
        PutWindowTilemap(MAIN_MENU_WINDOW_NEWGAME);
        PutWindowTilemap(MAIN_MENU_WINDOW_MYSTERYGIFT);
        CopyWindowToVram(MAIN_MENU_WINDOW_CONTINUE, COPYWIN_GFX);
        CopyWindowToVram(MAIN_MENU_WINDOW_NEWGAME, COPYWIN_GFX);
        CopyWindowToVram(MAIN_MENU_WINDOW_MYSTERYGIFT, COPYWIN_FULL);
        break;
    }
    gTasks[taskId].func = Task_WaitDma3AndFadeIn;
}

// POKEPVP (menu redesign): draws all 5 real menu items, styled to match
// the owner's BattleDex creative -- the selected row is a solid red bar
// with white text (sTextColorSelected/PIXEL_FILL(13)), everything else
// is plain cream with dark-ink text (sTextColor1/PIXEL_FILL(10)), and
// ONE frame is drawn around the whole 5-row block (sPokePvPMenuPanelTemplate)
// instead of a separate border per row -- five individually-boxed rows
// is what read as FireRed's stock list "slapped on top of" the backdrop
// rather than one embedded panel. Shared by Task_PrintMainMenuText (first
// draw, with the fade-in) and every redraw-on-return/redraw-on-cursor-move
// path below.
static void DrawPokePvPMenuItems(u8 selectedIdx)
{
    // POKEPVP (ADR-189): back to 5 real rows. ADR-188 added LEADERBOARD as
    // a real 6th row (window 5), but that contradicts a binding product
    // decision (PokePvP_Launch_Features_Build_Plan.md §2.1: "Ranked
    // Leaderboard is not a launch destination while ranked play is
    // disabled. It may be added inside Profile or promoted later when
    // ranked play becomes active") and repeats a mistake this project
    // already made and reverted once before, in the Gen I era (ADR-053:
    // a global ranked leaderboard was always empty under unrated pairing,
    // replaced with a per-opponent head-to-head record instead). Ranked
    // is still disabled here (launch-config's rankedEnabled defaults to
    // false, ADR-174) and every match is still Quick/Custom/Practice/
    // Invite (all unranked), so GET /v1/leaderboard (which defaults to
    // queueId "rby-competitive-ranked") would render permanently empty
    // in production -- exactly the dead-feature shape ADR-053 already
    // named and fixed once. See docs/adr/189 for the full resolution.
    // Window 5 / sPokePvPMenuPanelTemplateWide stay in place (other
    // screens' own window-5-blanking calls still reference them, and
    // reverting the shared VRAM-tile layout ADR-188 tuned is its own
    // separate risk not needed to fix this) -- window 5 is simply never
    // populated with real content from this screen anymore.
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    const u8 *const sLabels[] = {
        sText_StartMatch, sText_TeamBuilder, sText_Profile,
        sText_MatchHistory, sText_Options,
    };
    u8 i;

    for (i = 0; i < 5; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
            selected ? sTextColorSelected : sTextColor1, -1, sLabels[i]);
    }
    // alpha-build-plan P6 (docs/adr/263): the top menu's own identity tag,
    // in window 5 rather than a new window -- ADR-262 (this same session)
    // tried a new top-of-screen window instead and found it corrupted the
    // MATCH HISTORY screen: window 5's baseBlock is already safely shared
    // (aliased with the move-editor list, which is never live at the same
    // time), and every OTHER screen that reuses this shared 5-row layout
    // already defensively re-blanks window 5 itself (the exact "erase,
    // don't just occlude" comment this replaced) -- so reusing it needs
    // zero new tile storage and zero new call sites elsewhere to avoid
    // bleed-through; those dozen existing blank-window-5 calls already do
    // that job. sTextColor2 (the vanilla CONTINUE screen's own label
    // color, untouched by ADR-186) instead of sTextColor1 so this reads
    // as a dim footer tag, not a 6th selectable row -- selectedIdx never
    // reaches 5 (the loop above only ever runs i<5), so it can never be
    // highlighted, but a same-looking unselectable row would still be
    // confusing to look at.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplateWide);
    for (i = 0; i < 5; i++)
        PutWindowTilemap(sWindowIds[i]);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    for (i = 0; i < 5; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void Task_WaitDma3AndFadeIn(u8 taskId)
{
    if (WaitDma3Request(-1) != -1)
    {
        /* POKEPVP (ADR-158, owner-reported live: "it's not possible to
         * select 'edit team' or 'edit moves'" -- the second PC-box entry
         * hard-reset the game): LoadPokePvPMenuBackdrop's four
         * DecompressAndCopyTileDataToVram calls (header/footer/blank/map)
         * allocate heap decompress buffers tracked in the shared
         * sTempTileDataBuffers cursor list, and MainMenuGpuInit runs
         * again on every return from the PC box (CB2_InitMainMenu). Every
         * other subsystem that uses that API (berry_crush, daycare,
         * diploma, ...) pairs each use with ResetTempTileDataBuffers +
         * FreeTempTileDataBuffersIfPossible; the backdrop load did not,
         * so every Team Builder round trip leaked the whole backdrop set
         * (~21 KB, measured via a temporary heap walk: +4 blocks / -21376
         * free per round trip) until the malloc free-list could no longer
         * serve EnterPokeStorage's gStorage Alloc -- malloc.c:174
         * assertion, illegal opcode, soft reset to the title screen.
         * Freed here, at the one point this task is guaranteed DMA-idle
         * (WaitDma3Request just resolved; FreeTempTileDataBuffersIfPossible
         * deliberately refuses while a bg copy is in flight) and every
         * menu entry -- boot and re-init alike -- passes through. */
        FreeTempTileDataBuffersIfPossible();
        ResetTempTileDataBuffers();
        gTasks[taskId].func = Task_UpdateVisualSelection;
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
        ShowBg(0);
            SetVBlankCallback(VBlankCB_MainMenu);
    }
}

static void Task_UpdateVisualSelection(u8 taskId)
{
    // POKEPVP (ADR-093): the one place every path into the live menu passes
    // through, so the "came back from the box screen" hand-off happens here
    // rather than in each of Task_PrintMainMenuText's branches.
    if (sPokePvPReturnToTeamList)
    {
        sPokePvPReturnToTeamList = FALSE;
        DrawTeamListItems(0);
        gTasks[taskId].tCursorPos = 1;
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPTeamList;
        return;
    }
    // POKEPVP (ADR-233): same shape as sPokePvPReturnToTeamList just
    // above -- skip the top menu (and PROFILE) entirely and land straight
    // back on FRIENDS, with any pending ADD FRIEND result shown
    // immediately instead of only on the next ordinary visit.
    // tCursorPos = 2 documents PROFILE as this screen's real top-menu
    // parent (see DrawPokePvPMenuItems's own row order); B from SOCIAL
    // always jumps straight to the top menu's row 0 by existing design
    // (Task_PokePvPReturnToTopMenuFromHistory), so this value is never
    // actually read for navigation, only for consistency with that
    // parent relationship.
    if (sPokePvPReturnToSocialFriends)
    {
        sPokePvPReturnToSocialFriends = FALSE;
        gTasks[taskId].tSocialList = 0;
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].tCursorPos = 2;
        DrawSocialListItems(0, 0, TRUE);
        ShowAddFriendResultIfPending(taskId);
        return;
    }
    // POKEPVP (ADR-238): same shape again -- lands straight back on the
    // PLAYER submenu (SPRITE/NAME), cursor on NAME (the row just used),
    // instead of the top menu, after the naming screen's own
    // CB2_InitMainMenu round trip.
    if (sPokePvPReturnToPlayerMenu)
    {
        sPokePvPReturnToPlayerMenu = FALSE;
        gTasks[taskId].tSubCursorPos = 1;
        gTasks[taskId].tCursorPos = 2;
        DrawPlayerMenuItems(1);
        gTasks[taskId].func = Task_PokePvPPlayerMenu;
        return;
    }
    MoveWindowByMenuTypeAndCursorPos(gTasks[taskId].tMenuType, gTasks[taskId].tCursorPos);
    // POKEPVP (menu redesign): the selected-row red-bar/white-text look
    // is a real color swap, not the old WIN0-darken trick (BLDY is 0 now
    // for this menu type -- see Task_SetWin0BldRegsAndCheckSaveFile) --
    // so it has to be redrawn every time the cursor moves, not just once.
    if (gTasks[taskId].tMenuType == MAIN_MENU_POKEPVP)
        DrawPokePvPMenuItems(gTasks[taskId].tCursorPos);
    gTasks[taskId].func = Task_HandleMenuInput;
}

static void Task_HandleMenuInput(u8 taskId)
{
    /* POKEPVP (UI plan slice 4): while the player idles on the top menu
     * (the only surface this task drives), a buffered challenge surfaces
     * the inbox -- unless the player already dismissed it
     * (gPokePvPInboxSnoozed, cleared on every fresh menu init). A
     * battle, team-builder, naming, options, or post-match flow never
     * reaches this task -- those own their own funcs -- so "don't
     * interrupt battles or destructive flows" (Build Plan2 item 6) is
     * structural, not a flag. */
    if (!gPaletteFade.active
     && gTasks[taskId].tMenuType == MAIN_MENU_POKEPVP
     && PokePvPInbox_Count() > 0
     && !gPokePvPInboxSnoozed)
    {
        gTasks[taskId].tInboxSlot = 0;
        gTasks[taskId].tSubCursorPos = 0;
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].tMGErrorMsgState = 0;
        gTasks[taskId].func = Task_PokePvPInbox;
        return;
    }
    if (!gPaletteFade.active && HandleMenuInput(taskId))
    {
        gTasks[taskId].func = Task_UpdateVisualSelection;
    }
}

static void Task_ExecuteMainMenuSelection(u8 taskId)
{
    s32 menuAction;
    if (!gPaletteFade.active)
    {
        switch (gTasks[taskId].tMenuType)
        {
        default:
        case MAIN_MENU_POKEPVP:
            // POKEPVP (ADR-085): only slot 0 (START MATCH) is real behavior;
            // slots 1-4 go to the stub handler instead of falling through
            // Task_ExecuteMainMenuSelection's menuAction dispatch below.
            if (gTasks[taskId].tCursorPos == 0)
            {
                // POKEPVP (ADR-091): START MATCH now opens the AUTO-MATCH/
                // INVITE MATCH submenu instead of calling StartPokePvPMenuMatch
                // directly. Same fade-out-already-happened situation as the
                // stub branch below (HandleMenuInput's A-press), so redraw
                // and fade back in here.
                DrawStartMatchSubmenuItems(0);
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
                gTasks[taskId].tSubCursorPos = 0;
                gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
            }
            else if (gTasks[taskId].tCursorPos == 1)
            {
                // POKEPVP (ADR-093): TEAM BUILDER. Same shape as the START
                // MATCH submenu above -- redraw into the existing windows
                // and fade back in, since HandleMenuInput's A-press has
                // already taken the screen to black.
                DrawTeamListItems(0);
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
                gTasks[taskId].tSubCursorPos = 0;
                gTasks[taskId].func = Task_PokePvPTeamList;
            }
            else if (gTasks[taskId].tCursorPos == 2)
            {
                // POKEPVP (ADR-238, owner-directed restructure): PROFILE
                // is now a plain 2-row action selector (SOCIAL/PLAYER),
                // not a stats display (see Task_PokePvPProfile's own doc
                // comment) -- drawn eagerly here, same "redraw into the
                // existing windows and fade back in" shape as the START
                // MATCH/Team Builder branches above, not the lazy
                // tScreenDrawn-gated draw this screen used to need for its
                // old stats content.
                gTasks[taskId].tSubCursorPos = 0;
                DrawProfileItems(0);
                gTasks[taskId].func = Task_PokePvPProfile;
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            }
            else if (gTasks[taskId].tCursorPos == 4)
            {
                // POKEPVP (ADR-112): OPTIONS reuses FireRed's own real
                // options screen verbatim (CB2_OptionsMenuFromStartMenu,
                // option_menu.c) -- same "reuse FireRed's own real code"
                // bet as Team Builder reusing the PC (ADR-093). Every row
                // on that screen (TEXT SPEED, BATTLE SCENE, BATTLE STYLE,
                // SOUND) is already fully real and reads/writes
                // gSaveBlock2Ptr the same way a vanilla FireRed save would
                // -- nothing here is a mockup. gMain.savedCallback is only
                // defaulted by that function when NULL, so setting it here
                // to CB2_InitMainMenu (not CB2_ReturnToFieldWithOpenMenu,
                // which assumes a normal overworld return) makes EXIT
                // return to this PokePvP menu instead of a nonexistent
                // field state, the same pattern PokePvPTeamBuilder_Open
                // already uses for its own return target.
                gExitStairsMovementDisabled = FALSE;
                gMain.savedCallback = CB2_InitMainMenu;
                FreeAllWindowBuffers();
                DestroyTask(taskId);
                SetMainCallback2(CB2_OptionsMenuFromStartMenu);
            }
            else if (gTasks[taskId].tCursorPos == 3)
            {
                /* POKEPVP (ADR-168): MATCH HISTORY -- real screen now
                 * (was LEADERBOARD/stub). The task draws whatever the
                 * launcher already buffered in history.c; fade back in
                 * like the START MATCH/Team Builder branches above (the
                 * A-press already blacked the screen). */
                // POKEPVP (ADR-189): reset the draw-once latch explicitly --
                // see tScreenDrawn's own comment for why this can't be
                // trusted to already be 0.
                gTasks[taskId].tScreenDrawn = 0;
                gTasks[taskId].func = Task_PokePvPMatchHistory;
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            }
            else
            {
                // ADR-086 fix: HandleMenuInput's A-press already fades the
                // whole screen to black (0,0,16,RGB_BLACK) before this task
                // runs. MAIN_MENU_MYSTERYGIFT's own error path (below) fades
                // back in here so its message is actually visible while the
                // player reads it -- Task_PokePvPMenuStub never had this
                // call, so "Not yet implemented." was drawn into a fully
                // black palette and was never visible until the final
                // dismiss-time fade (added in ADR-085) flashed the menu back.
                // Unreachable now that cursor 0-4 are all handled above --
                // sMenuCursorYMax caps the cursor at 4 (ADR-189: LEADERBOARD
                // removed as a top-level row) -- kept only as a defensive
                // default.
                gTasks[taskId].tMGErrorMsgState = 0;
                gTasks[taskId].func = Task_PokePvPMenuStub;
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            }
            return;
        case MAIN_MENU_NEWGAME:
            menuAction = MAIN_MENU_NEWGAME;
            break;
        case MAIN_MENU_CONTINUE:
            switch (gTasks[taskId].tCursorPos)
            {
            default:
            case 0:
                menuAction = MAIN_MENU_CONTINUE;
                break;
            case 1:
                menuAction = MAIN_MENU_NEWGAME;
                break;
            }
            break;
        case MAIN_MENU_MYSTERYGIFT:
            switch (gTasks[taskId].tCursorPos)
            {
            default:
            case 0:
                menuAction = MAIN_MENU_CONTINUE;
                break;
            case 1:
                menuAction = MAIN_MENU_NEWGAME;
                break;
            case 2:
                if (!IsWirelessAdapterConnected())
                {
                    SetStdFrame0OnBg(0);
                    gTasks[taskId].func = Task_MysteryGiftError;
                    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
                    return;
                }
                else
                {
                    menuAction = MAIN_MENU_MYSTERYGIFT;
                }
                break;
            }
            break;
        }
        switch (menuAction)
        {
        default:
        case MAIN_MENU_NEWGAME:
            // POKEPVP (ADR-079/080, D7 step 1): was StartNewGameScene()
            // -- this slot is now "START MATCH" (sText_StartMatch
            // above), skipping Oak's intro entirely in favor of
            // StartPokePvPMenuMatch's direct warp into a real overworld
            // state. See that function's own doc comment
            // (battle_setup.c) for why this is safe at this exact point.
            gExitStairsMovementDisabled = FALSE;
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            StartPokePvPMenuMatch();
            break;
        case MAIN_MENU_CONTINUE:
            gPlttBufferUnfaded[0] = RGB_BLACK;
            gPlttBufferFaded[0] = RGB_BLACK;
            gExitStairsMovementDisabled = FALSE;
            FreeAllWindowBuffers();
            TryStartQuestLogPlayback(taskId);
            break;
        case MAIN_MENU_MYSTERYGIFT:
            SetMainCallback2(CB2_InitMysteryGift);
            HelpSystem_Disable();
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            break;
        }
    }
}

static void Task_MysteryGiftError(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        FillBgTilemapBufferRect_Palette0(0, 0, 0, 0, 30, 20);
        if (gTasks[taskId].tMGErrorType == 1)
            PrintMessageOnWindow4(gText_WirelessNotConnected);
        else
            PrintMessageOnWindow4(gText_MysteryGiftCantUse);
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (!gPaletteFade.active)
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 3:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            gTasks[taskId].func = Task_ReturnToTileScreen;
        }
        break;
    }
}

// POKEPVP (ADR-085): Team Builder/Player Settings/Leaderboard/Options stub.
// Same message-box-then-wait-for-button shape as Task_MysteryGiftError
// (a proven FireRed pattern), but returns into the still-live menu instead
// of the title screen -- redraws all 5 items via DrawPokePvPMenuItems since
// the error window's tiles (top15, height4) physically overlap the bottom
// two menu slots (top13/top17) and erasing it blanks those tiles.
static void Task_PokePvPMenuStub(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        PrintMessageOnWindow4(sText_NotYetImplemented);
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (!gPaletteFade.active)
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 3:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            // The menu-selection fade-out (HandleMenuInput's A-press,
            // BeginNormalPaletteFade(...,0,16,...)) already took the
            // screen to black before this task ever ran -- without fading
            // back to normal here, the screen stays black forever after
            // dismissal. Mirrors Task_WaitDma3AndFadeIn's own fade-in call.
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            DrawPokePvPMenuItems(gTasks[taskId].tCursorPos);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_UpdateVisualSelection;
        }
        break;
    }
}

// POKEPVP (ADR-238, owner-directed restructure, 2026-09-19): MATCH
// HISTORY now also shows the tag/aggregate-stats/top-species/recent-
// opponent content PROFILE used to (ADR-188) -- the owner's own read was
// that a player's record belongs with the rest of their match history,
// not mixed into a plain action-selector menu. Windows 0-3 take the same
// four rows PROFILE's own layout used (tag, M/W/L/T, top species, most
// recent opponent); window 4 is a plain divider label; the ERROR band
// (4 tile rows, twice window 0's own old height) now holds the per-
// opponent win/loss list this screen already showed, relocated here from
// window 0 alone -- strictly more room than before for that list, not
// less, though the list's own worst case (8 entries) still would not
// fully fit even in 4 rows; this was already true of the original
// window-0-only layout and is not a new regression, just not fully
// solved by this pass either.
static void Task_PokePvPMatchHistory(u8 taskId)
{
    PokePvPProfile profile;
    bool8 haveProfile;
    u8 recentCount;
    u8 historyCount = PokePvPMatchHistory_Count();
    // Sized for the worst case of the ERROR band's own combined buffer.
    // Found live during P6's max-length-name gate pass: this previously
    // read "NAME" (16) here, but history.c's real wire cap is
    // POKEPVP_HISTORY_MAX_NAME_LEN (24) -- history.c's own receive path
    // never clamps to 16 the way post_match.c/profile.c/social.c/inbox.c
    // do, and 24 matches the real max username length (apps/api's
    // `displayName.length > 24` check, auth.ts) -- so a genuine
    // real-world 17-24 char opponent name was never actually impossible,
    // just never tried against this buffer. Real worst case: "RECORD:"
    // (7) + newline (1) + POKEPVP_HISTORY_MAX_ENTRIES (8) list lines
    // (24 + "   W: " (6) + 2 digits (wins clamped <=99 launcher-side) +
    // "  L: " (5) + 2 digits (losses, same clamp) + newline = 40 each)
    // = 8 + 320 = 328, + EOS = 329 -- reused per-window below, so sized
    // for its own single largest user, not the sum of all of them
    // (ADR-198's own lesson, which this buffer had silently drifted
    // out of sync with). See ADR-198 for the original overflow class.
    u8 buf[336];
    u8 *dst;
    u8 i;

    if (gPaletteFade.active)
        return;

    if (gTasks[taskId].tScreenDrawn == 0)
    {
        gTasks[taskId].tScreenDrawn = 1;
        haveProfile = PokePvPProfile_Get(&profile);
        recentCount = PokePvPProfile_RecentCount();

        FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
        PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
        CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);

        // Window 0: the permanent NAME#1234 tag.
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_0, PIXEL_FILL(10));
        dst = buf;
        if (haveProfile && profile.tag[0] != EOS)
            dst = StringCopy(dst, profile.tag);
        else
            dst = StringCopy(dst, sText_ProfileNoTag);
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_0, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
        PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_0);
        CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_0, COPYWIN_FULL);

        // Window 1: M/W/L/T on one line (qualifying matches only, per
        // ADR-175's counts_for_stats).
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_1, PIXEL_FILL(10));
        dst = buf;
        if (haveProfile)
        {
            dst = StringCopy(dst, sText_ProfileM);
            dst = ConvertIntToDecimalStringN(dst, profile.matches, STR_CONV_MODE_LEFT_ALIGN, 4);
            dst = StringCopy(dst, sText_ProfileStatSep);
            dst = StringCopy(dst, sText_ProfileW);
            dst = ConvertIntToDecimalStringN(dst, profile.wins, STR_CONV_MODE_LEFT_ALIGN, 4);
            dst = StringCopy(dst, sText_ProfileStatSep);
            dst = StringCopy(dst, sText_ProfileL);
            dst = ConvertIntToDecimalStringN(dst, profile.losses, STR_CONV_MODE_LEFT_ALIGN, 4);
            dst = StringCopy(dst, sText_ProfileStatSep);
            dst = StringCopy(dst, sText_ProfileT);
            dst = ConvertIntToDecimalStringN(dst, profile.ties, STR_CONV_MODE_LEFT_ALIGN, 4);
        }
        else
        {
            dst = StringCopy(dst, sText_ProfileEmpty);
        }
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_1, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
        PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_1);
        CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_1, COPYWIN_FULL);

        // Window 2: up to 3 most-used species (ADR-175's mostUsedSpecies).
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_2, PIXEL_FILL(10));
        dst = buf;
        dst = StringCopy(dst, sText_ProfileTop);
        if (haveProfile && profile.topCount > 0)
        {
            for (i = 0; i < profile.topCount && i < POKEPVP_PROFILE_MAX_TOP_SPECIES; i++)
            {
                if (i > 0)
                    dst = StringCopy(dst, sText_ProfileSpeciesSep);
                dst = StringCopy(dst, gSpeciesNames[profile.topSpecies[i]]);
            }
        }
        else
        {
            dst = StringCopy(dst, sText_ProfileNoSpecies);
        }
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_2, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
        PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_2);
        CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_FULL);

        // Window 3: the most recent opponent.
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_3, PIXEL_FILL(10));
        dst = buf;
        dst = StringCopy(dst, sText_ProfileRecent);
        if (recentCount > 0)
        {
            PokePvPRecentOpponent opp;
            if (PokePvPProfile_GetRecent(0, &opp))
            {
                dst = StringCopy(dst, opp.name);
                dst = StringCopy(dst, sText_PostMatchVs);
                dst = StringCopy(dst, opp.tag);
            }
        }
        else
        {
            dst = StringCopy(dst, sText_ProfileNoOpponent);
        }
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_3, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
        PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_3);
        CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_FULL);

        // Window 4: left blank ("erase, don't occlude" -- same discipline
        // every sibling screen uses for an unused row). A first attempt
        // here put a plain "RECORD:" divider label in this window, but
        // window 4 (tilemapTop=14, height=2, rows 14-15) and the ERROR
        // band drawn just below (tilemapTop=15, height=4, rows 15-18)
        // share tilemap row 15 -- the exact same class of overlap ADR-192
        // already found and fixed once for a different pair of windows.
        // Confirmed live via a headless capture: the ERROR band's own
        // later PutWindowTilemap/CopyWindowToVram call for row 15 silently
        // erased the label. Fixed by not drawing anything into window 4
        // at all and prepending the divider text to the ERROR band's own
        // buffer instead (below), which owns that shared row outright.
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_4, PIXEL_FILL(10));
        PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_4);
        CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_FULL);

        // ERROR band: a "RECORD:" divider line followed by the per-
        // opponent win/loss list (unchanged content and format from
        // before this screen absorbed PROFILE's own rows -- just
        // relocated from window 0 alone into this taller, 4-tile band).
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
        MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
        dst = buf;
        dst = StringCopy(dst, sText_MatchHistoryDivider);
        dst = StringCopy(dst, sString_Newline);
        if (historyCount == 0)
        {
            dst = StringCopy(dst, sText_HistoryEmpty);
        }
        else
        {
            for (i = 0; i < historyCount && i < POKEPVP_HISTORY_MAX_ENTRIES; i++)
            {
                PokePvPHistoryEntry entry;

                if (PokePvPMatchHistory_Get(i, &entry))
                {
                    dst = StringCopy(dst, entry.name);
                    dst = StringCopy(dst, sText_HistoryWSep);
                    dst = ConvertIntToDecimalStringN(dst, entry.wins, STR_CONV_MODE_LEFT_ALIGN, 2);
                    dst = StringCopy(dst, sText_HistoryL);
                    dst = ConvertIntToDecimalStringN(dst, entry.losses, STR_CONV_MODE_LEFT_ALIGN, 2);
                    dst = StringCopy(dst, sString_Newline);
                }
            }
        }
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 0, 2, sTextColor1, -1, buf);
        PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
        CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_FULL);
    }

    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].func = Task_PokePvPReturnToTopMenuFromHistory;
    }
}

/* POKEPVP (ADR-168): fade-out already ran (B above); redraw the top menu
 * and hand back to selection, same as Task_PokePvPReturnToTopMenuFromSubmenu. */
static void Task_PokePvPReturnToTopMenuFromHistory(u8 taskId)
{
    if (gPaletteFade.active)
        return;
    DrawPokePvPMenuItems(0);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tCursorPos = 0;
    gTasks[taskId].func = Task_UpdateVisualSelection;
}

// POKEPVP (ADR-189): Task_PokePvPLeaderboard / Task_PokePvPReturnToTop-
// MenuFromLeaderboard (ADR-188) removed along with the top menu's 6th
// row -- see DrawPokePvPMenuItems's own comment for why. The ROM-side
// buffer (rom/pvp-gen3/leaderboard.c/h), POKEPVP_MSG_LEADERBOARD_ENTRY,
// and the launcher's fetch_leaderboard() are deliberately left in place
// (small, additive, harmless) rather than unwound here too -- see
// docs/adr/189.

// POKEPVP (ADR-091): draws the 2-item AUTO-MATCH/INVITE MATCH submenu into
// the same 5 window slots DrawPokePvPMenuItems uses -- rows 0/1 get real
// labels, rows 2-4 are left blank (still drawn/tilemapped so any leftover
// top-level text is actually erased, not just occluded). Exact same
// draw/tilemap/vram-copy shape as DrawPokePvPMenuItems, just fewer labels
// -- same "reuse, don't duplicate" discipline as that function itself.
// POKEPVP (menu redesign): same red-bar/white-text selection style and
// single-panel-border as DrawPokePvPMenuItems -- this screen had no
// visible cursor at all once BLDY went to 0 (the old WIN0-darken trick
// this submenu still called MoveWindowByMenuTypeAndCursorPos for was the
// ONLY thing indicating a selection here, and neutering it without an
// alternative silently broke picking AUTO-MATCH vs INVITE MATCH).
static void DrawStartMatchSubmenuItems(u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    /* The visible rows, in the same order StartMatchModeForRow maps them
     * (QUICK EARLY, QUICK ELITE, CUSTOM ELITE, INVITE MATCH, PRACTICE),
     * compressed upward as flags hide modes -- a hidden mode never
     * occupies a row, so no phantom unselectable slot can render. The
     * flags==0 -> 0x0F rule keeps an offline build (no LAUNCH_CONFIG
     * ever pushed) at the legacy dense five. */
    static const u8 *const sAllLabels[] = {
        sText_QuickEarly, sText_QuickElite, sText_CustomElite, sText_InviteMatch, sText_PracticeMatch,
    };
    u8 flags = PokePvPPacks_Flags();
    const u8 *visible[5];
    u8 n = 0;
    u8 i;

    if (flags == 0)
        flags = 0x0F;
    if (flags & POKEPVP_FLAG_QUICK)
    {
        visible[n++] = sAllLabels[0];
        visible[n++] = sAllLabels[1];
    }
    if (flags & POKEPVP_FLAG_CUSTOM)
        visible[n++] = sAllLabels[2];
    if (flags & POKEPVP_FLAG_INVITE)
        visible[n++] = sAllLabels[3];
    if (flags & POKEPVP_FLAG_PRACTICE)
        visible[n++] = sAllLabels[4];

    for (i = 0; i < 5; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (i < n)
        {
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, visible[i]);
        }
    }
    // POKEPVP (ADR-188): this screen only ever populates 5 rows, but the
    // shared panel border now spans 6 (window 5's rows, freed by
    // ADR-186) -- blank it too, same "erase, don't just occlude"
    // discipline as the unused rows above, so LEADERBOARD text left over
    // from the top menu never bleeds through.
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 5; i++)
        PutWindowTilemap(sWindowIds[i]);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_GFX);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);

    /* Owner ask (2026-09-13, item 6): show the hovered row's own mode
     * description in the ERROR band, same window the pack picker's own
     * rules line already uses. Only drawn when the row is real
     * (selectedIdx < n) -- StartMatchModeForRow's own contract never maps
     * an out-of-range row, so this mirrors that bound. */
    {
        FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
        if (selectedIdx < n)
        {
            const u8 *desc;

            switch (StartMatchModeForRow(selectedIdx))
            {
            case POKEPVP_MATCH_MODE_QUICK_EARLY:
                desc = sText_ModeDescQuickEarly;
                break;
            case POKEPVP_MATCH_MODE_QUICK_ELITE:
                desc = sText_ModeDescQuickElite;
                break;
            case POKEPVP_MATCH_MODE_CUSTOM_ELITE:
                desc = sText_ModeDescCustomElite;
                break;
            case POKEPVP_MATCH_MODE_INVITE:
                desc = sText_ModeDescInvite;
                break;
            default:
                desc = sText_ModeDescPractice;
                break;
            }
            AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 1, 2, sTextColor1, -1, desc);
        }
        PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
        CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_GFX);
    }
}

/* POKEPVP (UI plan slice 3): the START MATCH mode tree (Build Plan §2.2
 * categories, §8 item 2). Five fixed rows -- QUICK EARLY / QUICK ELITE /
 * CUSTOM ELITE / INVITE MATCH / PRACTICE -- with rows hidden by the
 * server's feature flags (PacksCatalog_Flags, pushed via LAUNCH_CONFIG).
 * flags == 0 means "no configuration received at all" (offline build,
 * fetch failed): the ROM cannot tell that from an explicit all-disabled
 * push, and the real server always enables invite+practice, so 0 is
 * treated as the legacy dense tree -- every mode visible, exactly the
 * pre-slice-3 behavior. The row count is what the submenu task's D-pad
 * bounds clamp to. */
static u8 StartMatchRowCount(void)
{
    u8 flags = PokePvPPacks_Flags();
    u8 n = 0;

    if (flags == 0)
        flags = 0x0F; /* unconfigured -> all modes visible (legacy dense) */
    if (flags & POKEPVP_FLAG_QUICK)
        n += 2;
    if (flags & POKEPVP_FLAG_CUSTOM)
        n++;
    if (flags & POKEPVP_FLAG_INVITE)
        n++;
    if (flags & POKEPVP_FLAG_PRACTICE)
        n++;
    return n;
}

/* Row index -> submenu mode (POKEPVP_MATCH_MODE_*), walking the same
 * order and visibility as StartMatchRowCount. Returns POKEPVP_MATCH_MODE_
 * AUTO (0) for an out-of-range row -- the caller never passes one. */
static u8 StartMatchModeForRow(u8 row)
{
    u8 flags = PokePvPPacks_Flags();

    if (flags == 0)
        flags = 0x0F;
    if (flags & POKEPVP_FLAG_QUICK)
    {
        if (row == 0)
            return POKEPVP_MATCH_MODE_QUICK_EARLY;
        if (row == 1)
            return POKEPVP_MATCH_MODE_QUICK_ELITE;
        row -= 2;
    }
    if (flags & POKEPVP_FLAG_CUSTOM)
    {
        if (row == 0)
            return POKEPVP_MATCH_MODE_CUSTOM_ELITE;
        row--;
    }
    if (flags & POKEPVP_FLAG_INVITE)
    {
        if (row == 0)
            return POKEPVP_MATCH_MODE_INVITE;
        row--;
    }
    return POKEPVP_MATCH_MODE_PRACTICE;
}

/* POKEPVP (UI plan slice 3): the pack picker (Build Plan §8 items 3-4:
 * five Battle Packs + RANDOM + a rules/overview line). A 4-row scroll
 * window over a 6-row list (5 packs, then RANDOM) -- see ADR-192 for why
 * this is 4 rows, not 5: the row slots are shared with other screens'
 * geometry, and a 5th row here always collided with the overview line
 * below it. The overview line -- the hovered pack's title plus the static
 * format line -- draws in the ERROR band below the panel (the "pack
 * overview" of §8 item 3, lean: title + rules; the full description stays
 * a server-side summary the catalog could later carry). */
static void DrawPackPickerItems(u8 selectedIdx, u8 battleClass)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    u8 start;
    u8 i;
    u8 buf2[32];

    // POKEPVP (owner playtest round 4, ADR-192): this screen used to fill
    // all 5 shared POKEPVP_0..4 row slots (a 5-row window sliding over the
    // 6-row pack+RANDOM list), landing pack-list text in POKEPVP_4
    // (tilemapTop 14, rows 14-15). MAIN_MENU_WINDOW_ERROR -- where this
    // same function's own overview line below always draws -- starts at
    // tilemapTop 15, so its first line shares screen row 15 with
    // POKEPVP_4's own text every time a pack list is showing, and whichever
    // one draws last (the overview line, drawn after) visibly overwrites
    // the bottom row of the pack list (the owner's "3v3 LVL100 FROZEN is
    // obstructing the options text" report). Neither window's *template*
    // is touched here -- both are shared with other screens (submenu, team
    // selector, inbox, ...) that rely on their existing geometry -- instead
    // this screen alone now only ever fills 4 of its 5 row slots (POKEPVP_4
    // stays blank, "erase don't occlude" like every other unused slot in
    // this file), so nothing this screen draws ever reaches row 15 at all.
    start = 0;
    if (selectedIdx > 3)
        start = (u8)(selectedIdx - 3); /* selectedIdx max 4 (RANDOM) -> start max 1 */
    for (i = 0; i < 4; i++)
    {
        u8 row = (u8)(start + i);
        bool8 selected = (row == selectedIdx);

        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (row < POKEPVP_PACKS_PER_CLASS)
        {
            PokePvPPackEntry pack;

            if (PokePvPPacks_Get(battleClass, row, &pack))
            {
                AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                    selected ? sTextColorSelected : sTextColor1, -1, pack.title);
            }
            else
            {
                AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                    selected ? sTextColorSelected : sTextColor1, -1, sText_NoMove);
            }
        }
        else
        {
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sText_RandomPack);
        }
        PutWindowTilemap(sWindowIds[i]);
    }
    /* Row slot 4 (POKEPVP_4, screen rows 14-15) is deliberately always left
     * blank on this screen -- see this function's own comment above. */
    FillWindowPixelBuffer(sWindowIds[4], PIXEL_FILL(10));
    PutWindowTilemap(sWindowIds[4]);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    // POKEPVP (ADR-188): this screen only ever populates 5 rows, but the
    // shared panel border now spans 6 (window 5's rows, freed by
    // ADR-186) -- blank it too, so leftover LEADERBOARD text from the
    // top menu never bleeds through (same "erase, don't just occlude"
    // discipline DrawStartMatchSubmenuItems already used for its own
    // unused rows). Done BEFORE the ERROR-band overview line below,
    // deliberately -- window 5's screen rows (16-17) fall inside the
    // ERROR window's own rows (15-18), and drawing them in the other
    // order let window 5's blank tiles clobber the bottom half of the
    // overview text (a real regression a golden-slice test caught).
    // alpha-build-plan P6 (docs/adr/264): deliberately NOT given the
    // window-5 identity tag every other screen got -- this screen's own
    // real content (the ERROR band's pack overview/playstyle line) draws
    // *after* window 5 specifically so ERROR wins the shared rows, per
    // the comment above. Reversing that order to let a wordmark tag win
    // instead would silently reintroduce the exact regression this
    // ordering was already fixed for. Left blank; not every screen needs
    // it.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
    /* Overview line(s) in the ERROR band. Pack overview (owner ask,
     * 2026-09-13 revision): the ERROR window reserves 4 rows
     * (naming_screen-style message box height) and only the top one was
     * ever used, for the static per-class rules line -- real, spare room
     * for a real second line showing the *hovered pack's own* playstyle
     * (server data, `POKEPVP_MSG_PACK_CATALOG_ENTRY`'s playstyle field),
     * not just the class-wide rules every row already shared. `difficulty`
     * used to prefix this line ("difficulty - playstyle") -- removed
     * entirely (owner ask), so this is now just the playstyle string
     * alone (an abbreviated species list, e.g. "Sno/Tau/Gen/Ala/Sta/Rhy").
     * RANDOM (selectedIdx == POKEPVP_PACKS_PER_CLASS) has no specific pack
     * to describe, so it keeps the rules line as its only line, same as
     * before this ADR. */
    {
        u8 *dst;

        FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
        dst = StringCopy(buf2, (battleClass == 0) ? sText_QuickRulesLineEarly : sText_QuickRulesLineElite);
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 1, 2, sTextColor1, -1, buf2);
        if (selectedIdx < POKEPVP_PACKS_PER_CLASS)
        {
            PokePvPPackEntry pack;

            if (PokePvPPacks_Get(battleClass, selectedIdx, &pack) && pack.playstyle[0] != EOS)
            {
                u8 buf3[POKEPVP_PACKS_MAX_PLAYSTYLE_LEN + 1];

                dst = StringCopy(buf3, pack.playstyle);
                *dst = EOS;
                AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 1, 18, sTextColor1, -1, buf3);
            }
        }
        PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
        CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_GFX);
    }
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
}

/* POKEPVP (UI plan slice 3, simplified ADR-192): pack picker task -- A on
 * a pack row (or RANDOM) starts matchmaking for that pack immediately; B
 * returns straight to the START MATCH submenu (the separate EARLY/ELITE
 * class-picker screen this used to return to is gone -- see
 * Task_PokePvPStartMatchSubmenu's own comment).
 *
 * POKEPVP (owner playtest round 4, ADR-192): A used to hand off to
 * Task_PokePvPTeamSelector -- CUSTOM's own named "TEAM 1 -- READY/EMPTY"
 * multi-slot screen, asking the player to additionally pick one of their
 * own saved teams after already picking a pack. That question makes sense
 * for CUSTOM ELITE (there is no pack; the saved team *is* the roster) but
 * not for a pack pick, which has already fully decided the roster (the
 * real, in-scope fix for the owner's "should go straight to match query,
 * not team list ready -- that's for custom" report). ADR-193's
 * POKEPVP_MSG_PACK_TEAM_MEMBER later closed the deeper gap this comment
 * used to describe (the pack's own server-materialized roster now does
 * push back down to the ROM, StartPokePvPRealMatch's
 * PokePvPTeamBuilder_LoadPackTeamForBattle call) -- which is exactly why a
 * saved team is no longer *required* here. A real saved slot is still
 * preferred when one exists (first non-empty slot, same
 * LoadTeamForBattle/SendTeam plumbing CUSTOM's own selector uses), purely
 * so the brief local "waiting for opponent" window renders the player's
 * own team instead of a placeholder; with no saved team anywhere,
 * StartPokePvPMatchWithTeam now synthesizes one instead of refusing
 * (owner-reported real gap: a pack match's real roster has never been the
 * local save's problem to provide, so gating on one here was always
 * stricter than the actual data dependency). */
static void Task_PokePvPPackPicker(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        u8 row = gTasks[taskId].tSubCursorPos;
        u8 slot;
        u8 autoSlot = POKEPVP_TEAM_SLOTS; /* sentinel: none found yet */

        for (slot = 0; slot < POKEPVP_TEAM_SLOTS; slot++)
        {
            if (PokePvPTeamBuilder_MemberCount(slot) != 0)
            {
                autoSlot = slot;
                break;
            }
        }
        if (autoSlot == POKEPVP_TEAM_SLOTS)
        {
            // No saved team anywhere -- fine for a pack match (see this
            // task's own doc comment): slot 0 is always a valid mailbox
            // slot index, and StartPokePvPMatchWithTeam synthesizes a
            // placeholder local party when the slot it's given is empty.
            autoSlot = 0;
        }

        gTasks[taskId].tTeamSlot = (row < POKEPVP_PACKS_PER_CLASS) ? row : POKEPVP_PICKER_ROW_RANDOM;
        gTasks[taskId].tPickerStage = POKEPVP_PICKER_STAGE_TEAM;
        gTasks[taskId].tSubMode = (gTasks[taskId].tPickerClass == 0)
            ? POKEPVP_MATCH_MODE_QUICK_EARLY
            : POKEPVP_MATCH_MODE_QUICK_ELITE;
        StartPokePvPMatchWithTeam(taskId, autoSlot);
    }
    else if (JOY_NEW(B_BUTTON))
    {
        // POKEPVP (ADR-192): the class picker this used to return to is
        // gone from this path (see Task_PokePvPStartMatchSubmenu's own
        // comment) -- back up straight to the submenu, landing the cursor
        // on whichever of QUICK EARLY/QUICK ELITE got here.
        PlaySE(SE_SELECT);
        gTasks[taskId].tPickerStage = POKEPVP_PICKER_STAGE_MENU;
        gTasks[taskId].tSubCursorPos = gTasks[taskId].tPickerClass;
        DrawStartMatchSubmenuItems(gTasks[taskId].tSubCursorPos);
        gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawPackPickerItems(gTasks[taskId].tSubCursorPos, gTasks[taskId].tPickerClass);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_PICKER_ROW_COUNT - 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawPackPickerItems(gTasks[taskId].tSubCursorPos, gTasks[taskId].tPickerClass);
    }
}

// POKEPVP (ADR-091): drives the START MATCH submenu -- D-pad moves the
// WIN0 highlight between the visible mode rows (START MATCH submenu's own
// 32px-slot geometry), A selects, B returns to the top-level 5-item menu.
// POKEPVP (UI plan slice 3, ADR-192): the rows are QUICK / CUSTOM / INVITE
// / PRACTICE in the Build Plan §2.2 category order (flag-hidden); QUICK
// descends straight into that class's own pack picker (no separate class-
// picker step -- ADR-192), which itself starts the match directly once a
// pack is chosen. Every other mode goes straight to the team selector like
// the pre-slice-3 AUTO MATCH did.
static void Task_PokePvPStartMatchSubmenu(u8 taskId)
{
    u8 rowCount;

    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    rowCount = StartMatchRowCount();
    if (rowCount == 0)
        rowCount = 1;

    if (JOY_NEW(A_BUTTON))
    {
        u8 mode = StartMatchModeForRow(gTasks[taskId].tSubCursorPos);

        PlaySE(SE_SELECT);
        if (mode == POKEPVP_MATCH_MODE_QUICK_EARLY || mode == POKEPVP_MATCH_MODE_QUICK_ELITE)
        {
            // POKEPVP (owner playtest round 4, ADR-192): QUICK EARLY and
            // QUICK ELITE are two distinct, differently-labeled top-menu
            // rows specifically so the player picks a class *here*. This
            // used to route through a separate, freely-navigable two-row
            // EARLY/ELITE class picker (Task_PokePvPClassPicker) that only
            // ever started its cursor on the row matching the label just
            // pressed -- nothing stopped the player from moving the D-pad
            // and picking the *other* class from either entry point, so
            // "QUICK EARLY" could open the elite pack list and vice versa
            // (the exact owner report). The class is already fully decided
            // by which of these two rows was pressed; go straight to that
            // class's own pack list instead of re-asking the same question
            // on a screen that silently accepts a different answer.
            gTasks[taskId].tPickerClass = (mode == POKEPVP_MATCH_MODE_QUICK_EARLY) ? 0 : 1;
            gTasks[taskId].tPickerStage = POKEPVP_PICKER_STAGE_PACK;
            gTasks[taskId].tSubCursorPos = 0;
            DrawPackPickerItems(0, gTasks[taskId].tPickerClass);
            gTasks[taskId].func = Task_PokePvPPackPicker;
        }
        else if (mode == POKEPVP_MATCH_MODE_INVITE)
        {
            // POKEPVP (ADR-193, Gap 2): INVITE MATCH used to fall straight
            // into the team selector with tSubMode=INVITE, which
            // (StartPokePvPMatchWithTeam's own mode==0 mapping) sends the
            // exact same blind inviteJoin as AUTO-MATCH -- no way to name
            // *who* to invite (ADR-181/190/192, confirmed real missing
            // scope, not a bug). Goes to a FRIENDS/RIVALS type picker
            // first (HANDOFF item 23 follow-up), then the real target
            // picker; see each screen's own doc comment.
            gTasks[taskId].tSubCursorPos = 0;
            DrawInviteTargetTypeItems(0);
            gTasks[taskId].func = Task_PokePvPInviteTargetTypePicker;
        }
        else
        {
            gTasks[taskId].tSubMode = mode;
            gTasks[taskId].tPickerStage = POKEPVP_PICKER_STAGE_TEAM;
            gTasks[taskId].tSubCursorPos = 0;
            DrawTeamSelectorItems(0);
            gTasks[taskId].func = Task_PokePvPTeamSelector;
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].func = Task_PokePvPReturnToTopMenuFromSubmenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawStartMatchSubmenuItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < rowCount - 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawStartMatchSubmenuItems(gTasks[taskId].tSubCursorPos);
    }
}

// POKEPVP (ADR-091): INVITE MATCH stub -- identical shape to
// Task_PokePvPMenuStub, but returns into the submenu (not the top-level
// menu) on dismissal, since that's where the player selected it from.

// POKEPVP (ADR-091): B out of the submenu -- fades to black (done by the
// caller before switching to this func), redraws the real 5-item top-level
// menu, fades back in, hands off to Task_UpdateVisualSelection exactly like
// Task_PokePvPMenuStub's own dismissal path does.
static void Task_PokePvPReturnToTopMenuFromSubmenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    DrawPokePvPMenuItems(0);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tCursorPos = 0;
    gTasks[taskId].func = Task_UpdateVisualSelection;
}

// HANDOFF item 23 follow-up (user ask): a FRIENDS/RIVALS chooser in front
// of Task_PokePvPInviteTargetPicker, mirroring Task_PokePvPSocial's own
// two-level shape (tSubCursorPos 0/1 picks the list, stored into
// tSocialList before handing off) rather than growing a second targeting
// mechanism. Two fixed rows, same draw pattern as DrawStartMatchSubmenuItems
// (fill selected/unselected, blank the panel's unused rows).
static void DrawInviteTargetTypeItems(u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
    };
    static const u8 *const sLabels[] = { sText_Friends, sText_Rivals };
    u8 i;

    for (i = 0; i < 2; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
            selected ? sTextColorSelected : sTextColor1, -1, sLabels[i]);
    }
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_2, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_3, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_4, PIXEL_FILL(10));
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 2; i++)
        PutWindowTilemap(sWindowIds[i]);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_2);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_3);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_4);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(sWindowIds[0], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[1], COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void Task_PokePvPInviteTargetTypePicker(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        gTasks[taskId].tSocialList = gTasks[taskId].tSubCursorPos; // 0 FRIENDS, 1 RIVALS
        gTasks[taskId].tSubCursorPos = 0;
        DrawSocialListItems(gTasks[taskId].tSocialList, 0, FALSE);
        gTasks[taskId].func = Task_PokePvPInviteTargetPicker;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        DrawStartMatchSubmenuItems(0);
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawInviteTargetTypeItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawInviteTargetTypeItems(gTasks[taskId].tSubCursorPos);
    }
}

// POKEPVP (ADR-193, Gap 2, extended HANDOFF item 23 follow-up): INVITE
// MATCH's real target picker.
//
// The gap (ADR-181/190/192, confirmed real, not a bug): INVITE MATCH used
// to send the exact same blind inviteJoin as AUTO-MATCH -- no way to name
// *who* to invite. `inviteJoin` itself has no target field on the wire
// (packages/contracts/src/gateway-protocol.ts) and the gateway's own
// inviteJoin handler is unconditionally blind pairing (gateway-server.ts).
//
// The real, already-working addressed mechanism (per ADR-192's own
// scoping note) is the SOCIAL screen's own per-friend/rival CHALLENGE
// action (Task_PokePvPSocialRowMenu, this file) -- it sends a real
// SOCIAL_ACTION{list, action=0 CHALLENGE, index} that the launcher
// resolves against its own cached friends/rivals accountIds
// (client/launcher/src/main.rs's SOCIAL_ACTION handler) into a real,
// addressed challengeCreate, landing in the target's real challenge inbox
// (POKEPVP_MSG_CHALLENGE_ARRIVED, ADR-184) -- a fundamentally different,
// already-addressed wire path from inviteJoin's anonymous queue. This
// screen is a second *entry point* into that exact pipeline, not a
// second targeting mechanism: it reuses DrawSocialListItems/
// PokePvPSocial_Count/PokePvPSocial_Get verbatim (same rendering SOCIAL's
// own lists already use) and, on a real selection, sets the exact same
// state (tSocialList/tInboxSlot, tSubMode = POKEPVP_MATCH_MODE_
// CHALLENGE_TARGET) Task_PokePvPSocialRowMenu's own CHALLENGE branch
// already sets before handing to Task_PokePvPTeamSelector -- the shared
// team-pick -> SendSocialAction tail (StartPokePvPMatchWithTeam) is
// completely unchanged.
//
// Now works for both FRIENDS and RIVALS (HANDOFF item 23 follow-up, user
// ask): tSocialList is set by the new Task_PokePvPInviteTargetTypePicker
// above instead of being hardcoded to 0, and the launcher's own
// handle_social_action CHALLENGE branch (main.rs) resolves a rival's
// accountId exactly like a friend's now (ADR-201 already cached it for
// the rival presence subscription). BLOCKS is deliberately not offered as
// a third type-picker row -- a block is someone explicitly kept out, an
// odd thing to invite, and the launcher's block cache has no accountId at
// all (team_persistence::fetch_blocks returns name/tag pairs only), so
// there is nothing to resolve a challenge against even if a row existed.
static void Task_PokePvPInviteTargetPicker(u8 taskId)
{
    u8 list = gTasks[taskId].tSocialList;

    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        if (gTasks[taskId].tSubCursorPos >= PokePvPSocial_Count(list))
        {
            // Same "say so instead of doing nothing" shape as
            // Task_PokePvPTeamSelector's own empty-slot fix (ADR-190) --
            // an empty/unselected row here is a real, expected UI state,
            // not a bug, but silently doing nothing reads as a dead
            // button.
            PlaySE(SE_FAILURE);
            FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
            MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 0, 2, sTextColor1, -1,
                list == 1 ? sText_NoRivalsToInvite : sText_NoFriendsToInvite);
            PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_FULL);
            return;
        }
        PlaySE(SE_SELECT);
        // Exactly Task_PokePvPSocialRowMenu's own CHALLENGE branch: pick a
        // team, then the selector's A sends the team records + a real,
        // addressed SOCIAL_ACTION challenge at this target. tSocialList is
        // already FRIENDS(0)/RIVALS(1), set by
        // Task_PokePvPInviteTargetTypePicker before this screen was ever
        // reached -- left untouched here, not reset to 0, so a rival pick
        // stays a rival pick.
        gTasks[taskId].tInboxSlot = gTasks[taskId].tSubCursorPos; // row index
        gTasks[taskId].tSubMode = POKEPVP_MATCH_MODE_CHALLENGE_TARGET;
        gTasks[taskId].tSubCursorPos = 0;
        DrawTeamSelectorItems(0);
        gTasks[taskId].func = Task_PokePvPTeamSelector;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        // Erase the "no friends"/"no rivals" message (if shown) before
        // leaving -- this window is never otherwise touched by the START
        // MATCH submenu, same "erase, don't occlude" discipline as
        // Task_PokePvPTeamSelector's own B-handler. Back to the type
        // picker, not straight to the submenu -- one B undoes one step.
        MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
        gTasks[taskId].tSubCursorPos = list;
        DrawInviteTargetTypeItems(list);
        gTasks[taskId].func = Task_PokePvPInviteTargetTypePicker;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawSocialListItems(list, gTasks[taskId].tSubCursorPos, FALSE);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_SOCIAL_MAX_ENTRIES - 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawSocialListItems(list, gTasks[taskId].tSubCursorPos, FALSE);
    }
}

// POKEPVP (ADR-095): one team-selector row. Deliberately terser than the
// builder's own row below (exact "x/6" member count) -- this screen is a
// picker, not an editor, so READY/EMPTY is all a player needs to decide.
/* ADR-158 (owner-reported live, "team selector visually crooked"):
 * each row used to border itself with MainMenu_DrawWindow -- a full frame
 * stamped INSIDE the same 2-tile/16px-tall row the 16px text already
 * fills -- so packed rows read as cramped double-outlined boxes with the
 * frame tiles cutting the text ("arrow/chevron on top of the text").
 * Rows now follow the top menu's own reworked style (DrawPokePvPMenuItems):
 * a plain fill, a color-swap selection bar, and ONE panel frame around the
 * whole block instead of one box per row (drawn by DrawTeamSelectorItems). */
static void DrawOneSelectorRow(u8 windowId, u8 slot, bool8 selected)
{
    u8 buf[24];
    u8 *dest;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(selected ? 13 : 10));
    dest = StringCopy(buf, sText_Team);
    dest = ConvertIntToDecimalStringN(dest, slot + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
    *dest++ = CHAR_SPACE;
    *dest++ = CHAR_SPACE;
    *dest++ = CHAR_SPACE;
    if (PokePvPTeamBuilder_MemberCount(slot) == 0)
        dest = StringCopy(dest, sText_TeamEmpty);
    else
        dest = StringCopy(dest, sText_TeamReady);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, selected ? sTextColorSelected : sTextColor1, -1, buf);
    PutWindowTilemap(windowId);
}

// POKEPVP (ADR-095): five-row team picker opened from AUTO-MATCH, in place
// of firing StartPokePvPAutoMatch() directly -- there was previously no way
// to choose which built team entered the match. Deliberately a different
// screen from Task_PokePvPTeamList (TEAM BUILDER, menu cursor 1) below, not
// a mode of it: A here starts a match with the highlighted team immediately
// (or does nothing on an empty one); there is no EDIT TEAM/EDIT MOVES
// submenu here at all, so a player can't confuse "pick a team to play" with
// "edit a team".
/* ADR-158: now takes the selected index and draws the ONE panel frame
 * around the whole 5-row block (sPokePvPMenuPanelTemplate bounds rows
 * 6-15 exactly), matching the top menu's own reworked look and giving
 * the selector a real, redraw-on-move selection bar. */
static void DrawTeamSelectorItems(u8 selectedIdx)
{
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_0, 0, selectedIdx == 0);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_1, 1, selectedIdx == 1);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_2, 2, selectedIdx == 2);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_3, 3, selectedIdx == 3);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_4, 4, selectedIdx == 4);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_0, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_1, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_FULL);
    // POKEPVP (ADR-188): see DrawStartMatchSubmenuItems's identical
    // comment -- blank window 5 too so leftover LEADERBOARD text never
    // bleeds through the now-taller shared panel.
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

// POKEPVP (ADR-095): drives the team-selector list. Same D-pad/32px-slot
// input shape as every other list in this file. A on an empty slot is
// silently ignored -- an unselectable row is expected UI here, not a
// mistake worth a dialog. A on a populated slot loads that team's real
// Pokemon into gPlayerParty (PokePvPTeamBuilder_LoadTeamForBattle) before
// handing off to StartPokePvPAutoMatch, so gPlayerPartyCount is non-zero by
// the time StartPokePvPDebugBattle runs and its own gPlayerPartyCount == 0
// synthesis (ADR-067) never overwrites it.
// POKEPVP (ADR-121): a real, bounded number of frames to let the mailbox
// deliver a real gateway-paired opponent (POKEPVP_MSG_REAL_OPPONENT_MON)
// before giving up and falling back to the existing debug battle. Not a
// guess: every real two-launcher pairing this project's own live sessions
// have produced (ADR-115/116/119/120) resolved species+level within a
// handful of real frames of `matchStart`, so 300 (5 real seconds at 60fps)
// leaves generous headroom above observed real-world latency while still
// bounding a genuinely unreachable/misconfigured gateway to a short,
// visible wait rather than an indefinite hang -- this project's own
// standing rule against leaving a player stuck in a silent wait with no
// way out.
// POKEPVP (ADR-124): 300 frames (5s) was the right bound when this wait
// was only ever entered *after* a pairing had already succeeded and only
// the opponent's own mon was still in flight. It is the wrong bound now
// that an online launcher enters this wait to wait for the pairing itself
// -- i.e. for a human friend to press AUTO-MATCH on their own machine.
// 1800 frames is 30 real seconds, and the timeout is no longer a silent
// fallback into a fake battle (see this task's own timeout branch), so a
// bound that is generous rather than tight costs nothing.
#define POKEPVP_AUTO_MATCH_WAIT_FRAMES 1800

// POKEPVP (ADR-121): shown between AUTO-MATCH's team pick and the actual
// battle start, giving a real gateway-paired opponent a bounded chance to
// show up. Same message-window shape as Task_PokePvPPrepareRoster (a
// visible, honest wait on the one screen that needs it) but resolves on
// either real progress (PokePvP_IsRealOpponentReady(), set by
// battle_controller_pokepvp.c once POKEPVP_MSG_REAL_OPPONENT_MON arrives)
// or a real, bounded timeout -- a timeout is not a failure state here:
// with no gateway configured at all, PokePvP_IsRealOpponentReady() simply
// never becomes true, and that must still lead to the existing debug
// battle exactly as before. Either outcome calls the same, unchanged
// StartPokePvPAutoMatch() -- overworld.c's own CB2_Overworld branch is
// what actually decides real-vs-debug at the instant the battle fires
// (checking the same flag again there, right before it matters), so this
// task's only real job is deciding *when* to stop waiting, never which
// battle starts.
// POKEPVP (ADR-207): set immediately before Task_PokePvPWaitForRealOpponent
// hands off to Task_PokePvPNoOpponentFound on a real decline (rather than a
// genuine timeout), so that shared screen's own case 0 knows which of the
// two distinct messages to print. Consumed (cleared) the instant it's read.
static bool8 sPokePvPShowDeclinedMessage;
// ADR-242: same shape and lifecycle as sPokePvPShowDeclinedMessage just
// above, for the honest "Opponent is busy." case.
static bool8 sPokePvPShowBusyMessage;

// Team management options: RENAME's own round trip through
// DoNamingScreen (which destroys every task, so these have to be plain
// statics that outlive the round trip, not task data) and COPY TO...'s
// remembered source slot while its destination picker is up.
static u8 sPokePvPRenameSlot;
static u8 sPokePvPTeamNameBuffer[POKEPVP_TEAM_NAME_LENGTH + 1];
static u8 sPokePvPCopySourceSlot;

// POKEPVP (ADR-217, redesigned ADR-226): SOCIAL FRIENDS' "ADD FRIEND"
// round trip through DoNamingScreen -- same "plain static, not task
// data" reason as sPokePvPTeamNameBuffer above. Reuses NAMING_SCREEN_BOX
// (BOX_NAME_LENGTH == 8) rather than a dedicated template: adding one
// would mean editing naming_screen.c itself, vendored upstream code
// (prohibition #16). ADR-226 dropped the numeric-only requirement this
// buffer used to serve -- the naming screen's own ordinary default
// (letters-first) keyboard is exactly what's wanted now, no vendored
// edit needed at all -- so this is now a plain up-to-8-character
// username buffer; CB2_PokePvPAddFriendNameEntered converts and bounds
// it instead of validating exactly 4 digits.
static u8 sPokePvPAddFriendTagBuffer[BOX_NAME_LENGTH + 1];

static void Task_PokePvPWaitForRealOpponent(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        // Deliberately does NOT clear PokePvP_IsRealOpponentReady() here --
        // tried first, and found live to be a real bug, not a safety net:
        // a real opponent's mon routinely arrives within a few hundred
        // frames of pairing, long before this wait task's own screen is
        // ever reached, so clearing it here discarded real, current,
        // already-arrived data instead of only stale leftovers. The right
        // place to reset stale state from an *earlier, unrelated* pairing
        // is the instant a *new* one begins (POKEPVP_MSG_REAL_MATCH_PENDING's
        // own handler, battle_controller_pokepvp.c) -- not here.
        PrintMessageOnWindow4(gText_PokePvPWaitingForOpponent);
        gTasks[taskId].tWaitFrames = 0;
        gTasks[taskId].tMGErrorMsgState++;
        // POKEPVP (ADR-207): clear any stale decline left over from an
        // earlier, unrelated invite that arrived after the ROM had
        // already locally bailed on it (its own 30s timeout or a B-button
        // cancel) -- see POKEPVP_MSG_CHALLENGE_DECLINED's own doc comment
        // (presentation_types.h) for why this is safe here, unlike
        // sPokePvPRealOpponentReady just below, which is deliberately NOT
        // cleared at this same point.
        PokePvP_ClearChallengeDeclined();
        // ADR-242: same reasoning as PokePvP_ClearChallengeDeclined just
        // above -- a stale busy flag from an earlier, already-abandoned
        // wait must not bleed into this new one.
        PokePvP_ClearOpponentBusy();
        break;
    case 1:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
    {
        bool8 ready = PokePvP_IsRealOpponentReady();

        // POKEPVP (ADR-207, HANDOFF item 7): a real, immediate decline
        // ends this wait exactly like the 30s timeout below (same target
        // task, same fade/dismiss shape), but with an honest, distinct
        // message instead of silently running out the clock. Checked
        // before the ready-check prompt below for the same reason the
        // B-cancel further down is: a real opponent can't both be ready
        // and have declined, so `!ready` gates all three the same way.
        if (!ready && PokePvP_IsChallengeDeclined())
        {
            PokePvP_ClearChallengeDeclined();
            PokePvP_ClearRealMatchPending();
            DebugPrintf("POKEPVP: AUTO-MATCH/INVITE challenge declined by target after %d frames", gTasks[taskId].tWaitFrames);
            sPokePvPShowDeclinedMessage = TRUE;
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPNoOpponentFound;
            break;
        }

        // ADR-242: same shape as the decline check just above, for the
        // gateway's own opponent_busy rejection (a real, addressed
        // SOCIAL CHALLENGE/INVITE MATCH target already in another match/
        // queue right now).
        if (!ready && PokePvP_IsOpponentBusy())
        {
            PokePvP_ClearOpponentBusy();
            PokePvP_ClearRealMatchPending();
            DebugPrintf("POKEPVP: AUTO-MATCH/INVITE target is busy after %d frames", gTasks[taskId].tWaitFrames);
            sPokePvPShowBusyMessage = TRUE;
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPNoOpponentFound;
            break;
        }

        /* POKEPVP (UI plan slice 3): while this wait is on screen, a
         * gateway ready-check gets a real A/B prompt. The prompt's own
         * A/B handling consumes the frame (returns TRUE); a cancel
         * returns to the START MATCH submenu the player came from. */
        if (!ready && TickReadyCheckPrompt(taskId))
        {
            if (PokePvP_IsReadyCheckCancelled())
            {
                PokePvP_ClearRealMatchPending();
                ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
                MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
                DrawStartMatchSubmenuItems(0);
                gTasks[taskId].tSubCursorPos = 0;
                gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
            }
            break;
        }

        // POKEPVP (owner playtest round 4, ADR-192; real wire message
        // added HANDOFF items 22/38): "add a cancel option when waiting
        // for opponent." The ready-check sub-state above already has a
        // B-driven cancel (TickReadyCheckPrompt /
        // PokePvP_IsReadyCheckCancelled), but this earlier, plain
        // "Waiting for opponent..." state -- reached before any real
        // ready-check has arrived, which is most of this wait for a
        // typical pairing -- had none: a player who queued and changed
        // their mind had no way out except the full 1800-frame (30s)
        // timeout. Same local shape as the ready-check cancel above (clear
        // the pending real match, return to the START MATCH submenu), now
        // also emitting POKEPVP_MSG_LEAVE_QUEUE so the launcher forwards a
        // real queueLeave/inviteJoin-cancel to the gateway instead of
        // leaving a phantom queued session for its own timeout to notice.
        if (!ready && JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            SendLeaveQueue();
            PokePvP_ClearRealMatchPending();
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            DrawStartMatchSubmenuItems(0);
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
            break;
        }

        gTasks[taskId].tWaitFrames++;
        if (ready)
        {
            DebugPrintf("POKEPVP: AUTO-MATCH real opponent ready after %d frames", gTasks[taskId].tWaitFrames);
            PokePvP_ClearRealMatchPending();
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            StartPokePvPAutoMatch();
        }
        else if (gTasks[taskId].tWaitFrames >= POKEPVP_AUTO_MATCH_WAIT_FRAMES)
        {
            // POKEPVP (ADR-124): this used to fall through to
            // StartPokePvPAutoMatch() as well, which -- with no real
            // opponent ready -- means overworld.c's own branch starts
            // StartPokePvPDebugBattle(): a local battle against FireRed's
            // native AI, presented identically to a real match. That is
            // exactly what the owner played on 2026-09-03 and reported as
            // "the opponent still behaves like AI." An online launcher now
            // says so instead. See POKEPVP_MSG_ONLINE_MODE's doc comment
            // (presentation_types.h) for why this task is only ever
            // reached in online mode at all, so this branch never affects
            // the offline debug path every golden-frame suite uses.
            DebugPrintf("POKEPVP: AUTO-MATCH found no real opponent in %d frames", gTasks[taskId].tWaitFrames);
            PokePvP_ClearRealMatchPending();
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPNoOpponentFound;
        }
        break;
    }
    }
}

// POKEPVP (ADR-124): the honest end of an AUTO-MATCH that never found
// anyone. The same shape and return target as the old
// Task_PokePvPInviteMatchStub (retired in Phase J v2 when the INVITE
// MATCH item became real) -- a message on the same already-open window,
// dismissed with A or B, back into the START MATCH submenu the player
// selected AUTO-MATCH from -- rather than a new screen: this is the same
// "tell the player and put them back where they were" case that task
// already solves, and reusing it keeps one dismissal/fade path instead
// of two.
static void Task_PokePvPNoOpponentFound(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        // POKEPVP (ADR-207, HANDOFF item 7): shared entry point for both
        // the honest 30s timeout (Task_PokePvPWaitForRealOpponent's own
        // fall-through) and a real, immediate decline
        // (sPokePvPShowDeclinedMessage, set right before that same
        // transition) -- distinct text, same dismiss/return shape either
        // way. Consumed immediately so a later, unrelated timeout never
        // shows the wrong message.
        PrintMessageOnWindow4(sPokePvPShowBusyMessage ? sText_OpponentBusy
            : sPokePvPShowDeclinedMessage ? sText_ChallengeDeclined : sText_NoOpponentFound);
        sPokePvPShowBusyMessage = FALSE;
        sPokePvPShowDeclinedMessage = FALSE;
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (!gPaletteFade.active)
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 3:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            DrawStartMatchSubmenuItems(gTasks[taskId].tSubCursorPos);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// POKEPVP (UI plan slice 2, Phase I): POST-MATCH screen.
//
// Reached from Task_WaitFadeAndPrintMainMenuText while a post-match
// session is pending. Layout reuses the five menu windows exactly like
// every other screen here: window 0 shows the result line, windows 1-4
// are REMATCH / PLAY AGAIN / ADD RIVAL / EXIT (cursor = action value,
// matching POKEPVP_POST_MATCH_ACTION_*), and the ERROR window below the
// panel shows the summary line (duration / turns / remaining Pokemon) or
// a one-off message during the wait states. EXIT (and B) clears the
// session in-ROM and returns to the top-level menu; every other action
// reports POST_MATCH_ACTION to the host and waits -- the host owns the
// network call each implies (rematchRequest, queue re-join,
// rivals API) and answers with POST_MATCH_RESULT, which the wait tasks
// surface as a message and then return to this screen.
// ---------------------------------------------------------------------------

// The bound for the ADD RIVAL wait: the host always answers a
// POST_MATCH_ACTION immediately, so 600 frames (10s) is a generous
// backstop, never the expected path.
#define POKEPVP_POST_MATCH_RESULT_WAIT_FRAMES 600

static void SendPostMatchAction(u8 action)
{
    if (action > POKEPVP_POST_MATCH_ACTION_EXIT)
        return;
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_POST_MATCH_ACTION,
                                0,
                                action,
                                &action,
                                sizeof(action));
    DebugPrintf("POKEPVP: post-match action=%d sent", action);
}

/* POKEPVP (UI plan slice 3): the ready-check prompt (Build Plan §8 item
 * 2's "must both confirm" gate). Called every frame from the waiting
 * tasks (Task_PokePvPWaitForRealOpponent and
 * Task_PokePvPPostMatchWait); returns TRUE when it consumed the frame
 * (prompt shown / input handled / budget ticked), FALSE when no prompt
 * is pending so the caller's own wait logic runs unchanged.
 *
 * A=YES answers READY (accept), B=NO or budget lapse answers CANCEL;
 * both clear the prompt and emit POKEPVP_MSG_READY_CHOICE (host
 * forwards readyCheckAck). B additionally prints "Match cancelled." and
 * routes the caller back through the same honest message flow as
 * Task_PokePvPNoOpponentFound by clearing the real-match pending state;
 * the caller detects that via PokePvP_IsReadyCheckCancelled() and
 * returns to its own previous screen. */
static bool8 sReadyCheckCancelled;
static bool8 sReadyPromptShown;
// HANDOFF item 37: frames left showing "READY FOR BATTLE!" after A is
// pressed, before the window reverts to the ordinary waiting message.
// Nonzero means TickReadyCheckPrompt is mid-flash and should keep
// consuming the frame regardless of PokePvPReadyCheck_IsPending() (the
// ready check itself is already cleared and acked by this point). 60
// frames (~1s at the GBA's ~59.7fps) -- long enough to read, short
// enough not to meaningfully delay the real match start once the
// opponent's own mon arrives (which still preempts the flash immediately:
// both call sites gate this whole function behind `!ready`).
#define POKEPVP_READY_CONFIRM_FLASH_FRAMES 60
static u16 sReadyConfirmFramesRemaining;

static bool8 PokePvP_IsReadyCheckCancelled(void)
{
    return sReadyCheckCancelled;
}

static void SendReadyChoice(u8 accept)
{
    u8 payload = accept;

    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_READY_CHOICE,
                                0,
                                accept,
                                &payload,
                                sizeof(payload));
    DebugPrintf("POKEPVP: ready choice accept=%d sent", accept);
}

// HANDOFF items 22/38 (real leave-queue wire message): sent from either
// plain-wait "Waiting for opponent..." cancel, mirroring SendReadyChoice's
// own zero-retry shape (a dropped send here just means the gateway's own
// timeout eventually notices instead of an immediate leave -- no worse
// than the B-cancel's pre-existing "sends nothing at all" behavior, and
// consistent with this ready-check-adjacent send site's own established,
// un-audited pattern rather than introducing a new discipline here).
static void SendLeaveQueue(void)
{
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_LEAVE_QUEUE,
                                0,
                                0,
                                NULL,
                                0);
    DebugPrintf("POKEPVP: leave queue sent");
}

// ADR-209: sent when the trainer-sprite picker confirms a pick. Same
// fire-and-forget shape as SendLeaveQueue/SendReadyChoice above -- see
// that ADR's own Non-goals for why a dropped send here is low-stakes
// (the ROM's own local effect, PokePvPProfile_SetSpriteId and the
// Red/Leaf gender write, already happened before this is even called).
static void SendSetSprite(u8 spriteId)
{
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_SET_SPRITE,
                                0,
                                spriteId,
                                &spriteId,
                                sizeof(spriteId));
    DebugPrintf("POKEPVP: set sprite=%d sent", spriteId);
}

/* POKEPVP (team-list corruption fix): FireRed's own naming screen stores
 * `name` in its internal charmap ('0'-'9' at 0xA1, 'A'-'Z' at 0xBB, 'a'-'z'
 * at 0xD5, space at 0x00 -- the same layout battle_controller_pokepvp.c's
 * own PokePvPAsciiToCharmap documents), not ASCII. Every other outbound
 * mailbox message that carries player-entered or server-bound text
 * (SendSocialAction's targets are indices, not names, so it doesn't need
 * this -- the actual precedent is the *inbound* direction's own
 * PokePvPAsciiToCharmap/ProfileAsciiToCharmap/SocialAsciiToCharmap/
 * InboxAsciiToCharmap/PackTextAsciiToCharmap/PokePvPPostMatchAsciiToCharmap
 * family, ASCII in, charmap out) converts at the ROM/host boundary; this
 * was the one place that sent raw charmap bytes to the host labeled as if
 * they were already plain text. The host's own TEAM_RENAME handler
 * (`main.rs`) UTF-8-decodes the payload and persists it verbatim via
 * `PATCH /v1/teams/:teamId` -- raw charmap bytes (>= 0x80 for every real
 * letter) are not valid UTF-8, so `String::from_utf8_lossy` replaced them
 * with U+FFFD, and that garbage got saved as the team's real name
 * (live-reproduced: `logs/launcher-781852.log` frame 2105, `TEAM_RENAME
 * slot=0 name="\xEF\xBF\xBD\xEF\xBF\xBD" -> OK`). Converting to ASCII
 * here, symmetric with the inbound family, is the missing half of the
 * round trip; see POKEPVP_MSG_TEAM_NAME's own receive-side fix
 * (battle_controller_pokepvp.c) for the other half (raw ASCII was also
 * being written straight into the charmap-native `sTeamNames[]` on boot
 * restore, with no conversion at all, corrupting every restored name's
 * on-screen glyphs regardless of whether this send-side bug had already
 * run). Unconvertible bytes are dropped, matching the inbound family's own
 * "fail closed on unexpected input" contract; an all-garbage `name` sends
 * nameLen=0, which the host and the next restore both already render
 * safely as "no custom name" rather than corrupting anything. */
static u8 TeamNameCharmapToAscii(u8 c)
{
    if (c >= 0xBB && c <= 0xD4)
        return (u8)('A' + (c - 0xBB));
    if (c >= 0xD5 && c <= 0xEE)
        return (u8)('a' + (c - 0xD5));
    if (c >= 0xA1 && c <= 0xAA)
        return (u8)('0' + (c - 0xA1));
    if (c == 0x00)
        return ' ';
    return 0; /* sentinel: not a legal name character, drop it */
}

// Team management options: sent when RENAME's naming-screen round trip
// confirms a name (CB2_PokePvPTeamRenamed below) and when SET AS ACTIVE
// picks a slot (Task_PokePvPSlotMenu). Same fire-and-forget shape as
// SendSetSprite above -- the ROM's own local copy
// (PokePvPTeamBuilder_SetName/SetNameString /SetActiveSlotLocal) has
// already been updated before either of these is called, so a dropped
// send here costs a stale server-side name/active-flag until the next
// successful one, never a wrong local display.
static void SendTeamRename(u8 slot, const u8 *name)
{
    u8 payload[2 + POKEPVP_TEAM_NAME_LENGTH];
    u8 nameLen = 0;
    u8 i;

    for (i = 0; i < POKEPVP_TEAM_NAME_LENGTH && name[i] != EOS; i++)
    {
        u8 ascii = TeamNameCharmapToAscii(name[i]);

        if (ascii != 0)
            payload[2 + nameLen++] = ascii;
    }
    payload[0] = slot;
    payload[1] = nameLen;
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_TEAM_RENAME,
                                0,
                                slot,
                                payload,
                                2 + nameLen);
    DebugPrintf("POKEPVP: team rename slot=%d nameLen=%d sent", slot, nameLen);
}

static void SendActiveTeamSlot(u8 slot)
{
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_SET_ACTIVE_TEAM,
                                0,
                                slot,
                                &slot,
                                sizeof(slot));
    DebugPrintf("POKEPVP: set active team slot=%d sent", slot);
}

// Team management options: DoNamingScreen's own return callback for
// RENAME (Task_PokePvPSlotMenu's case 2). By the time this runs,
// sPokePvPTeamNameBuffer holds the player's entered name (EOS-terminated,
// naming_screen.c's own contract) and every task from before the naming
// screen has already been destroyed -- same "round trip through a non-task
// screen" shape as PLAYER SETTINGS' own NAME action (DoNamingScreen ->
// CB2_InitMainMenu directly, no deep link back into a specific submenu).
static void CB2_PokePvPTeamRenamed(void)
{
    PokePvPTeamBuilder_SetNameString(sPokePvPRenameSlot, sPokePvPTeamNameBuffer);
    SendTeamRename(sPokePvPRenameSlot, sPokePvPTeamNameBuffer);
    CB2_InitMainMenu();
}

// POKEPVP (ADR-217, redesigned ADR-226): fire-and-forget, same shape as
// SendTeamRename above (a single attempt, no retry-until-MB_OK --
// accepted risk, matching this screen's own existing precedent for a
// one-off player-initiated social action rather than a boot-critical
// burst). Payload is now [nameLen][ascii bytes], mirroring
// SendTeamRename's own [slot][nameLen][bytes] shape (minus the slot byte
// this message never needed). `asciiName`/`nameLen` must already be
// validated ASCII, 1..POKEPVP_ADD_FRIEND_NAME_MAX bytes --
// CB2_PokePvPAddFriendNameEntered below is the only caller.
static void SendAddFriendTag(const u8 *asciiName, u8 nameLen)
{
    u8 payload[1 + POKEPVP_ADD_FRIEND_NAME_MAX];
    u8 i;

    payload[0] = nameLen;
    for (i = 0; i < nameLen; i++)
        payload[1 + i] = asciiName[i];
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_ADD_FRIEND_TAG,
                                0,
                                0,
                                payload,
                                1 + nameLen);
    DebugPrintf("POKEPVP: add friend username sent (len=%d)", nameLen);
}

// POKEPVP (ADR-226): DoNamingScreen's own return callback for SOCIAL
// FRIENDS' "ADD FRIEND" row (Task_PokePvPSocialList's ADD FRIEND branch
// below). Same "round trip through a non-task screen, callback into
// CB2_InitMainMenu directly" shape as CB2_PokePvPTeamRenamed above.
// Reuses NAMING_SCREEN_BOX (BOX_NAME_LENGTH == 8, its own ordinary
// default letters-first keyboard -- no vendored-code edit needed, unlike
// ADR-216's own rejected numeral-default idea, since a username is
// exactly what that default keyboard is for) -- converts every typed
// charmap byte up to the first EOS via TeamNameCharmapToAscii (the same
// conversion TEAM_RENAME's own send path already uses), same
// "drop unconvertible bytes rather than corrupt/reject the whole buffer"
// discipline ADR-215/218 settled on for this class of field. An
// all-unconvertible or empty result is the one real invalid-input case
// (nothing legible was typed) -- PokePvPSocial_NoteInvalidAddFriendInput
// sets the same pending-result flag a real wire ADD_FRIEND_RESULT would,
// so Task_PokePvPAddFriendResultDismiss's display path is uniform for
// both.
static void CB2_PokePvPAddFriendNameEntered(void)
{
    u8 ascii[BOX_NAME_LENGTH];
    u8 asciiLen = 0;
    u8 i;

    for (i = 0; i < BOX_NAME_LENGTH && sPokePvPAddFriendTagBuffer[i] != EOS; i++)
    {
        u8 c = TeamNameCharmapToAscii(sPokePvPAddFriendTagBuffer[i]);
        if (c != 0)
            ascii[asciiLen++] = c;
    }

    if (asciiLen > 0)
        SendAddFriendTag(ascii, asciiLen);
    else
        PokePvPSocial_NoteInvalidAddFriendInput();

    // POKEPVP (ADR-233, owner-reported live: the result only ever showed
    // after re-entering FRIENDS manually, not right after submitting):
    // CB2_InitMainMenu always lands on the top menu, never directly back
    // on this screen -- sPokePvPReturnToSocialFriends (same shape as
    // sPokePvPReturnToTeamList) tells Task_UpdateVisualSelection, the one
    // place every re-init passes through, to skip straight to FRIENDS
    // with the result already showing instead.
    sPokePvPReturnToSocialFriends = TRUE;
    CB2_InitMainMenu();
}

/* POKEPVP (UI plan slice 3): the ready-check prompt (Build Plan §8 item
 * 2's "must both confirm" gate). Called every frame from the waiting
 * tasks (Task_PokePvPWaitForRealOpponent and
 * Task_PokePvPPostMatchWait); returns TRUE when it consumed the frame
 * (prompt shown / input handled / budget ticked), FALSE when no prompt
 * is pending so the caller's own wait logic runs unchanged.
 *
 * A=YES answers READY (accept), B=NO or budget lapse answers CANCEL;
 * both clear the prompt and emit POKEPVP_MSG_READY_CHOICE (the host
 * forwards readyCheckAck). A cancel sets
 * PokePvP_IsReadyCheckCancelled() so the caller returns to its own
 * previous screen immediately -- the prompt text on screen already told
 * the player what B means, so a silent return is unambiguous. */
static bool8 TickReadyCheckPrompt(u8 taskId)
{
    // HANDOFF item 37: mid-flash takes priority over the pending check --
    // the ready check is already cleared/acked by the time this starts
    // (see the A_BUTTON branch below), so PokePvPReadyCheck_IsPending()
    // would otherwise read FALSE and skip straight past the flash.
    if (sReadyConfirmFramesRemaining != 0)
    {
        RunTextPrinters();
        if (--sReadyConfirmFramesRemaining == 0)
            PrintMessageOnWindow4(gText_PokePvPWaitingForOpponent);
        return TRUE;
    }

    if (!PokePvPReadyCheck_IsPending())
    {
        sReadyPromptShown = FALSE;
        return FALSE;
    }

    if (!sReadyPromptShown)
    {
        PrintMessageOnWindow4(sText_ReadyPrompt);
        sReadyPromptShown = TRUE;
    }

    // POKEPVP (owner-reported live, 2026-09-10, ADR-191): PrintMessageOnWindow4
    // starts a real text-printer job (AddTextPrinterParameterized3's speed=2
    // is a scrolling printer, not an instant blit) -- every OTHER caller in
    // this file follows it with its own case that polls RunTextPrinters()
    // every frame until IsTextPrinterActive() clears (see this task's own
    // case 1, or Task_PokePvPNoOpponentFound's case 2). This function is
    // reached from case 2 of both callers, *after* that polling case has
    // already finished for the ORIGINAL "Waiting for opponent..." message,
    // so nothing was left driving the printer for a *second*, later job
    // queued here. Verified live (headless, --dump-milestones pixel
    // inspection, no fixture/index check): the window was cleared and
    // re-bordered (FillWindowPixelBuffer + MainMenu_DrawWindow both ran)
    // but sat visibly BLANK -- no "READY? A=YES B=NO" glyphs ever
    // appeared, for the entire 15s gateway window and beyond -- because
    // nothing called RunTextPrinters() to advance the queued job. A/B
    // input was never actually broken (JOY_NEW below always worked), but a
    // real player facing a blank box had no visible reason to press
    // either, so a live two-human CUSTOM ELITE match read as "stuck at
    // waiting-for-opponent forever" even though the ROM's own 30s local
    // backstop (PokePvPReadyCheck_Tick's own lapse branch below) would
    // eventually auto-decline and bounce back to the submenu on its own.
    // Calling RunTextPrinters() every frame the prompt is pending is the
    // same fix shape this file already uses everywhere else the printer
    // needs to progress.
    RunTextPrinters();

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        sReadyCheckCancelled = FALSE;
        PokePvPReadyCheck_Clear();
        SendReadyChoice(1);
        // HANDOFF item 37: a brief "READY FOR BATTLE!" confirmation
        // before falling back to the ordinary waiting message, instead
        // of silently re-showing "Waiting for opponent..." the instant A
        // is pressed. Still consumes the frame (TRUE) so the caller's
        // own case just breaks, same as every other branch here; the
        // flash itself is preempted the moment the real opponent's mon
        // arrives (`!ready` gates this whole function at both call
        // sites), so it never delays an actual match start.
        sReadyPromptShown = FALSE;
        PrintMessageOnWindow4(sText_ReadyForBattle);
        sReadyConfirmFramesRemaining = POKEPVP_READY_CONFIRM_FLASH_FRAMES;
        return TRUE;
    }
    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        sReadyCheckCancelled = TRUE;
        PokePvPReadyCheck_Clear();
        SendReadyChoice(0);
        return TRUE; /* caller returns to its previous screen */
    }
    if (!PokePvPReadyCheck_Tick())
    {
        /* Window lapsed: treat as declined, same as B. */
        sReadyCheckCancelled = TRUE;
        PokePvPReadyCheck_Clear();
        SendReadyChoice(0);
        return TRUE;
    }
    /* Prompt on screen; keep it. */
    return TRUE;
}

static void DrawPostMatchResultLine(u8 windowId)
{
    PokePvPPostMatch pm;
    /* POKEPVP (ADR-198): worst case is "YOU LOSE" (8) + " vs " (4) +
     * a full-length oppName (POKEPVP_POST_MATCH_MAX_OPP_NAME=16) +
     * CHAR_SPACE (1) + a full-length oppTag (POKEPVP_POST_MATCH_MAX_OPP_TAG
     * =16) + EOS (1) = 46 bytes. The previous `u8 buf[25]` was a real,
     * live-reproduced ARM stack-buffer overflow -- every StringCopy below
     * writes unconditionally before the 24-tile display clip further down
     * even looks at how much was written, so any real match whose opponent
     * name+tag together push past 25 bytes (routine: this project's own
     * guest tags alone routinely fill all 16 oppTag bytes) smashes this
     * frame's saved return address. Found live from two real launcher
     * logs (both instances of the same real gateway match) that showed
     * mGBA's core executing garbage ("Jumped to invalid address" /
     * "Illegal opcode") immediately after this screen's own
     * "post-match session accepted (outcome=%d oppNameLen=16
     * oppTagLen=16)" debug line -- the exact black-screen-after-a-real-
     * match report ADR-195/196 thought they'd already closed (those two
     * fixed a real parse bug and a real race, but neither was this). 48
     * bytes gives a little headroom over the proven 46-byte worst case. */
    u8 buf[48];
    u8 *dst;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(10));
    if (!PokePvPPostMatch_Get(&pm))
    {
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, sTextColor1, -1, sString_Dummy);
        PutWindowTilemap(windowId);
        return;
    }
    dst = buf;
    switch (pm.outcome)
    {
    case POKEPVP_POST_MATCH_WIN:
        dst = StringCopy(dst, sText_PostMatchWin);
        break;
    case POKEPVP_POST_MATCH_LOSS:
        dst = StringCopy(dst, sText_PostMatchLose);
        break;
    default:
        dst = StringCopy(dst, sText_PostMatchDraw);
        break;
    }
    if (pm.oppName[0] != EOS)
    {
        /* ADR-226: the opponent's tag is no longer shown to anyone but
         * its own owner (PROFILE/PLAYER SETTINGS) -- pm.oppTag stays on
         * the wire (post_match.c's own parser is a proven, fragile
         * floating-offset format, ADR-196/198; not touched here) but is
         * deliberately never read for display anymore. */
        dst = StringCopy(dst, sText_PostMatchVs);
        dst = StringCopy(dst, pm.oppName);
        *dst = EOS;
    }
    /* The window is 24 tiles wide; a wrapped second line would be cut off
     * by the 16px-tall row (ADR-098) -- clip instead. */
    if ((u32)(dst - buf) > 24u)
        buf[23] = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
    PutWindowTilemap(windowId);
}

/* "1m05s 14T 3-2" -- duration, turn count, and the remaining-Pokemon
 * scoreline the Build Plan §14 post-match set asks for, drawn as a plain
 * line in the ERROR window band (no box; PrintMessageOnWindow4's own
 * wait states overwrite it with their messages). */
static void DrawPostMatchMeta(void)
{
    PokePvPPostMatch pm;
    u8 buf[32];
    u8 *dst;

    FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
    if (PokePvPPostMatch_Get(&pm))
    {
        dst = buf;
        dst = ConvertIntToDecimalStringN(dst, pm.durationSec / 60, STR_CONV_MODE_LEFT_ALIGN, 1);
        *dst++ = (u8)'m';
        dst = ConvertIntToDecimalStringN(dst, pm.durationSec % 60, STR_CONV_MODE_LEADING_ZEROS, 2);
        *dst++ = (u8)'s';
        *dst++ = CHAR_SPACE;
        dst = ConvertIntToDecimalStringN(dst, pm.turnCount, STR_CONV_MODE_LEFT_ALIGN, 1);
        *dst++ = (u8)'T';
        *dst++ = CHAR_SPACE;
        dst = ConvertIntToDecimalStringN(dst, pm.myRemaining, STR_CONV_MODE_LEFT_ALIGN, 1);
        *dst++ = (u8)'-';
        dst = ConvertIntToDecimalStringN(dst, pm.oppRemaining, STR_CONV_MODE_LEFT_ALIGN, 1);
        *dst = EOS;
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 1, 2, sTextColor1, -1, buf);
    }
    PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
    CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_GFX);
}

static void DrawPostMatchItems(u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    const u8 *const sLabels[] = {
        sText_Rematch, sText_PlayAgain, sText_AddRival, sText_Exit, sString_Dummy,
    };
    u8 i;

    DrawPostMatchResultLine(sWindowIds[0]);
    // POKEPVP (ADR-199): this loop used to run i=0..4 with `sLabels[i]`
    // directly, which is off by one against window[0]'s own real content
    // (DrawPostMatchResultLine's "YOU WIN vs NAME" line, drawn just above)
    // and against the `selected` test below it (`(i - 1) == selectedIdx`,
    // which already correctly says "window i is cursor position i-1" --
    // only the label index disagreed with it). Two real, owner-reported
    // symptoms, one cause: (1) window[0] got immediately overwritten with
    // plain "REMATCH" text instead of showing the match result, and (2)
    // every highlighted row showed the *next* action's label, not the one
    // `tSubCursorPos`/`SendPostMatchAction` actually target -- so pressing
    // A on what visibly reads "ADD RIVAL" (highlighted at window 3, whose
    // real cursor value is 1) actually sent action 1 (PLAY AGAIN), not 2
    // (ADD RIVAL): a fresh matchmaking join, ready-check prompt included,
    // is exactly the "rematch happened anyway... prompted ready on rival
    // add" report. `handle_post_match_action` (launcher) and this file's
    // own `Task_PokePvPPostMatch` switch already dispatch on the raw
    // cursor value correctly -- only the *display* was wrong. Fixed by
    // leaving window[0] alone here (its content is DrawPostMatchResultLine's)
    // and indexing the label for window i (i=1..4) as sLabels[i-1], the
    // same offset the highlight test already uses.
    for (i = 1; i < 5; i++)
    {
        bool8 selected = ((i - 1) == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
            selected ? sTextColorSelected : sTextColor1, -1, sLabels[i - 1]);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 5; i++)
        PutWindowTilemap(sWindowIds[i]);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
    // POKEPVP (ADR-188): this screen only ever populates 5 rows, but the
    // shared panel border now spans 6 (window 5's rows, freed by
    // ADR-186) -- blank it too, so leftover LEADERBOARD text from the
    // top menu never bleeds through (same "erase, don't just occlude"
    // discipline DrawStartMatchSubmenuItems already used for its own
    // unused rows).
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void ReturnToPostMatchScreen(u8 taskId)
{
    ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
    DrawPostMatchItems(gTasks[taskId].tSubCursorPos);
    DrawPostMatchMeta();
    gTasks[taskId].tMGErrorMsgState = 1;
    gTasks[taskId].func = Task_PokePvPPostMatch;
}

static void ShowPostMatchResultMessage(u8 result)
{
    const u8 *msg;

    switch (result)
    {
    case POKEPVP_POST_MATCH_RESULT_OK:
        msg = sText_RivalAdded;
        break;
    case POKEPVP_POST_MATCH_RESULT_OPPONENT_UNAVAILABLE:
        msg = sText_OpponentUnavailable;
        break;
    case POKEPVP_POST_MATCH_RESULT_RATE_LIMITED:
        msg = sText_TooManyRequests;
        break;
    case POKEPVP_POST_MATCH_RESULT_DECLINED:
        /* ADR-207 (HANDOFF item 7): reuses the same distinct string
         * Task_PokePvPNoOpponentFound shows for a declined fresh INVITE --
         * one real-world event ("the target said no"), one honest message,
         * regardless of which wait screen it's shown from. */
        msg = sText_ChallengeDeclined;
        break;
    case POKEPVP_POST_MATCH_RESULT_BUSY:
        /* ADR-242: same channel-reuse precedent as DECLINED just above,
         * for the gateway's own opponent_busy rejection on a REMATCH/
         * PLAY AGAIN target. */
        msg = sText_OpponentBusy;
        break;
    default:
        msg = sText_RequestFailed;
        break;
    }
    PrintMessageOnWindow4(msg);
}

static void Task_PokePvPPostMatch(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        /* First entry (from the menu-init chain): draw everything, then
         * fade in exactly like Task_WaitDma3AndFadeIn does for the top
         * menu (incl. the temp-tile-buffer release that prevents the
         * ADR-158 backdrop leak on repeated inits). */
        DrawPostMatchItems(0);
        DrawPostMatchMeta();
        FreeTempTileDataBuffersIfPossible();
        ResetTempTileDataBuffers();
        gTasks[taskId].tSubCursorPos = 0;
        ShowBg(0);
            SetVBlankCallback(VBlankCB_MainMenu);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (gPaletteFade.active)
            return;
        MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            SendPostMatchAction((u8)gTasks[taskId].tSubCursorPos);
            if (gTasks[taskId].tSubCursorPos == POKEPVP_POST_MATCH_ACTION_EXIT)
            {
                PokePvPPostMatch_Clear();
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
                gTasks[taskId].func = Task_PokePvPReturnToTopMenuFromPostMatch;
            }
            else if (gTasks[taskId].tSubCursorPos == POKEPVP_POST_MATCH_ACTION_ADD_RIVAL)
            {
                gTasks[taskId].tMGErrorMsgState = 0;
                gTasks[taskId].func = Task_PokePvPPostMatchRivalWait;
            }
            else
            {
                gTasks[taskId].tMGErrorMsgState = 0;
                gTasks[taskId].func = Task_PokePvPPostMatchWait;
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            /* B = EXIT, same as the menu item (Build Plan §14's "Exit"). */
            PlaySE(SE_SELECT);
            SendPostMatchAction(POKEPVP_POST_MATCH_ACTION_EXIT);
            PokePvPPostMatch_Clear();
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            gTasks[taskId].func = Task_PokePvPReturnToTopMenuFromPostMatch;
        }
        else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
        {
            gTasks[taskId].tSubCursorPos--;
            DrawPostMatchItems(gTasks[taskId].tSubCursorPos);
        }
        else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 3)
        {
            gTasks[taskId].tSubCursorPos++;
            DrawPostMatchItems(gTasks[taskId].tSubCursorPos);
        }
        break;
    }
}

/* REMATCH / PLAY AGAIN: the host is joining matchmaking (a rematch
 * request or a fresh queue/invite join). Mirrors
 * Task_PokePvPWaitForRealOpponent's shape -- same waiting text, same
 * real-opponent-ready completion into StartPokePvPAutoMatch -- but a
 * host result message (opponent unavailable / rate-limited / failed)
 * and the 30s timeout both return to the post-match screen instead of
 * the START MATCH submenu, since the player came from a match, not
 * matchmaking. The post-match session is cleared only when a new battle
 * actually starts (the launcher re-pushes it on the next matchEnd). */
static void Task_PokePvPPostMatchWait(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        PrintMessageOnWindow4(gText_PokePvPWaitingForOpponent);
        gTasks[taskId].tWaitFrames = 0;
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
    {
        u8 result;

        if (PokePvPPostMatch_ConsumeResult(&result))
        {
            if (result == POKEPVP_POST_MATCH_RESULT_OK)
            {
                ReturnToPostMatchScreen(taskId);
            }
            else
            {
                ShowPostMatchResultMessage(result);
                gTasks[taskId].tMGErrorMsgState = 3;
            }
            break;
        }
        /* POKEPVP (UI plan slice 3): the ready-check prompt, same hook
         * as the AUTO-MATCH wait. A cancel returns to the post-match
         * screen. */
        {
            bool8 ready = PokePvP_IsRealOpponentReady();

            if (!ready && TickReadyCheckPrompt(taskId))
            {
                if (PokePvP_IsReadyCheckCancelled())
                {
                    PokePvP_ClearRealMatchPending();
                    ReturnToPostMatchScreen(taskId);
                }
                break;
            }

            // POKEPVP (owner playtest, 2026-09-12; real wire message added
            // HANDOFF items 22/38): same gap
            // Task_PokePvPWaitForRealOpponent's own plain-wait cancel
            // closed for AUTO-MATCH (ADR-192) but this screen's earlier,
            // plain "Waiting for opponent..." state -- reached before any
            // real ready-check has arrived, for a rematch/PLAY AGAIN
            // requeue exactly like AUTO-MATCH's own queue join -- was
            // never given the same fix (HANDOFF item 22 named this
            // explicitly as an out-of-scope follow-up). A player who
            // requeued and changed their mind had no way out except the
            // full 1800-frame (30s) timeout. Same shape as
            // Task_PokePvPWaitForRealOpponent's own sibling fix: clears
            // the pending real match locally, returns to the post-match
            // screen, and now also emits POKEPVP_MSG_LEAVE_QUEUE.
            if (!ready && JOY_NEW(B_BUTTON))
            {
                PlaySE(SE_SELECT);
                SendLeaveQueue();
                PokePvP_ClearRealMatchPending();
                ReturnToPostMatchScreen(taskId);
                break;
            }
        }
        gTasks[taskId].tWaitFrames++;
        if (PokePvP_IsRealOpponentReady())
        {
            DebugPrintf("POKEPVP: post-match wait resolved by real opponent after %d frames", gTasks[taskId].tWaitFrames);
            PokePvP_ClearRealMatchPending();
            PokePvPPostMatch_Clear();
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            StartPokePvPAutoMatch();
        }
        else if (gTasks[taskId].tWaitFrames >= POKEPVP_AUTO_MATCH_WAIT_FRAMES)
        {
            DebugPrintf("POKEPVP: post-match wait found no opponent in %d frames", gTasks[taskId].tWaitFrames);
            PokePvP_ClearRealMatchPending();
            ReturnToPostMatchScreen(taskId);
        }
        break;
    }
    case 3:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 4:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ReturnToPostMatchScreen(taskId);
        }
        break;
    }
}

/* ADD RIVAL: the host is doing an API round trip against the rivals
 * endpoint. Same message/dismiss shape as the match wait's result path;
 * a hard 10s backstop rather than an infinite wait, though the host
 * always answers. */
static void Task_PokePvPPostMatchRivalWait(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        PrintMessageOnWindow4(sText_AddingRival);
        gTasks[taskId].tWaitFrames = 0;
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
    {
        u8 result;

        gTasks[taskId].tWaitFrames++;
        if (PokePvPPostMatch_ConsumeResult(&result))
        {
            ShowPostMatchResultMessage(result); /* OK -> "Rival added." */
            gTasks[taskId].tMGErrorMsgState = 3;
        }
        else if (gTasks[taskId].tWaitFrames >= POKEPVP_POST_MATCH_RESULT_WAIT_FRAMES)
        {
            ShowPostMatchResultMessage(POKEPVP_POST_MATCH_RESULT_FAILED);
            gTasks[taskId].tMGErrorMsgState = 3;
        }
        break;
    }
    case 3:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 4:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ReturnToPostMatchScreen(taskId);
        }
        break;
    }
}

/* EXIT (or B): fade-out already ran; redraw the top-level menu and hand
 * back to selection, same as the history/submenu return tasks. */
static void Task_PokePvPReturnToTopMenuFromPostMatch(u8 taskId)
{
    if (gPaletteFade.active)
        return;
    DrawPokePvPMenuItems(0);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tCursorPos = 0;
    gTasks[taskId].func = Task_UpdateVisualSelection;
}

// ---------------------------------------------------------------------------
// POKEPVP (ADR-238, owner-directed restructure, 2026-09-19): PROFILE is a
// plain 2-row action selector now -- SOCIAL and PLAYER -- not a stats
// display. The owner's own read: a permanent tag/stats/top-species/
// recent-opponent readout doesn't belong mixed into a plain navigation
// menu, and belongs with the rest of a player's match record instead --
// see Task_PokePvPMatchHistory's own doc comment for where it moved.
// PLAYER is new here: SOCIAL's old 4th "PLAYER" row (ADR-186/209) is gone
// (see Task_PokePvPSocial's own doc comment) -- sprite and name are now
// two fully independent actions under this row's own PLAYER submenu
// (Task_PokePvPPlayerMenu), not one row whose confirm silently chained
// into the naming screen right after picking a sprite. Same 2-row
// selector shape as Task_PokePvPPlayerMenu itself and every other small
// picker in this file (DrawInviteTargetTypeItems, etc).
// ---------------------------------------------------------------------------

static void DrawProfileItems(u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    const u8 *const sLabels[] = { sText_ProfileSocialRow, sText_ProfilePlayerRow };
    u8 i;

    for (i = 0; i < 5; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (i < 2)
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sLabels[i]);
        PutWindowTilemap(sWindowIds[i]);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
    // This screen no longer uses the ERROR band (ADR-238 moved all of
    // PROFILE's old stat content to MATCH HISTORY) -- blank it explicitly
    // rather than trusting an earlier screen's leftover content to
    // already be gone (same "erase, don't occlude" discipline every
    // sibling screen uses). Done *before* the window-5 identity tag below
    // -- alpha-build-plan P6 (docs/adr/264, real bug found and fixed same
    // session): MAIN_MENU_WINDOW_ERROR spans rows 15-18 (tilemapTop=15,
    // height=4), which fully overlaps window 5's own rows 16-17 --
    // MainMenu_EraseWindow blank-fills its *entire* rect, tilemap and
    // all, not just its border. Drawing the tag first and erasing ERROR
    // after (the order this function had before this ADR) silently wiped
    // the tag back out every time, confirmed via live headless capture
    // showing nothing at all where the tag should be -- not a palette or
    // timing issue, a genuine same-frame overwrite.
    ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void Task_PokePvPProfile(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (gTasks[taskId].tSubCursorPos == 0)
        {
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPSocial;
            DrawSocialItems(0);
        }
        else
        {
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPPlayerMenu;
            DrawPlayerMenuItems(0);
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].func = Task_PokePvPReturnToTopMenuFromHistory;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawProfileItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawProfileItems(gTasks[taskId].tSubCursorPos);
    }
}

// POKEPVP (ADR-238): the PLAYER submenu -- SPRITE and NAME as two fully
// independent actions, reached from PROFILE's own PLAYER row. Same 2-row
// selector shape as DrawProfileItems just above.
static void DrawPlayerMenuItems(u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    const u8 *const sLabels[] = { sText_PlayerMenuSprite, sText_PlayerMenuName };
    u8 i;

    for (i = 0; i < 5; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (i < 2)
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sLabels[i]);
        PutWindowTilemap(sWindowIds[i]);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void Task_PokePvPPlayerMenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (gTasks[taskId].tSubCursorPos == 0)
        {
            // SPRITE: same trainer-sprite picker as before (ADR-209),
            // but its own confirm no longer chains into the naming
            // screen -- see Task_PokePvPSpritePicker's own A-button
            // branch. Explicit tMGErrorMsgState reset before the handoff
            // (ADR-214's own lesson: this task struct is reused all
            // session, so a stale nonzero value left by an earlier
            // screen would skip the picker's own case 0 setup entirely).
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPSpritePicker;
        }
        else
        {
            // NAME: DoNamingScreen tears down every task itself
            // (EnterPokeStorage-style) -- this screen's own window
            // buffers/task must be freed first, same as every other
            // DoNamingScreen call site in this file (ADR-233's own
            // ADD FRIEND fix named this exact class of gap when it was
            // missing at that call site; built correctly here from the
            // start). sPokePvPReturnToPlayerMenu (ADR-233's own
            // sPokePvPReturnToSocialFriends shape) lands the player back
            // on this submenu directly afterward instead of the top menu.
            sPokePvPReturnToPlayerMenu = TRUE;
            gExitStairsMovementDisabled = FALSE;
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            DoNamingScreen(NAMING_SCREEN_PLAYER, gSaveBlock2Ptr->playerName, gSaveBlock2Ptr->playerGender, 0, 0, CB2_InitMainMenu);
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        gTasks[taskId].tSubCursorPos = 1;
        gTasks[taskId].func = Task_PokePvPProfile;
        DrawProfileItems(1);
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawPlayerMenuItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawPlayerMenuItems(gTasks[taskId].tSubCursorPos);
    }
}

// ---------------------------------------------------------------------------
// POKEPVP (UI plan slice 6, Phase G): SOCIAL screens.
//
// Reached from the PROFILE screen's SOCIAL row. The social screen lists
// FRIENDS / RIVALS / BLOCKS -- PLAYER (name/sprite) moved out to its own
// submenu under PROFILE, ADR-238. Each list screen shows up to 4 rows
// (name + tag, plus a presence letter for friends) and a per-row action
// menu (CHALLENGE / REMOVE, or UNBLOCK for the blocks list). CHALLENGE
// hands to the team selector (subMode POKEPVP_MATCH_MODE_CHALLENGE_TARGET,
// list + row kept in tPickerClass/tInboxSlot); the selector's A sends
// the team records + a SOCIAL_ACTION challenge. REMOVE/UNBLOCK send
// SOCIAL_ACTION immediately. B walks back: list -> social -> profile ->
// top menu.
// ---------------------------------------------------------------------------

// tSocialList (data[15]) is now #defined near tPickerStage/tPickerClass/
// tInboxSlot up top -- ADR-193/Gap 2's Task_PokePvPInviteTargetPicker
// needs it before this point in the file.

static void SendSocialAction(u8 list, u8 action, u8 index)
{
    u8 payload[3];

    payload[0] = list;
    payload[1] = action;
    payload[2] = index;
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_SOCIAL_ACTION,
                                0,
                                list,
                                payload,
                                sizeof(payload));
    DebugPrintf("POKEPVP: social action list=%u action=%u index=%u sent", list, action, index);
}

static void DrawSocialItems(u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    // POKEPVP (ADR-238): PLAYER (the old 4th row, ADR-186/209) moved out
    // to its own submenu under PROFILE (Task_PokePvPPlayerMenu) -- this
    // screen is FRIENDS/RIVALS/BLOCKS only now, rows 3-4 always blank.
    const u8 *const sLabels[] = { sText_Friends, sText_Rivals, sText_Blocks };
    u8 i;

    for (i = 0; i < 5; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (i < 3)
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sLabels[i]);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 5; i++)
        PutWindowTilemap(sWindowIds[i]);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
    // POKEPVP (ADR-188): this screen only ever populates 5 rows, but the
    // shared panel border now spans 6 (window 5's rows, freed by
    // ADR-186) -- blank it too, so leftover LEADERBOARD text from the
    // top menu never bleeds through (same "erase, don't just occlude"
    // discipline DrawStartMatchSubmenuItems already used for its own
    // unused rows).
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

// POKEPVP (ADR-209, reached from the PLAYER submenu's own SPRITE row as
// of ADR-238 -- no longer chains into the naming screen on confirm, see
// this task's own A-button branch below): the trainer-sprite picker.
// Only 4 classes are offered -- see that ADR's own table for why (only
// Red/Leaf/RS Brendan/RS May have both a real front-pic, for the
// opponent's own view and this preview, and a real back-pic, for this
// player's own in-battle view; every other FRLG trainer-class front-pic
// is opponent-only art).
static const u16 sPokePvPSpriteFrontPicIds[4] = {
    TRAINER_PIC_RED, TRAINER_PIC_LEAF, TRAINER_PIC_RS_BRENDAN_1, TRAINER_PIC_RS_MAY_1,
};
static const u8 sText_SpriteRed[] = _("RED");
static const u8 sText_SpriteLeaf[] = _("LEAF");
static const u8 sText_SpriteBrendan[] = _("BRENDAN");
static const u8 sText_SpriteMay[] = _("MAY");
static const u8 *const sPokePvPSpriteLabels[4] = {
    sText_SpriteRed, sText_SpriteLeaf, sText_SpriteBrendan, sText_SpriteMay,
};
static const u8 sText_SpritePickerPrompt[] = _("<>=CHANGE A=OK B=CANCEL");

#define POKEPVP_SPRITE_PICKER_PAL_SLOT 6
#define POKEPVP_SPRITE_PICKER_X 120
#define POKEPVP_SPRITE_PICKER_Y 60

// POKEPVP (item 41, second real bug found this session): every other
// PrintMessageOnWindow4 caller in this file passes a static/const string
// (a literal or a switch-selected `sText_*` constant); this is the only
// one that builds a dynamic message. AddTextPrinterParameterized3's
// speed=2 scrolling printer (see RunTextPrinters's own doc comment on
// this screen's case 1) keeps reading from the pointer it was given
// across later frames, not just the frame it was called on -- a local
// stack buffer is invalid by the time those later frames run, so once
// RunTextPrinters() actually started pumping this screen's printer (the
// first bug this session, fixed above), the glyphs it read back were
// whatever now occupied that stack slot, not this string. Static instead
// of local: only one such message is ever showing on this screen at a
// time, so there is no aliasing risk in making it live for the screen's
// whole lifetime instead of one call's stack frame.
static u8 sPokePvPSpritePickerPreviewBuf[48];

// POKEPVP (item 56, real root cause found): every other PokePvP screen
// entry leaves MAIN_MENU_WINDOW_POKEPVP_0..3 showing whatever the
// *previous* screen last drew into them (DrawPlayerMenuItems' own
// SPRITE/NAME rows when reached from the current PROFILE->PLAYER flow) --
// every other Task_PokePvP* screen overwrites those same windows itself on
// entry, but this screen never did, since its own content is the OBJ
// trainer pic, not a text list. Blanked here, once, so the stale list text
// doesn't stay on screen behind (and, before the WININ/WINOUT fix just
// below, in place of) the trainer preview.
static void ClearSpritePickerContentArea(void)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
    };
    u8 i;

    for (i = 0; i < 4; i++)
    {
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(10));
        PutWindowTilemap(sWindowIds[i]);
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
}

// POKEPVP (item 56, real root cause found): this file's shared WIN0
// curtain-reveal setup (MoveWindowByMenuTypeAndCursorPos's own callers,
// e.g. WININ=0x0001/WINOUT=0x0021 at this file's screen-entry points)
// never includes the OBJ bit (0x10) in either register -- fine for every
// other PokePvP screen, since none of them ever show a real OBJ sprite,
// but this is the one screen that does (CreateTrainerPicSprite). With
// OBJ excluded from *both* "inside window 0" and "outside window 0"
// visibility, the trainer preview sprite is suppressed everywhere on
// screen, unconditionally -- not a heap/decompression bug (ADR-237's own
// leading hypothesis): the sprite is created successfully (a real,
// valid sprite id, confirmed via ADR-237's own diagnostic) and simply
// never has a path to be drawn while WIN0 is active. Confirmed live via
// a headless repro dumping DISPCNT (0x3140 -- WIN0+OBJ both nominally
// on) and pixel-scanning the captured frame: zero trainer-pic-colored
// pixels anywhere, not a partial WIN0-band clip, consistent with OBJ
// being masked out of both WININ and WINOUT rather than just one band.
// Scoped to this screen only (not a blanket file-wide change) since
// altering the shared constant risks the vanilla CONTINUE/NEW GAME/
// MYSTERY GIFT curtain transition, unverified and out of scope here.
static void EnableSpritePickerObjWindow(void)
{
    SetGpuReg(REG_OFFSET_WININ, 0x0011);  // BG0 + OBJ inside win0
    SetGpuReg(REG_OFFSET_WINOUT, 0x0031); // BG0 + OBJ + color-effect outside win0
}

static void RestorePokePvPStandardWindow(void)
{
    SetGpuReg(REG_OFFSET_WININ, 0x0001);
    SetGpuReg(REG_OFFSET_WINOUT, 0x0021);
}

static void DrawSpritePickerPreview(u8 taskId, u8 index)
{
    /* ADR-198's own lesson applied up front: sized to the real worst case
     * (longest label "BRENDAN" = 7 + 2 spaces + the prompt string's own
     * 24 chars + EOS = 34), not guessed short. */
    u8 *buf = sPokePvPSpritePickerPreviewBuf;
    u8 *ptr;

    if (gTasks[taskId].data[4] != -1)
        FreeAndDestroyTrainerPicSprite(gTasks[taskId].data[4]);
    gTasks[taskId].data[4] = CreateTrainerPicSprite(sPokePvPSpriteFrontPicIds[index], TRUE,
        POKEPVP_SPRITE_PICKER_X, POKEPVP_SPRITE_PICKER_Y, POKEPVP_SPRITE_PICKER_PAL_SLOT, TAG_NONE);
    /* POKEPVP (item 40/41 diagnostic): CreateTrainerPicSprite/CreatePicSprite
     * already fail safe on a heap allocation miss (returns 0xFFFF, checked
     * internally -- see trainer_pokemon_sprites.c), so this can't itself be
     * the reported "PLAYER row does nothing" freeze, but this screen's own
     * ~8KB decompress-buffer alloc on every keypress, on this project's own
     * chronically tight heap (ADR-158/ADR-214 item 40 already proved a real
     * boot burst can exhaust it), was never once logged either way. */
    if (gTasks[taskId].data[4] == -1)
        DebugPrintf("POKEPVP: sprite picker preview alloc FAILED for class=%d (heap pressure)", index);

    ptr = StringCopy(buf, sPokePvPSpriteLabels[index]);
    *ptr++ = CHAR_SPACE;
    *ptr++ = CHAR_SPACE;
    StringCopy(ptr, sText_SpritePickerPrompt);
    PrintMessageOnWindow4(buf);
}

static void Task_PokePvPSpritePicker(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
    {
        PokePvPProfile profile;
        u8 startIndex = 0;

        /* POKEPVP (item 40/41, ADR-214/215): this screen's own entry was
         * never logged, which is exactly why the real crash log that
         * session couldn't say whether Team Builder or this picker was
         * open when it hit -- same gap item 32's own permanent diagnostic
         * printf was added to close for a different elusive bug. Left in
         * place, not a one-shot TEMP hack, until a real playtest's log
         * confirms this screen is (or isn't) actually being reached on a
         * PLAYER-row press. `taskId` added (this session) to correlate
         * against the new dispatch-site log in Task_PokePvPSocial's own
         * NAME-row branch -- see that log's own doc comment. */
        DebugPrintf("POKEPVP: sprite picker opened taskId=%d vblank=%d", taskId, gMain.vblankCounter2);
        ResetAllPicSprites();
        if (PokePvPProfile_Get(&profile) && profile.spriteId < 4)
            startIndex = profile.spriteId;
        gTasks[taskId].tSubCursorPos = startIndex;
        gTasks[taskId].data[4] = -1; /* no preview sprite yet -- DrawSpritePickerPreview's own free-guard */
        ClearSpritePickerContentArea();
        EnableSpritePickerObjWindow();
        DrawSpritePickerPreview(taskId, startIndex);
        gTasks[taskId].tMGErrorMsgState = 1;
        break;
    }
    case 1:
        // POKEPVP (item 41, real root cause found this session): this
        // screen's own case 0 (and every DPAD_LEFT/RIGHT re-draw below)
        // calls PrintMessageOnWindow4, which starts a real scrolling
        // text-printer job (speed=2, not an instant blit) -- exactly the
        // already-diagnosed ADR-191 bug class ("PrintMessageOnWindow4
        // starts a real text-printer job... every OTHER caller in this
        // file follows it with its own case that polls RunTextPrinters()
        // every frame until IsTextPrinterActive() clears"). This screen
        // was the one caller that never did, confirmed live this session
        // via a deterministic headless repro (ADR-236): the window was
        // never visibly touched at all, because the queued text glyphs
        // never actually got drawn into its pixel buffer. Same fix shape
        // as TickReadyCheckPrompt's own identical bug (line ~3009):
        // called unconditionally every frame this case runs, not split
        // into a separate waiting sub-state, since this screen (unlike a
        // one-shot dismiss prompt) needs to keep accepting D-pad input
        // the whole time regardless of whether the printer has caught up.
        RunTextPrinters();
        if (JOY_NEW(DPAD_LEFT))
        {
            PlaySE(SE_SELECT);
            gTasks[taskId].tSubCursorPos = (gTasks[taskId].tSubCursorPos + 3) % 4;
            DrawSpritePickerPreview(taskId, gTasks[taskId].tSubCursorPos);
        }
        else if (JOY_NEW(DPAD_RIGHT))
        {
            PlaySE(SE_SELECT);
            gTasks[taskId].tSubCursorPos = (gTasks[taskId].tSubCursorPos + 1) % 4;
            DrawSpritePickerPreview(taskId, gTasks[taskId].tSubCursorPos);
        }
        else if (JOY_NEW(A_BUTTON))
        {
            u8 index = gTasks[taskId].tSubCursorPos;

            PlaySE(SE_SELECT);
            FreeAndDestroyTrainerPicSprite(gTasks[taskId].data[4]);
            /* ADR-209: local effect first (own PROFILE render, and for
             * Red/Leaf -- the only 2 of the 4 with a real vanilla
             * gender-driven back-pic lookup, see that ADR's own honest
             * gap note for Brendan/May -- the actual in-battle sprite),
             * then the real wire send, same "ROM-local first, best-
             * effort network write second" shape SendLeaveQueue/
             * SendReadyChoice already use. */
            PokePvPProfile_SetSpriteId(index);
            if (index == 0)
                gSaveBlock2Ptr->playerGender = MALE;
            else if (index == 1)
                gSaveBlock2Ptr->playerGender = FEMALE;
            SendSetSprite(index);
            // POKEPVP (ADR-238, owner-directed restructure): confirming a
            // sprite used to chain straight into the naming screen --
            // the owner's own ask was to make SPRITE and NAME fully
            // independent actions, so this now just returns to the
            // PLAYER submenu (same CB2, no DoNamingScreen/teardown
            // needed) instead.
            RestorePokePvPStandardWindow();
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPPlayerMenu;
            DrawPlayerMenuItems(0);
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            FreeAndDestroyTrainerPicSprite(gTasks[taskId].data[4]);
            RestorePokePvPStandardWindow();
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPPlayerMenu;
            DrawPlayerMenuItems(0);
        }
        break;
    }
}

static void Task_PokePvPSocial(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        // POKEPVP (ADR-238): the old index-3 NAME branch (item 41's own
        // long history: ADR-186/209/214/215/218/228/232/234/235/236/237)
        // is gone -- PLAYER moved out to its own submenu under PROFILE
        // (Task_PokePvPPlayerMenu), reached independently of this list.
        // This screen is FRIENDS(0)/RIVALS(1)/BLOCKS(2) only now.
        gTasks[taskId].tSocialList = gTasks[taskId].tSubCursorPos;
        gTasks[taskId].tSubCursorPos = 0;
        DrawSocialListItems(gTasks[taskId].tSocialList, 0, TRUE);

        // POKEPVP (ADR-217, most of this moved into the shared
        // ShowAddFriendResultIfPending by ADR-233): only checked for
        // list == 0 (FRIENDS) -- RIVALS/BLOCKS can never have a
        // pending result of their own. ADR-233 also added a direct
        // return path (sPokePvPReturnToSocialFriends) that shows this
        // same result immediately after the naming screen closes,
        // instead of only the next time this branch runs -- see that
        // flag's own doc comment.
        if (gTasks[taskId].tSocialList == 0)
            ShowAddFriendResultIfPending(taskId);
        else
            gTasks[taskId].func = Task_PokePvPSocialList;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        // POKEPVP (ADR-238): now that PROFILE is a real, navigable 2-row
        // menu (not a single fixed pass-through row), B walks back to it
        // instead of jumping straight to the top menu -- restores the
        // "list -> social -> profile -> top menu" chain this file's own
        // SOCIAL doc comment always described but never actually built.
        PlaySE(SE_SELECT);
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPProfile;
        DrawProfileItems(0);
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawSocialItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 2)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawSocialItems(gTasks[taskId].tSubCursorPos);
    }
}

/* One row of a social list: name (+ presence letter for friends and
 * rivals -- ADR-200; blocks never get one). ADR-226: no longer shows
 * entry.tag -- tags are internal-only now, visible solely on their own
 * owner's PROFILE/PLAYER SETTINGS screen; the field still rides
 * PokePvPSocialEntry (used internally by REMOVE's own tag-keyed lookup)
 * but is deliberately never read for display anymore. */
static void DrawSocialListRow(u8 windowId, u8 list, u8 index, bool8 selected)
{
    PokePvPSocialEntry entry;
    /* POKEPVP (ADR-198): same class of stack-buffer overflow as
     * DrawPostMatchResultLine's own fix (see that function's own doc
     * comment for the live-reproduced crash this pattern causes) --
     * `entry.name` + space + a presence letter + EOS can legitimately be
     * a full POKEPVP_SOCIAL_MAX_NAME_LEN (16) + 2 bytes; buf kept at its
     * existing size (ADR-226 only shrank what's written, not the bound). */
    u8 buf[40];
    u8 *dst;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(selected ? 13 : 10));
    if (!PokePvPSocial_Get(list, index, &entry))
    {
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2,
            selected ? sTextColorSelected : sTextColor1, -1, sText_Empty);
        PutWindowTilemap(windowId);
        return;
    }
    dst = StringCopy(buf, entry.name);
    if (list == 0 || list == 1) /* ADR-200: friends and rivals both get presence */
    {
        *dst++ = CHAR_SPACE;
        switch (entry.presence)
        {
        case POKEPVP_PRESENCE_ONLINE:
            dst = StringCopy(dst, sText_PresenceOnline);
            break;
        case POKEPVP_PRESENCE_SEARCHING:
            dst = StringCopy(dst, sText_PresenceSearching);
            break;
        case POKEPVP_PRESENCE_IN_BATTLE:
            dst = StringCopy(dst, sText_PresenceBattle);
            break;
        case POKEPVP_PRESENCE_BUSY:
            dst = StringCopy(dst, sText_PresenceBusy);
            break;
        default:
            // POKEPVP (item: RIVALS row shows blank/garbled text, found
            // this session): every other branch of this switch
            // null-terminates buf via StringCopy's own return value;
            // this is the one branch that doesn't -- and it's the
            // *common* case, since a friend/rival starts and usually
            // stays POKEPVP_PRESENCE_OFFLINE (0) until a real PRESENCE
            // push arrives (ADR-200), which lands here every time. With
            // no EOS written after '-', AddTextPrinterParameterized3
            // read past the end of this stack buffer into whatever
            // garbage happened to follow it -- explaining both reported
            // symptoms: blank on the first draw (leftover near-zero
            // stack content from a shallow call depth) and garbled
            // glyphs on a later redraw (different, non-zero stack
            // content from the calls made in between).
            //
            // Also a second, smaller bug fixed alongside (found live in
            // this same screenshot): a raw ASCII '-' (0x2D) isn't this
            // ROM's own hyphen glyph in its custom charmap -- CHAR_HYPHEN
            // (0xAE, characters.h) is. Confirmed live: the OFFLINE dash
            // rendered as '&' before this fix.
            *dst++ = CHAR_HYPHEN;
            *dst = EOS;
            break;
        }
    }
    if ((u32)(dst - buf) > 23u)
        buf[23] = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2,
        selected ? sTextColorSelected : sTextColor1, -1, buf);
    PutWindowTilemap(windowId);
}

// POKEPVP (ADR-224 follow-up): `addFriendFirst` lets FRIENDS' fixed
// "ADD FRIEND" row move to the top (row 0, real friends pushed down to
// rows 1..4) for SOCIAL's own list screen, without disturbing
// Task_PokePvPInviteTargetPicker's independent use of this same rendering
// function -- that screen never offers ADD FRIEND at all (it targets an
// existing friend/rival to challenge) and indexes rows 0..3 directly as
// real friend/rival data indices, so it always passes FALSE. Every caller
// that reorders (FALSE->TRUE) must also translate its own row<->data-index
// math to match -- see Task_PokePvPSocialList's A-button handler and the
// tInboxSlot round-trips in Task_PokePvPSocialRowMenu/RowMenuDismiss.
static void DrawSocialListItems(u8 list, u8 selectedIdx, bool8 addFriendFirst)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    bool8 reordered = (list == 0 && addFriendFirst);
    u8 i;

    for (i = 0; i < 5; i++)
    {
        // POKEPVP (ADR-217, reordered to row 0 by ADR-224 follow-up):
        // FRIENDS' "ADD FRIEND" row is not a real entry --
        // PokePvPSocial_Count(0) never exceeds POKEPVP_SOCIAL_MAX_ENTRIES
        // (4), so there is always exactly one row of the five left over
        // for it, at index 4 (unreordered) or index 0 (reordered). RIVALS/
        // BLOCKS, and this same screen when addFriendFirst is FALSE, keep
        // the plain "Empty" placeholder here, same as before.
        if (reordered && i == 0)
        {
            FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(i == selectedIdx ? 13 : 10));
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                i == selectedIdx ? sTextColorSelected : sTextColor1, -1, sText_AddFriendRow);
            PutWindowTilemap(sWindowIds[i]);
        }
        else if (!reordered && list == 0 && i == POKEPVP_SOCIAL_MAX_ENTRIES)
        {
            FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(i == selectedIdx ? 13 : 10));
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                i == selectedIdx ? sTextColorSelected : sTextColor1, -1, sText_AddFriendRow);
            PutWindowTilemap(sWindowIds[i]);
        }
        else
        {
            u8 dataIndex = reordered ? (u8)(i - 1) : i;

            if (dataIndex < PokePvPSocial_Count(list))
                DrawSocialListRow(sWindowIds[i], list, dataIndex, i == selectedIdx);
            else
            {
                FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(i == selectedIdx ? 13 : 10));
                AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                    i == selectedIdx ? sTextColorSelected : sTextColor1, -1, sText_Empty);
                PutWindowTilemap(sWindowIds[i]);
            }
        }
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
    // POKEPVP (ADR-188): this screen only ever populates 5 rows, but the
    // shared panel border now spans 6 (window 5's rows, freed by
    // ADR-186) -- blank it too, so leftover LEADERBOARD text from the
    // top menu never bleeds through (same "erase, don't just occlude"
    // discipline DrawStartMatchSubmenuItems already used for its own
    // unused rows).
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void Task_PokePvPSocialList(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        // POKEPVP (ADR-217, reordered to row 0 by ADR-224 follow-up): the
        // ADD FRIEND row, only reachable at all when tSocialList == 0
        // (FRIENDS). Checked before the "empty row" bail so it never
        // falls into that branch.
        if (gTasks[taskId].tSocialList == 0 && gTasks[taskId].tSubCursorPos == 0)
        {
            PlaySE(SE_SELECT);
            // POKEPVP (ADR-224 follow-up): show the lead-in explanation
            // before handing off to the naming screen, instead of opening
            // it immediately -- see Task_PokePvPAddFriendPrompt.
            PrintMessageOnWindow4(sText_AddFriendLeadIn);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPAddFriendPrompt;
            return;
        }
        // POKEPVP (ADR-224 follow-up): with ADD FRIEND moved to row 0 for
        // FRIENDS, every row past it is offset by one from its real data
        // index (row 1 = friend 0, ..., row 4 = friend 3) -- RIVALS/BLOCKS
        // are untouched, row == data index as before. This is the one
        // place that translation happens on the way in; tInboxSlot always
        // holds the real data index from here on (SendSocialAction's own
        // two call sites, and Task_PokePvPInviteTargetPicker's identical
        // tInboxSlot usage, both expect that and are otherwise unchanged).
        {
            u8 dataIndex = (gTasks[taskId].tSocialList == 0)
                ? (u8)(gTasks[taskId].tSubCursorPos - 1)
                : gTasks[taskId].tSubCursorPos;

            if (dataIndex >= PokePvPSocial_Count(gTasks[taskId].tSocialList))
                return; /* empty row */
            PlaySE(SE_SELECT);
            gTasks[taskId].tInboxSlot = dataIndex;
        }
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPSocialRowMenu;
        DrawSocialRowMenuItems(gTasks[taskId].tSocialList, 0);
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        gTasks[taskId].tSubCursorPos = gTasks[taskId].tSocialList;
        gTasks[taskId].func = Task_PokePvPSocial;
        DrawSocialItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawSocialListItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos, TRUE);
    }
    // POKEPVP (ADR-217, reordered by ADR-224 follow-up): FRIENDS (list 0)
    // can reach one row further than RIVALS/BLOCKS -- the fixed ADD FRIEND
    // row, never a real entry for any list, now at row 0 instead of
    // POKEPVP_SOCIAL_MAX_ENTRIES. The bound itself is unchanged (still the
    // same total row count); only which row is the pseudo-entry moved.
    else if (JOY_NEW(DPAD_DOWN) &&
             gTasks[taskId].tSubCursorPos < (gTasks[taskId].tSocialList == 0
                                              ? POKEPVP_SOCIAL_MAX_ENTRIES
                                              : POKEPVP_SOCIAL_MAX_ENTRIES - 1))
    {
        gTasks[taskId].tSubCursorPos++;
        DrawSocialListItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos, TRUE);
    }
}

/* Per-row action menu: CHALLENGE / REMOVE (or UNBLOCK for blocks). */
static void DrawSocialRowMenuItems(u8 list, u8 selectedIdx)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    const u8 *const sActions[] = { sText_Challenge, sText_Remove, sText_Unblock, sString_Dummy, sString_Dummy };
    u8 i;

    for (i = 0; i < 5; i++)
    {
        bool8 selected = (i == selectedIdx);
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (i == 0)
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sText_Challenge);
        else if (i == 1)
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, (list == 2) ? sText_Unblock : sText_Remove);
        else
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sString_Dummy);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
    // POKEPVP (ADR-188): this screen only ever populates 5 rows, but the
    // shared panel border now spans 6 (window 5's rows, freed by
    // ADR-186) -- blank it too, so leftover LEADERBOARD text from the
    // top menu never bleeds through (same "erase, don't just occlude"
    // discipline DrawStartMatchSubmenuItems already used for its own
    // unused rows).
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

static void Task_PokePvPSocialRowMenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (gTasks[taskId].tSubCursorPos == 0)
        {
            /* CHALLENGE: pick a team, then the selector's A sends the
             * team records + SOCIAL_ACTION challenge. */
            DrawTeamSelectorItems(0);
            gTasks[taskId].tSubMode = POKEPVP_MATCH_MODE_CHALLENGE_TARGET;
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPTeamSelector;
        }
        else
        {
            /* REMOVE / UNBLOCK. */
            SendSocialAction(gTasks[taskId].tSocialList, 1, gTasks[taskId].tInboxSlot);
            PrintMessageOnWindow4(sText_Removed);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPSocialRowMenuDismiss;
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        // POKEPVP (ADR-224 follow-up): tInboxSlot holds the real data
        // index (set by Task_PokePvPSocialList's own translation above);
        // FRIENDS' reordered display needs it converted back to a row
        // (+1, since row 0 is ADD FRIEND) to restore the cursor onto the
        // same entry the player opened this menu from. RIVALS/BLOCKS have
        // no offset, same as before.
        gTasks[taskId].tSubCursorPos = (gTasks[taskId].tSocialList == 0)
            ? (u8)(gTasks[taskId].tInboxSlot + 1)
            : gTasks[taskId].tInboxSlot;
        gTasks[taskId].func = Task_PokePvPSocialList;
        DrawSocialListItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos, TRUE);
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawSocialRowMenuItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawSocialRowMenuItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos);
    }
}

// POKEPVP (ADR-224 follow-up): the lead-in explanation shown before ADD
// FRIEND's naming screen opens -- same "print, wait for the printer, then
// wait for a button" shape as Task_PokePvPSocialRowMenuDismiss just above.
// A opens the naming screen (the same call this row used to make
// immediately); B cancels back to the FRIENDS list without ever opening
// it, landing back on the ADD FRIEND row itself.
static void Task_PokePvPAddFriendPrompt(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            sPokePvPAddFriendTagBuffer[0] = EOS;
            // POKEPVP: a real, pre-existing gap found while building this
            // lead-in -- DoNamingScreen tears down every task itself
            // (EnterPokeStorage-style, same as RENAME/PLAYER SETTINGS just
            // above/below), so this screen's own window buffers/task must
            // be freed first. The original ADD FRIEND handoff (ADR-217)
            // called DoNamingScreen directly from Task_PokePvPSocialList
            // with neither -- the one DoNamingScreen call site in this
            // file that skipped it. ADR-226's own HANDOFF entry already
            // flagged this exact round trip as "not live-verified through
            // the real emulator", so it was never actually exercised
            // against a live ROM. Fixed here rather than left for whoever
            // next reaches this line.
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            DoNamingScreen(NAMING_SCREEN_BOX, sPokePvPAddFriendTagBuffer, 0, 0, 0, CB2_PokePvPAddFriendNameEntered);
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            // POKEPVP (ADR-224 follow-up): row 0, not
            // POKEPVP_SOCIAL_MAX_ENTRIES -- ADD FRIEND now lives at the
            // top of the list.
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPSocialList;
            DrawSocialListItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos, TRUE);
        }
        break;
    }
}

// POKEPVP (ADR-217, extracted into a shared helper by ADR-233): shows the
// pending ADD FRIEND result (if any) and dispatches into the dismiss
// screen for it, or falls through to the ordinary FRIENDS list if nothing
// is pending. Assumes the caller has already set gTasks[taskId].tSocialList
// = 0 and drawn the FRIENDS list (DrawSocialListItems(0, ..., TRUE)) --
// shared by two call sites: Task_PokePvPSocial's own ordinary FRIENDS
// entry, and sPokePvPReturnToSocialFriends's direct post-naming-screen
// return (Task_UpdateVisualSelection), so a fresh ADD FRIEND result shows
// immediately either way, not just on the next ordinary FRIENDS visit.
static void ShowAddFriendResultIfPending(u8 taskId)
{
    if (!PokePvPSocial_IsAddFriendResultPending())
    {
        gTasks[taskId].func = Task_PokePvPSocialList;
        return;
    }
    {
        u8 result = PokePvPSocial_ConsumeAddFriendResult();
        const u8 *msg;

        switch (result)
        {
        case POKEPVP_ADD_FRIEND_RESULT_SUCCESS:
            msg = sText_AddFriendSuccess;
            break;
        case POKEPVP_ADD_FRIEND_RESULT_UNKNOWN_TAG:
            msg = sText_AddFriendUnknown;
            break;
        case POKEPVP_ADD_FRIEND_RESULT_SELF:
            msg = sText_AddFriendSelf;
            break;
        case POKEPVP_ADD_FRIEND_RESULT_BLOCKED:
            msg = sText_AddFriendBlocked;
            break;
        case POKEPVP_ADD_FRIEND_RESULT_INVALID_INPUT:
            msg = sText_AddFriendInvalid;
            break;
        default:
            msg = sText_AddFriendFailed;
            break;
        }
        PrintMessageOnWindow4(msg);
        gTasks[taskId].tMGErrorMsgState = 0;
        gTasks[taskId].func = Task_PokePvPAddFriendResultDismiss;
    }
}

// POKEPVP (ADR-217): dismiss the ADD FRIEND result message back to the
// FRIENDS list, same shape as Task_PokePvPSocialRowMenuDismiss just
// below -- always returns to row 0 (tSubCursorPos is already 0 here,
// set when Task_PokePvPSocial handed off), not tInboxSlot, since this
// isn't a per-row action. Row 0 is ADD FRIEND itself post-ADR-224-follow-
// up's reorder, which reads naturally here too -- the cursor lands right
// back on the row the player just used.
static void Task_PokePvPAddFriendResultDismiss(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            gTasks[taskId].func = Task_PokePvPSocialList;
            DrawSocialListItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos, TRUE);
        }
        break;
    }
}

/* Dismiss the REMOVE/UNBLOCK result message back to the list. */
static void Task_PokePvPSocialRowMenuDismiss(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            // POKEPVP (ADR-224 follow-up): same tInboxSlot (data index) ->
            // row translation as Task_PokePvPSocialRowMenu's own B-button,
            // above.
            gTasks[taskId].tSubCursorPos = (gTasks[taskId].tSocialList == 0)
                ? (u8)(gTasks[taskId].tInboxSlot + 1)
                : gTasks[taskId].tInboxSlot;
            gTasks[taskId].func = Task_PokePvPSocialList;
            DrawSocialListItems(gTasks[taskId].tSocialList, gTasks[taskId].tSubCursorPos, TRUE);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// POKEPVP (UI plan slice 4, Phase H): CHALLENGE INBOX.
//
// Reached from Task_HandleMenuInput while a challenge is buffered (and
// not snoozed). Window 0 shows the challenger line ("CHALLENGE FROM" +
// name + tag, clipped to the 24-tile window), the ERROR band below shows
// the challenge type (REMATCH / QUICK EARLY / QUICK ELITE), and windows
// 1-3 are ACCEPT / DECLINE / BLOCK. A on ACCEPT hands to the team
// selector (subMode POKEPVP_MATCH_MODE_ACCEPT_CHALLENGE, inbox slot kept
// in tInboxSlot); DECLINE/BLOCK send a CHALLENGE_ACTION immediately and
// return to the top menu. B snoozes (the popup re-arms on the next menu
// init -- an unanswered challenge is never dropped, but never nags the
// idle frame loop either).
// ---------------------------------------------------------------------------

static void SendInboxAction(u8 inboxSlot, u8 action, u8 teamSlot)
{
    u8 payload[3];

    payload[0] = inboxSlot;
    payload[1] = action;
    payload[2] = teamSlot; /* only meaningful for action 0 (ACCEPT) */
    PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                POKEPVP_MSG_CHALLENGE_ACTION,
                                0,
                                inboxSlot,
                                payload,
                                sizeof(payload));
    DebugPrintf("POKEPVP: inbox action slot=%u action=%u teamSlot=%u sent", inboxSlot, action, teamSlot);
}

/* Clears the message band and returns to the top menu (shared by every
 * inbox exit -- DECLINE / BLOCK dismissal / entry-vanished fallback). */
static void LeaveInboxToMenu(u8 taskId)
{
    ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
    DrawPokePvPMenuItems(0);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tCursorPos = 0;
    gTasks[taskId].func = Task_UpdateVisualSelection;
}

static void DrawInboxItems(u8 inboxSlot, u8 cursor)
{
    static const u8 sWindowIds[] = {
        MAIN_MENU_WINDOW_POKEPVP_0, MAIN_MENU_WINDOW_POKEPVP_1,
        MAIN_MENU_WINDOW_POKEPVP_2, MAIN_MENU_WINDOW_POKEPVP_3,
        MAIN_MENU_WINDOW_POKEPVP_4,
    };
    const u8 *const sActions[] = { sText_Accept, sText_Decline, sText_BlockPlayer };
    PokePvPInboxEntry entry;
    u8 i;
    /* POKEPVP (ADR-198): same overflow class as DrawPostMatchResultLine
     * and DrawSocialListRow (see the former's own doc comment) --
     * "CHALLENGE FROM" (14) + space + a full-length entry.name (16) +
     * space + a full-length entry.tag (16) + EOS = 49 bytes, past the
     * previous `u8 buf[25]` before the 23-tile clip below ever runs. Not
     * yet reported live; found by auditing every sibling of the
     * post-match crash's own name+tag concatenation shape in this file. */
    u8 buf[56];
    u8 *dst;

    if (!PokePvPInbox_Get(inboxSlot, &entry))
        return; /* stale -- the task falls back to the menu */

    /* Window 0: the challenger line. ADR-226: no longer shows entry.tag
     * -- tags are internal-only now, visible solely on their own owner's
     * PROFILE/PLAYER SETTINGS screen. */
    FillWindowPixelBuffer(sWindowIds[0], PIXEL_FILL(10));
    dst = StringCopy(buf, sText_ChallengeFrom);
    *dst++ = CHAR_SPACE;
    dst = StringCopy(dst, entry.name);
    if ((u32)(dst - buf) > 23u)
        buf[23] = EOS; /* one tile per char, 24-tile window */
    AddTextPrinterParameterized3(sWindowIds[0], FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
    PutWindowTilemap(sWindowIds[0]);

    // POKEPVP (ADR-188): blank window 5 BEFORE the ERROR-band type line
    // below -- window 5's screen rows (16-17) fall inside the ERROR
    // window's own rows (15-18), and blanking it after let window 5's
    // tiles clobber the bottom half of that line (a real regression a
    // golden-slice test caught in DrawPackPickerItems's identical
    // overview line; same fix here, done up front instead).
    // alpha-build-plan P6 (docs/adr/264): deliberately NOT given the
    // window-5 identity tag every other screen got -- the ERROR band's
    // own type line (REMATCH/QUICK EARLY/QUICK ELITE, always shown)
    // draws right after and would win the shared rows regardless, per
    // the comment above. Left blank; not every screen needs it.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);

    /* Type line in the ERROR band (same pattern as the post-match and
       pack-picker summary lines). */
    if (entry.flags & POKEPVP_INBOX_FLAG_REMATCH)
        PrintMessageOnWindow4(sText_RematchLabel);
    else if (entry.flags & POKEPVP_INBOX_FLAG_EARLY)
        PrintMessageOnWindow4(sText_EarlyLabel);
    else
        PrintMessageOnWindow4(sText_EliteLabel);

    for (i = 1; i < 5; i++)
    {
        bool8 selected = (i - 1) == cursor;
        FillWindowPixelBuffer(sWindowIds[i], PIXEL_FILL(selected ? 13 : 10));
        if (i < 4)
            AddTextPrinterParameterized3(sWindowIds[i], FONT_NORMAL, 2, 2,
                selected ? sTextColorSelected : sTextColor1, -1, sActions[i - 1]);
        PutWindowTilemap(sWindowIds[i]);
    }
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    for (i = 0; i < 4; i++)
        CopyWindowToVram(sWindowIds[i], COPYWIN_GFX);
    CopyWindowToVram(sWindowIds[4], COPYWIN_FULL);
}

static void Task_PokePvPInbox(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        /* The caller (Task_HandleMenuInput) already faded OUT. */
        FreeTempTileDataBuffersIfPossible();
        ResetTempTileDataBuffers();
        ShowBg(0);
            SetVBlankCallback(VBlankCB_MainMenu);
        DrawInboxItems(gTasks[taskId].tInboxSlot, gTasks[taskId].tSubCursorPos);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (gPaletteFade.active)
            return;
        if (!PokePvPInbox_Count())
        {
            /* The host ended the burst while the popup was up. */
            LeaveInboxToMenu(taskId);
            break;
        }
        MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            switch (gTasks[taskId].tSubCursorPos)
            {
            case 0: /* ACCEPT */
                DrawTeamSelectorItems(0);
                gTasks[taskId].tSubMode = POKEPVP_MATCH_MODE_ACCEPT_CHALLENGE;
                gTasks[taskId].tSubCursorPos = 0;
                gTasks[taskId].func = Task_PokePvPTeamSelector;
                break;
            case 1: /* DECLINE */
                SendInboxAction(gTasks[taskId].tInboxSlot, 1, 0);
                PokePvPInbox_Remove(gTasks[taskId].tInboxSlot);
                LeaveInboxToMenu(taskId);
                break;
            case 2: /* BLOCK */
                SendInboxAction(gTasks[taskId].tInboxSlot, 2, 0);
                PrintMessageOnWindow4(sText_ChallengeBlocked);
                gTasks[taskId].tMGErrorMsgState = 2;
                break;
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            /* Dismiss = "later": re-arm only on the next menu init. */
            PlaySE(SE_SELECT);
            gPokePvPInboxSnoozed = TRUE;
            LeaveInboxToMenu(taskId);
        }
        else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
        {
            gTasks[taskId].tSubCursorPos--;
            DrawInboxItems(gTasks[taskId].tInboxSlot, gTasks[taskId].tSubCursorPos);
        }
        else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 2)
        {
            gTasks[taskId].tSubCursorPos++;
            DrawInboxItems(gTasks[taskId].tInboxSlot, gTasks[taskId].tSubCursorPos);
        }
        break;
    case 2: /* BLOCK result message, dismiss then remove + menu */
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 3:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            PokePvPInbox_Remove(gTasks[taskId].tInboxSlot);
            LeaveInboxToMenu(taskId);
            break;
        }
        break;
    }
}

// POKEPVP (ADR-192): the shared "actually start a match/practice/challenge
// with this team slot" sequence, extracted out of Task_PokePvPTeamSelector's
// own A-press so it has exactly one owner. Used by that screen (CUSTOM
// ELITE/INVITE/PRACTICE/ACCEPT_CHALLENGE/CHALLENGE_TARGET, where the player
// explicitly picks the slot in tSubCursorPos) and, since ADR-192, called
// directly by Task_PokePvPPackPicker for QUICK EARLY/ELITE, which never
// shows this screen at all -- see that task's own comment for why. CUSTOM
// ELITE/INVITE/PRACTICE/ACCEPT_CHALLENGE/CHALLENGE_TARGET's own selector
// still confirms PokePvPTeamBuilder_MemberCount(slot) != 0 before calling
// here (those modes have no other source for a real team); a pack pick
// no longer does, so slot may legitimately have zero members here.
static void StartPokePvPMatchWithTeam(u8 taskId, u8 slot)
{
    PlaySE(SE_SELECT);
    PokePvPTeamBuilder_LoadTeamForBattle(slot);
    // POKEPVP: only reachable with an empty gPlayerParty for a
    // pack-originated queue with no saved team anywhere (LoadTeamForBattle
    // above is a no-op for an empty slot). This local party only has to
    // survive the brief "waiting for opponent" window -- the real roster
    // for a pack match is the server's own materialized pack team,
    // applied by StartPokePvPRealMatch's PokePvPTeamBuilder_
    // LoadPackTeamForBattle call the moment pairing completes. Same
    // gPlayerPartyCount == 0 synthesis StartPokePvPDebugBattle already
    // uses (battle_setup.c, ADR-067) instead of rendering nothing.
    if (gPlayerPartyCount == 0)
    {
        ZeroMonData(&gPlayerParty[0]);
        CreateMon(&gPlayerParty[0], SPECIES_CHARMANDER, 5, 0, FALSE, 0, OT_ID_PLAYER_ID, 0);
        GiveMoveToMon(&gPlayerParty[0], MOVE_SCRATCH);
        gPlayerPartyCount = 1;
        DebugPrintf("POKEPVP: slot=%d has no saved team -- synthesized placeholder player mon (species=%d CHARMANDER) for local rendering only", slot, SPECIES_CHARMANDER);
    }
    // POKEPVP (ADR-125): matchmaking starts *here*, when the player
    // picks a team and presses AUTO-MATCH -- not when the launcher
    // process started. The team's own member records go up first, then
    // the request that tells the host to join pairing with them; the
    // ring is FIFO, so the host always has the team before it reads the
    // request. Sent unconditionally (even with no gateway configured):
    // with no gateway the host simply has nothing to join, and the
    // offline debug path below is unchanged.
    // POKEPVP (Phase J v2): PRACTICE (submenu item 2) sends
    // PRACTICE_REQUEST instead of MATCH_REQUEST -- the host starts a
    // practice match against the server AI with this same team.
    // POKEPVP (UI plan slice 3): the mode chosen in the START MATCH
    // tree rides ahead as MATCH_CONFIG so the host applies it to this
    // session's queue/pack selection before the join request arrives
    // (FIFO order). The short modes map straight to the old AUTO
    // behavior; the quick modes carry the picked class + pack row
    // (pack row 5 = RANDOM).
    PokePvPTeamBuilder_SendTeam(slot);
    {
        u8 payload[3];
        u8 mode = 0;
        u8 cls = 0;
        u8 packRow = 0;
        u8 len = 3;

        switch (gTasks[taskId].tSubMode)
        {
        case POKEPVP_MATCH_MODE_INVITE:
        case POKEPVP_MATCH_MODE_AUTO:
            mode = 0;
            break;
        case POKEPVP_MATCH_MODE_QUICK_EARLY:
            mode = 1;
            cls = 0;
            packRow = gTasks[taskId].tTeamSlot;
            break;
        case POKEPVP_MATCH_MODE_QUICK_ELITE:
            mode = 2;
            cls = 1;
            packRow = gTasks[taskId].tTeamSlot;
            break;
        case POKEPVP_MATCH_MODE_CUSTOM_ELITE:
            mode = 3;
            break;
        default: /* PRACTICE */
            len = 0;
            break;
        }
        if (len != 0)
        {
            payload[0] = mode;
            payload[1] = cls;
            payload[2] = packRow;
            PokePvPMailboxRing_TryWrite(&gPokePvPMailbox.romToHost,
                                        POKEPVP_MAILBOX_ROM_TO_HOST_MAGIC,
                                        POKEPVP_MSG_MATCH_CONFIG,
                                        0,
                                        mode,
                                        payload,
                                        len);
            DebugPrintf("POKEPVP: match config mode=%d class=%d packRow=%d sent", mode, cls, packRow);
        }
    }
    if (gTasks[taskId].tSubMode == POKEPVP_MATCH_MODE_PRACTICE)
        PokePvPTeamBuilder_RequestPractice(slot);
    // POKEPVP (UI plan slice 4): ACCEPT_CHALLENGE sends the chosen
    // team's records (already above via SendTeam) then a CHALLENGE_ACTION
    // accept -- the host pairs via challengeAccept, not matchmaking, so
    // no MATCH_REQUEST and no MATCH_CONFIG ride along.
    else if (gTasks[taskId].tSubMode == POKEPVP_MATCH_MODE_ACCEPT_CHALLENGE)
        SendInboxAction(gTasks[taskId].tInboxSlot, 0, slot);
    // POKEPVP (UI plan slice 6): CHALLENGE_TARGET sends the chosen
    // team's records then a SOCIAL_ACTION challenge against the
    // social-list row kept in tSocialList/tInboxSlot.
    else if (gTasks[taskId].tSubMode == POKEPVP_MATCH_MODE_CHALLENGE_TARGET)
        SendSocialAction(gTasks[taskId].tSocialList, 0, gTasks[taskId].tInboxSlot);
    else
        PokePvPTeamBuilder_RequestMatch(slot);
    gExitStairsMovementDisabled = FALSE;
    // POKEPVP (ADR-121): a real gateway pairing is worth a bounded
    // wait for the opponent's mon; no gateway (or no pairing yet) is
    // not -- checked here, synchronously, so the plain no-gateway case
    // takes the exact same zero-delay path it always has
    // (StartPokePvPAutoMatch's own "no overworld wait" design,
    // ADR-091) with no timing change at all. Only a real
    // POKEPVP_MSG_REAL_MATCH_PENDING record (sent the instant a real
    // matchStart arrives, main.rs) ever makes this true.
    // POKEPVP (ADR-124): PokePvP_IsOnlineMode() added alongside the
    // original pairing check. Waiting only when a pairing *already*
    // exists is what let AUTO-MATCH start a local AI battle whenever
    // the other player hadn't pressed their own AUTO-MATCH yet -- the
    // common case for two humans, and the one the owner hit. A
    // launcher with no gateway at all still takes the unchanged
    // zero-delay path below, so every offline golden-frame suite's
    // timing is untouched.
    // POKEPVP (HANDOFF item 32, still open): cheap, permanent diagnostic --
    // a prior session's real match log proved this exact check took the
    // offline branch despite ONLINE_MODE having landed minutes earlier,
    // with no code path found anywhere that clears the flag in between.
    // Left in place (not a TEMP hack) until that's actually explained by a
    // real log capturing both values at the point of failure.
    DebugPrintf("POKEPVP: online=%d pending=%d", PokePvP_IsOnlineMode(), PokePvP_IsRealMatchPending());
    if (PokePvP_IsOnlineMode() || PokePvP_IsRealMatchPending())
    {
        // Window buffers are deliberately NOT freed here (unlike the
        // branch below) -- the wait task reuses this same screen's
        // already-faded-in message window, matching
        // Task_PokePvPPrepareRoster's own precedent.
        gTasks[taskId].tMGErrorMsgState = 0;
        gTasks[taskId].func = Task_PokePvPWaitForRealOpponent;
    }
    else
    {
        FreeAllWindowBuffers();
        DestroyTask(taskId);
        StartPokePvPAutoMatch();
    }
}

static void Task_PokePvPTeamSelector(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        if (PokePvPTeamBuilder_MemberCount(gTasks[taskId].tSubCursorPos) == 0)
        {
            // POKEPVP (owner playtest, 2026-09-10): this used to be a
            // silent no-op -- indistinguishable from a dead button (the
            // same shape as ADR-189's bug 1). Every mode reaching this
            // screen (CUSTOM ELITE/INVITE/PRACTICE/CHALLENGE -- QUICK no
            // longer does, ADR-192) needs a real saved team in the
            // selected slot before A does anything; say so instead of
            // doing nothing. Reuses sText_TeamIsEmpty (already shown by
            // the team-builder list for the same condition) so this isn't
            // a new string to translate/maintain.
            PlaySE(SE_FAILURE);
            FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
            MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 0, 2, sTextColor1, -1, sText_TeamIsEmpty);
            PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_FULL);
            return;
        }

        StartPokePvPMatchWithTeam(taskId, gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        // POKEPVP (owner playtest, 2026-09-10): erase the "team is empty"
        // message (above) before leaving -- otherwise it bleeds through
        // onto the START MATCH submenu, which never touches this window
        // itself (the same "erase, don't occlude" discipline every other
        // screen's B-handler already follows for this window).
        MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
        DrawStartMatchSubmenuItems(0);
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawTeamSelectorItems(gTasks[taskId].tSubCursorPos); /* ADR-158: selection follows the cursor */
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_TEAM_SLOTS - 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawTeamSelectorItems(gTasks[taskId].tSubCursorPos); /* ADR-158 */
    }
}

// POKEPVP (ADR-093): one team-slot row. Shows the slot number and how many
// members it holds. Team management options (owner ask): now also shows a
// real custom name in parens when RENAME has set one, and a "*" marker on
// whichever slot SET AS ACTIVE last marked -- both real, server-persisted
// state (PokePvPTeamBuilder_GetName/GetActiveSlot), never fabricated. buf
// grown from 24 to 40: "TEAM 5 (RAINTEAM) -- 6/6" is 24 chars alone, and
// ADR-198's own lesson (a 25-byte buffer overflowing on a real 46-byte
// worst case, three sites in this same file) is exactly why this is sized
// generously rather than exactly, with StringCopy bounded by
// POKEPVP_TEAM_NAME_LENGTH on the name's own way in, not trusted to fit by
// construction alone.
static void DrawOneTeamRow(u8 windowId, u8 slot, bool8 selected)
{
    u8 buf[40];
    u8 *dest;
    u8 count;
    const u8 *name;

    count = PokePvPTeamBuilder_MemberCount(slot);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(selected ? 13 : 10));
    dest = buf;
    *dest++ = (PokePvPTeamBuilder_GetActiveSlot() == slot) ? CHAR_BULLET : CHAR_SPACE;
    dest = StringCopy(dest, sText_Team);
    dest = ConvertIntToDecimalStringN(dest, slot + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
    name = PokePvPTeamBuilder_GetName(slot);
    if (name[0] != EOS)
    {
        *dest++ = CHAR_SPACE;
        *dest++ = CHAR_LEFT_PAREN;
        dest = StringCopy(dest, name);
        *dest++ = CHAR_RIGHT_PAREN;
    }
    *dest++ = CHAR_SPACE;
    *dest++ = CHAR_SPACE;
    *dest++ = CHAR_SPACE;
    if (count == 0)
    {
        dest = StringCopy(dest, sText_TeamEmpty);
    }
    else
    {
        dest = ConvertIntToDecimalStringN(dest, count, STR_CONV_MODE_LEFT_ALIGN, 1);
        *dest++ = CHAR_SLASH;
        dest = ConvertIntToDecimalStringN(dest, PARTY_SIZE, STR_CONV_MODE_LEFT_ALIGN, 1);
    }
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, selected ? sTextColorSelected : sTextColor1, -1, buf);
    PutWindowTilemap(windowId);
}

// POKEPVP (ADR-093): five team slots in the five existing windows -- the
// list is exactly as long as the window row count, so B (not a sixth
// "EXIT" row) is the way out, matching the START MATCH submenu's own
// convention rather than inventing a second one.
static void DrawTeamListItems(u8 selectedIdx)
{
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_0, 0, selectedIdx == 0);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_1, 1, selectedIdx == 1);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_2, 2, selectedIdx == 2);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_3, 3, selectedIdx == 3);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_4, 4, selectedIdx == 4);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_0, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_1, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_FULL);
    // POKEPVP (ADR-188): see DrawStartMatchSubmenuItems's identical
    // comment -- blank window 5 too so leftover LEADERBOARD text never
    // bleeds through the now-taller shared panel.
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

// POKEPVP (ADR-093): drives the team-slot list. Identical input shape to
// Task_PokePvPStartMatchSubmenu (D-pad over the same 32px slot geometry,
// A selects, B backs out), so the two screens do not behave differently
// for no reason. A opens FireRed's own PC box screen with the roster
// loaded -- PokePvPTeamBuilder_Open calls EnterPokeStorage, which calls
// ResetTasks, so this task frees its windows and destroys itself first,
// exactly as the AUTO-MATCH path already does.
static void Task_PokePvPTeamList(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        // POKEPVP (ADR-093): a slot has two things you can do to it now --
        // choose its POKEMON (the PC) and change their moves -- so it opens
        // a submenu rather than one of them directly.
        gTasks[taskId].tTeamSlot = gTasks[taskId].tSubCursorPos;
        DrawSlotMenuItems(0);
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPSlotMenu;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].func = Task_PokePvPReturnToTopMenuFromTeamList;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawTeamListItems(gTasks[taskId].tSubCursorPos); /* ADR-158: selection follows the cursor */
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_TEAM_SLOTS - 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawTeamListItems(gTasks[taskId].tSubCursorPos); /* ADR-158 */
    }
}

// Team management options: COPY TO...'s destination picker. Reuses
// DrawTeamListItems/Task_PokePvPTeamList's own row rendering verbatim --
// this is the same 5-slot list, just picking a copy destination instead of
// opening a slot's submenu. Picking sPokePvPCopySourceSlot itself is a
// silent no-op (same "unselectable row" convention as an empty slot in
// Task_PokePvPTeamSelector, ADR-190) rather than a second dialog just to
// say "pick a different slot".
static void Task_PokePvPTeamCopyPicker(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        u8 destSlot = gTasks[taskId].tSubCursorPos;

        if (destSlot != sPokePvPCopySourceSlot)
        {
            PlaySE(SE_SELECT);
            PokePvPTeamBuilder_CopySlot(sPokePvPCopySourceSlot, destSlot);
            PokePvPTeamBuilder_SendTeam(destSlot);
            DrawTeamListItems(destSlot);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            gTasks[taskId].tSubCursorPos = destSlot;
            gTasks[taskId].func = Task_PokePvPTeamList;
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        // tTeamSlot is untouched since Task_PokePvPSlotMenu set it (still
        // sPokePvPCopySourceSlot's own source) -- nothing in this task
        // changes it.
        DrawSlotMenuItems(3);
        gTasks[taskId].tSubCursorPos = 3;
        gTasks[taskId].func = Task_PokePvPSlotMenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawTeamListItems(gTasks[taskId].tSubCursorPos);
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_TEAM_SLOTS - 1)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawTeamListItems(gTasks[taskId].tSubCursorPos);
    }
}

// POKEPVP (ADR-093): drives the one-time roster build with a message on
// screen.
//
// Building it in the background from Task_HandleMenuInput instead was
// tried and reverted: it works, and it does make TEAM BUILDER open
// instantly, but a species per frame costs enough CPU to slow the whole
// main menu for the first several seconds after boot -- which shifted the
// frame timing of every scripted menu navigation in this repo's own
// golden-frame suites, and in testing sent a script that meant to select
// TEAM BUILDER into an AUTO-MATCH battle instead. A visible, honest wait
// on the one screen that needs it beats a hidden cost on every screen.
//
// Same message-window shape as Task_PokePvPMenuStub, Same message-window shape as Task_PokePvPMenuStub, but it waits
// on real progress instead of a button, and needs no fade of its own --
// the team list is already faded in when this starts.
static void Task_PokePvPPrepareRoster(u8 taskId)
{
    u8 slot;

    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        PrintMessageOnWindow4(sText_PreparingTeamBuilder);
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
        // One species per frame. The step function is idempotent once
        // complete, so an extra call on the finishing frame is harmless.
        if (PokePvPTeamBuilder_BuildRosterStep() == 100)
        {
            slot = gTasks[taskId].tTeamSlot;
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            PokePvPTeamBuilder_Open(slot, CB2_InitMainMenu);
        }
        break;
    }
}

// POKEPVP (ADR-093; team management options grew this to 5 rows): the
// per-slot submenu. Now uses every one of the five shared windows (EDIT
// TEAM / EDIT MOVES / RENAME / COPY TO... / SET AS ACTIVE) -- the explicit
// BACK row this used to end with is gone, since B already backs out of
// this screen (unchanged) and two of this file's other sibling list
// screens (Task_PokePvPTeamList, Task_PokePvPStartMatchSubmenu) already
// rely on B alone with no redundant row of their own.
/* ADR-158: same rework as the selector/team-list rows -- selection is a
 * fill swap now (drawn fresh on every cursor move), and ONE panel frame
 * bounds the whole block. */
static void DrawSlotMenuItems(u8 selectedIdx)
{
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_0, PIXEL_FILL(selectedIdx == 0 ? 13 : 10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_1, PIXEL_FILL(selectedIdx == 1 ? 13 : 10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_2, PIXEL_FILL(selectedIdx == 2 ? 13 : 10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_3, PIXEL_FILL(selectedIdx == 3 ? 13 : 10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_4, PIXEL_FILL(selectedIdx == 4 ? 13 : 10));
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_0, FONT_NORMAL, 2, 2, selectedIdx == 0 ? sTextColorSelected : sTextColor1, -1, sText_EditTeam);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_1, FONT_NORMAL, 2, 2, selectedIdx == 1 ? sTextColorSelected : sTextColor1, -1, sText_EditMoves);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_2, FONT_NORMAL, 2, 2, selectedIdx == 2 ? sTextColorSelected : sTextColor1, -1, sText_RenameTeam);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_3, FONT_NORMAL, 2, 2, selectedIdx == 3 ? sTextColorSelected : sTextColor1, -1, sText_CopyTeamTo);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_4, FONT_NORMAL, 2, 2, selectedIdx == 4 ? sTextColorSelected : sTextColor1, -1, sText_SetActiveTeam);
    MainMenu_DrawWindow(&sPokePvPMenuPanelTemplate);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_0);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_1);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_2);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_3);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_4);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_0, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_1, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_FULL);
    // POKEPVP (ADR-188): see DrawStartMatchSubmenuItems's identical
    // comment -- blank window 5 too so leftover LEADERBOARD text never
    // bleeds through the now-taller shared panel.
    // alpha-build-plan P6 (docs/adr/264): same window-5 identity tag as
    // the top menu (ADR-263) -- see DrawPokePvPMenuItems's own comment.
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_5, PIXEL_FILL(10));
    AddTextPrinterParameterized3(
        MAIN_MENU_WINDOW_POKEPVP_5, FONT_NORMAL,
        (24 * 8 - GetStringWidth(FONT_NORMAL, sText_Wordmark, 0)) / 2, 2,
        sTextColor2, -1, sText_Wordmark);
    PutWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_5);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_5, COPYWIN_FULL);
}

// POKEPVP (ADR-093; team management options grew this to 5 rows): EDIT
// TEAM / EDIT MOVES / RENAME / COPY TO... / SET AS ACTIVE for one team
// slot. B backs out to the team list directly (see DrawSlotMenuItems' own
// doc comment for why there's no separate BACK row any more).
static void Task_PokePvPSlotMenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        switch (gTasks[taskId].tSubCursorPos)
        {
        case 0:
            sPokePvPReturnToTeamList = TRUE;
            if (PokePvPTeamBuilder_RosterReady())
            {
                FreeAllWindowBuffers();
                DestroyTask(taskId);
                PokePvPTeamBuilder_Open(gTasks[taskId].tTeamSlot, CB2_InitMainMenu);
            }
            else
            {
                // First entry this session: the roster has to be built, and
                // that takes real time. Show a message and build it a
                // species per frame rather than freezing on one huge frame.
                gTasks[taskId].tMGErrorMsgState = 0;
                gTasks[taskId].func = Task_PokePvPPrepareRoster;
            }
            break;
        case 1:
            if (PokePvPTeamBuilder_MemberCount(gTasks[taskId].tTeamSlot) == 0)
            {
                // Nothing to edit the moves of. Says so, rather than
                // opening an empty list the player has to work out.
                gTasks[taskId].tMGErrorMsgState = 0;
                gTasks[taskId].func = Task_PokePvPEmptyTeamMessage;
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            }
            else if (AllocPokePvPList())
            {
                gTasks[taskId].tMemberIndex = 0;
                BuildMemberList(taskId);
                OpenPokePvPList(taskId, PokePvPTeamBuilder_MemberCount(gTasks[taskId].tTeamSlot), FALSE);
                gTasks[taskId].func = Task_PokePvPPickMember;
            }
            // Out of heap: stay on the submenu rather than opening a list
            // with nothing behind it.
            break;
        case 2:
            // RENAME: FireRed's own generic box-naming screen
            // (NAMING_SCREEN_BOX, naming_screen.c), pre-filled with the
            // slot's current name (empty if none set yet). DoNamingScreen
            // tears down every task itself (EnterPokeStorage-style, same
            // as PLAYER SETTINGS' own NAME action, Task_PokePvPSpritePicker
            // above) -- this screen's own window buffers/task must be
            // freed first, exactly like case 0's PokePvPTeamBuilder_Open
            // call just above.
            sPokePvPRenameSlot = gTasks[taskId].tTeamSlot;
            StringCopy(sPokePvPTeamNameBuffer, PokePvPTeamBuilder_GetName(sPokePvPRenameSlot));
            FreeAllWindowBuffers();
            DestroyTask(taskId);
            DoNamingScreen(NAMING_SCREEN_BOX, sPokePvPTeamNameBuffer, 0, 0, 0, CB2_PokePvPTeamRenamed);
            break;
        case 3:
            // COPY TO...: a same-account copy between two of the five
            // local slots (see the header's own doc comment for why this
            // isn't the server's POST /clone). No fade -- an in-place list
            // swap, same convention as the pack picker's own class-picker
            // hop.
            sPokePvPCopySourceSlot = gTasks[taskId].tTeamSlot;
            gTasks[taskId].tSubCursorPos = 0;
            DrawTeamListItems(0);
            gTasks[taskId].func = Task_PokePvPTeamCopyPicker;
            break;
        case 4:
            if (PokePvPTeamBuilder_MemberCount(gTasks[taskId].tTeamSlot) == 0)
            {
                // Nothing saved server-side for this slot yet (no roster
                // has ever been sent) -- nothing real to mark active.
                gTasks[taskId].tMGErrorMsgState = 0;
                gTasks[taskId].func = Task_PokePvPEmptyTeamMessage;
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            }
            else
            {
                // ROM-local first, best-effort network write second --
                // same shape as ADR-209's SendSetSprite (ADR-209's own
                // comment on that call site explains why).
                PokePvPTeamBuilder_SetActiveSlotLocal(gTasks[taskId].tTeamSlot);
                SendActiveTeamSlot(gTasks[taskId].tTeamSlot);
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
                gTasks[taskId].func = Task_PokePvPReturnToTeamListFromSlotMenu;
            }
            break;
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].func = Task_PokePvPReturnToTeamListFromSlotMenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
        DrawSlotMenuItems(gTasks[taskId].tSubCursorPos); /* ADR-158: selection follows the cursor */
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 4)
    {
        gTasks[taskId].tSubCursorPos++;
        DrawSlotMenuItems(gTasks[taskId].tSubCursorPos); /* ADR-158 */
    }
}

// POKEPVP (ADR-093): back from the slot submenu to the team list, redrawing
// it so a team whose size just changed shows its new count.
//
// This is also the move editor's commit point. The PC path emits the team
// on its own exit, but move edits happen entirely inside this menu and
// would otherwise never reach the launcher at all. Emitting here rather
// than on every SetMemberMove keeps one edit from being one round trip:
// the mailbox ring holds 16 records and the host drains one per frame, so
// a player changing four moves quickly could outrun it and have records
// dropped as MB_FULL. Leaving the slot is the natural "done" moment.
static void Task_PokePvPReturnToTeamListFromSlotMenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    PokePvPTeamBuilder_SendTeam(gTasks[taskId].tTeamSlot);
    DrawTeamListItems(gTasks[taskId].tTeamSlot);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tSubCursorPos = gTasks[taskId].tTeamSlot;
    gTasks[taskId].func = Task_PokePvPTeamList;
}

// POKEPVP (ADR-093): "EDIT MOVES" on a team with nothing in it.
static void Task_PokePvPEmptyTeamMessage(u8 taskId)
{
    switch (gTasks[taskId].tMGErrorMsgState)
    {
    case 0:
        PrintMessageOnWindow4(sText_TeamIsEmpty);
        gTasks[taskId].tMGErrorMsgState++;
        break;
    case 1:
        if (!gPaletteFade.active)
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 2:
        RunTextPrinters();
        if (!IsTextPrinterActive(MAIN_MENU_WINDOW_ERROR))
            gTasks[taskId].tMGErrorMsgState++;
        break;
    case 3:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            // Team management options: SET AS ACTIVE (row 4) can now reach
            // this same empty-team message too, not just EDIT MOVES (row
            // 1) -- redraw whichever row actually triggered it instead of
            // a hardcoded 1.
            DrawSlotMenuItems(gTasks[taskId].tSubCursorPos);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPSlotMenu;
        }
        break;
    }
}

// POKEPVP (ADR-093): the editor's working buffer has to exist before the
// first Build*List call, not inside the Open that follows it -- the two are
// separate because every Build* writes straight into it. (They were one
// function first, and the member list rendered as garbage for exactly this
// reason: it was built through a null pointer, then displayed out of the
// freshly-allocated buffer that had never been written. The move-slot list
// looked fine and hid it, because by then the buffer was already live.)
static bool8 AllocPokePvPList(void)
{
    if (sPokePvPList == NULL)
        sPokePvPList = Alloc(sizeof(*sPokePvPList));
    return (sPokePvPList != NULL) ? TRUE : FALSE;
}

// POKEPVP (ADR-093): puts a scrolling list on screen, over the five menu
// windows. Requires AllocPokePvPList to have succeeded and the caller to
// have filled sPokePvPList->items already. The list window is added here and removed in ClosePokePvPList,
// so its ~10KB buffer only exists while a list does.
//
// WIN0 is opened to the whole screen for the duration: this menu normally
// uses it to darken everything except the selected row, which is exactly
// wrong over a list that draws its own cursor.
//
// POKEPVP (ADR-096): `withMoveInfo` additionally opens the move info panel
// in the unused space to the list window's right -- TRUE for the
// move-slot and movepool lists, FALSE for the species/member list, which
// has no move stats to show. tLastInfoMoveId is reset to -1 either way so
// the first per-frame update after opening always draws (or, when
// withMoveInfo is FALSE, so a stale value from a previous screen can
// never be read).
static void OpenPokePvPList(u8 taskId, u16 count, bool8 withMoveInfo)
{
    ClearWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_0);
    ClearWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_1);
    ClearWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_2);
    ClearWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_3);
    ClearWindowTilemap(MAIN_MENU_WINDOW_POKEPVP_4);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_0]);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_1]);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_2]);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_3]);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_4]);

    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(0, DISPLAY_WIDTH));
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(0, DISPLAY_HEIGHT));

    gTasks[taskId].tListWindowId = AddWindow(&sPokePvPListWindowTemplate);
    FillWindowPixelBuffer(gTasks[taskId].tListWindowId, PIXEL_FILL(10));
    MainMenu_DrawWindow(&sPokePvPListWindowTemplate);
    PutWindowTilemap(gTasks[taskId].tListWindowId);

    gMultiuseListMenuTemplate.items = sPokePvPList->items;
    gMultiuseListMenuTemplate.moveCursorFunc = ListMenuDefaultCursorMoveFunc;
    gMultiuseListMenuTemplate.itemPrintFunc = NULL;
    gMultiuseListMenuTemplate.totalItems = count;
    gMultiuseListMenuTemplate.maxShowed = (count < POKEPVP_LIST_ROWS) ? count : POKEPVP_LIST_ROWS;
    gMultiuseListMenuTemplate.windowId = gTasks[taskId].tListWindowId;
    gMultiuseListMenuTemplate.header_X = 0;
    gMultiuseListMenuTemplate.item_X = 8;
    gMultiuseListMenuTemplate.cursor_X = 0;
    gMultiuseListMenuTemplate.upText_Y = 1;
    // Palette indices, not colours: the same 10/11/12 triple sTextColor1
    // uses for every other row of text on this screen.
    gMultiuseListMenuTemplate.fillValue = 10;
    gMultiuseListMenuTemplate.cursorPal = 11;
    gMultiuseListMenuTemplate.cursorShadowPal = 12;
    gMultiuseListMenuTemplate.lettersSpacing = 0;
    gMultiuseListMenuTemplate.itemVerticalPadding = 0;
    gMultiuseListMenuTemplate.scrollMultiple = LIST_NO_MULTIPLE_SCROLL;
    gMultiuseListMenuTemplate.fontId = FONT_NORMAL;
    gMultiuseListMenuTemplate.cursorKind = 0;

    gTasks[taskId].tListTaskId = ListMenuInit(&gMultiuseListMenuTemplate, 0, 0);
    CopyWindowToVram(gTasks[taskId].tListWindowId, COPYWIN_FULL);

    gTasks[taskId].tLastInfoMoveId = -1;
    gTasks[taskId].tMoveInfoPage = 0;
    if (withMoveInfo)
    {
        gTasks[taskId].tMoveInfoWindowId = AddWindow(&sPokePvPMoveInfoWindowTemplate);
        FillWindowPixelBuffer(gTasks[taskId].tMoveInfoWindowId, PIXEL_FILL(10));
        MainMenu_DrawWindow(&sPokePvPMoveInfoWindowTemplate);
        PutWindowTilemap(gTasks[taskId].tMoveInfoWindowId);
        CopyWindowToVram(gTasks[taskId].tMoveInfoWindowId, COPYWIN_FULL);
    }
    else
    {
        gTasks[taskId].tMoveInfoWindowId = WINDOW_NONE;
    }
}

// POKEPVP (ADR-093): tears the list down and hands the screen back to the
// five-window menu. Deliberately does not free sPokePvPList -- the editor's
// three pickers open and close lists constantly, and reallocating ~1.3KB on
// every A press is a fragmentation risk for nothing. It is freed when the
// editor is left entirely (ReturnToSlotMenu).
static void ClosePokePvPList(u8 taskId)
{
    DestroyListMenuTask(gTasks[taskId].tListTaskId, NULL, NULL);
    ClearWindowTilemap(gTasks[taskId].tListWindowId);
    MainMenu_EraseWindow(&sPokePvPListWindowTemplate);
    CopyWindowToVram(gTasks[taskId].tListWindowId, COPYWIN_FULL);
    RemoveWindow(gTasks[taskId].tListWindowId);

    // POKEPVP (ADR-096): the info panel only exists when OpenPokePvPList
    // was called with withMoveInfo -- WINDOW_NONE means there is nothing
    // here to tear down.
    if (gTasks[taskId].tMoveInfoWindowId != WINDOW_NONE)
    {
        ClearWindowTilemap(gTasks[taskId].tMoveInfoWindowId);
        MainMenu_EraseWindow(&sPokePvPMoveInfoWindowTemplate);
        CopyWindowToVram(gTasks[taskId].tMoveInfoWindowId, COPYWIN_FULL);
        RemoveWindow(gTasks[taskId].tMoveInfoWindowId);
        gTasks[taskId].tMoveInfoWindowId = WINDOW_NONE;
    }
}

// POKEPVP (ADR-217): the move info panel's second page. Max line count
// and per-line buffer sized generously against the longest real
// gMoveDescription_* string in move_descriptions.c (~86 bytes including
// its own hand-baked \n's) -- these are stack-temporary (WrapMoveDescription
// is called from DrawPokePvPMoveInfo only, never stored), not EWRAM/IWRAM
// cost.
#define POKEPVP_MOVE_DESC_MAX_LINES 4
#define POKEPVP_MOVE_DESC_LINE_BUF  40
// 80px window, 2px margin each side (matches every other value on this
// panel's own x=2 left margin) -- 76px of usable width for the measured
// candidate line.
#define POKEPVP_MOVE_DESC_LINE_WIDTH_PX 76

// POKEPVP (ADR-217): real runtime word-wrap for gMoveDescriptionPointers'
// verbatim text (reused, not re-authored -- see the ADR for why this is
// the owner-approved reading of that line). Vanilla's own hand-baked
// '\n's are sized for pokemon_summary_screen.c's 120px-wide window
// (confirmed in ADR-216), not this panel's 80px one, so they are treated
// as ordinary word breaks here (CHAR_NEWLINE folds into CHAR_SPACE) and
// the layout is rebuilt from scratch for this panel's real width --
// greedy word-wrap, extending the candidate line one whole word at a
// time and measuring it with GetStringWidth, the same measurement
// approach DrawPokePvPMoveInfo's own typeValueX/statValueX calc already
// uses just below. Never splits a word mid-way. A description whose
// wrapped length exceeds POKEPVP_MOVE_DESC_MAX_LINES (the panel's real,
// fixed vertical budget below the header -- 64px / 16px-per-line, the
// same 16px line pitch ADR-097 already established as this window's real
// constraint) is truncated at the last whole word that still fits rather
// than overflowing past the window; a real, deliberate limit of this
// panel's fixed footprint, not a bug -- see ADR-217.
static u8 WrapMoveDescription(const u8 *src, u8 outLines[][POKEPVP_MOVE_DESC_LINE_BUF])
{
    u8 word[POKEPVP_MOVE_DESC_LINE_BUF];
    u8 line[POKEPVP_MOVE_DESC_LINE_BUF];
    u8 candidate[POKEPVP_MOVE_DESC_LINE_BUF];
    u8 lineCount = 0;
    u16 lineLen = 0, wordLen;
    const u8 *s = src;

    line[0] = EOS;

    while (*s != EOS && lineCount < POKEPVP_MOVE_DESC_MAX_LINES)
    {
        while (*s == CHAR_SPACE || *s == CHAR_NEWLINE)
            s++;
        if (*s == EOS)
            break;

        wordLen = 0;
        while (*s != EOS && *s != CHAR_SPACE && *s != CHAR_NEWLINE
               && wordLen < POKEPVP_MOVE_DESC_LINE_BUF - 1)
        {
            word[wordLen++] = *s++;
        }
        word[wordLen] = EOS;

        if (lineLen == 0)
        {
            StringCopy(candidate, word);
        }
        else
        {
            StringCopy(candidate, line);
            candidate[lineLen] = CHAR_SPACE;
            candidate[lineLen + 1] = EOS;
            StringAppend(candidate, word);
        }

        if (lineLen != 0 && GetStringWidth(FONT_NORMAL, candidate, 0) > POKEPVP_MOVE_DESC_LINE_WIDTH_PX)
        {
            // Candidate overflows -- commit the line as it stood before
            // this word, start a fresh line with the word that didn't fit.
            StringCopy(outLines[lineCount], line);
            lineCount++;
            if (lineCount >= POKEPVP_MOVE_DESC_MAX_LINES)
                break;
            StringCopy(line, word);
            lineLen = StringLength(line);
        }
        else
        {
            StringCopy(line, candidate);
            lineLen = StringLength(line);
        }
    }

    if (lineLen != 0 && lineCount < POKEPVP_MOVE_DESC_MAX_LINES)
    {
        StringCopy(outLines[lineCount], line);
        lineCount++;
    }

    return lineCount;
}

// POKEPVP (ADR-217): page 2's body. MOVE_NONE (an empty move slot, or the
// movepool list's own "clear this slot" row) gets the same three-hyphen
// placeholder page 1 already uses for it, matching
// DrawPokePvPMoveInfo's own existing move == MOVE_NONE discipline rather
// than indexing gMoveDescriptionPointers[-1].
static void DrawPokePvPMoveDescription(u8 windowId, u16 move)
{
    u8 lines[POKEPVP_MOVE_DESC_MAX_LINES][POKEPVP_MOVE_DESC_LINE_BUF];
    u8 lineCount, i;

    if (move == MOVE_NONE)
    {
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 18, sTextColor1, -1, gText_ThreeHyphens);
        return;
    }

    lineCount = WrapMoveDescription(gMoveDescriptionPointers[move - 1], lines);
    for (i = 0; i < lineCount; i++)
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 18 + (i * 16), sTextColor1, -1, lines[i]);
}

// POKEPVP (ADR-096, extended ADR-217): Type/Power/Accuracy/PP for one move
// (page 0), or its re-flowed description (page 1) -- or three-hyphen
// placeholders for MOVE_NONE (an empty move slot, or the movepool list's
// own "clear this slot" row) on either page. The same "read
// gBattleMoves[0] as if it were a real move" mistake would otherwise be
// silent and wrong rather than a crash, so MOVE_NONE is handled
// explicitly instead of falling through.
static void DrawPokePvPMoveInfo(u8 windowId, u16 move, u8 page)
{
    u8 buf[8];
    u8 *dest;
    u8 typeValueX;
    u8 statValueX;
    const u8 *typeColor;
    const u8 *pageText;
    u8 pageX;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(10));

    // POKEPVP (ADR-186, de-skin): plain header, same fill/text convention
    // every other selected row on this screen already uses -- no bespoke
    // colored stripe.
    FillWindowPixelRect(windowId, PIXEL_FILL(13), 0, 0, 80, 16);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, sTextColorSelected, -1, sText_MoveInfoHeader);

    // POKEPVP (ADR-217): minimal page indicator, right-aligned in the
    // header stripe next to "MOVE INFO" -- no dedicated icon fits this
    // panel's tile budget, so plain text is the whole feature here.
    pageText = (page == 0) ? sText_MoveInfoPage1 : sText_MoveInfoPage2;
    pageX = 78 - GetStringWidth(FONT_NORMAL, pageText, 0);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, pageX, 2, sTextColorSelected, -1, pageText);

    if (page == 1)
    {
        DrawPokePvPMoveDescription(windowId, move);
        CopyWindowToVram(windowId, COPYWIN_GFX);
        return;
    }

    // POKEPVP (ADR-097, fixes a real bug in ADR-096): FONT_NORMAL's real
    // line height is 16px (two tiles), the same "two tiles per row" the
    // move list itself already accounts for (POKEPVP_LIST_ROWS above) --
    // stacking a label directly above its value only 9-16px apart, as the
    // first version of this panel did, let each value's own draw call
    // overwrite the bottom of the label above it before it finished
    // rendering. Bisected by actually zooming into a captured frame's
    // real pixels (not the 2x-scaled thumbnail this session first judged
    // it from) and finding every *label* corrupted while every *value*
    // rendered perfectly -- exactly the signature of "drawn first, then
    // partially overwritten by the next line down", not a tile-budget or
    // charmap problem. Fixed by putting each label and its value on the
    // *same* line, side by side, so there is only one line per field
    // (four total) with a full 16px of clearance between them -- cheaper
    // on window height than stacking would need at the correct spacing,
    // which is what keeps this inside the tile budget ADR-096 already
    // had to fix once. Rows now start at y=18 instead of y=2, making room
    // for the header stripe above (ADR-099).
    typeValueX = 2 + GetStringWidth(FONT_NORMAL, sText_MoveInfoType, 0) + 4;
    statValueX = 2 + GetStringWidth(FONT_NORMAL, sText_MoveInfoPower, 0) + 4;

    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 18, sTextColor1, -1, sText_MoveInfoType);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 34, sTextColor1, -1, sText_MoveInfoPower);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 50, sTextColor1, -1, sText_MoveInfoAcc);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 66, sTextColor1, -1, sText_MoveInfoPP);

    if (move == MOVE_NONE)
    {
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, typeValueX, 18, sTextColor1, -1, gText_ThreeHyphens);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 34, sTextColor1, -1, gText_ThreeHyphens);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 50, sTextColor1, -1, gText_ThreeHyphens);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 66, sTextColor1, -1, gText_ThreeHyphens);
        CopyWindowToVram(windowId, COPYWIN_GFX);
        return;
    }

    // POKEPVP (ADR-186, de-skin): plain text for every type, same as
    // every other value on this panel -- no per-type accent palette.
    typeColor = sTextColor1;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, typeValueX, 18, typeColor, -1, gTypeNames[gBattleMoves[move].type]);

    if (gBattleMoves[move].power < 2)
        dest = StringCopy(buf, gText_ThreeHyphens);
    else
        dest = ConvertIntToDecimalStringN(buf, gBattleMoves[move].power, STR_CONV_MODE_LEFT_ALIGN, 3);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 34, sTextColor1, -1, buf);

    if (gBattleMoves[move].accuracy == 0)
        dest = StringCopy(buf, gText_ThreeHyphens);
    else
        dest = ConvertIntToDecimalStringN(buf, gBattleMoves[move].accuracy, STR_CONV_MODE_LEFT_ALIGN, 3);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 50, sTextColor1, -1, buf);

    dest = ConvertIntToDecimalStringN(buf, gBattleMoves[move].pp, STR_CONV_MODE_LEFT_ALIGN, 2);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 66, sTextColor1, -1, buf);

    CopyWindowToVram(windowId, COPYWIN_GFX);
}

// POKEPVP (ADR-096): resolves the currently-highlighted row into a real
// move id. BuildLegalMoveList's own .index field already *is* the move id
// (or MOVE_NONE for the clear-slot row), since that list's row order
// (alphabetical) doesn't match move-id order -- but BuildMoveSlotList's
// .index is just the slot position 0-3 in list order, so the move-slot
// list needs a second lookup through the team record to reach an actual
// move id.
static u16 GetHoveredMoveId(u8 taskId, bool8 isMoveSlotList)
{
    u16 cursorPos, itemsAbove;
    u16 position;

    ListMenuGetScrollAndRow(gTasks[taskId].tListTaskId, &cursorPos, &itemsAbove);
    position = cursorPos + itemsAbove;

    if (isMoveSlotList)
        return PokePvPTeamBuilder_MemberMove(gTasks[taskId].tTeamSlot, gTasks[taskId].tMemberIndex, position);

    return sPokePvPList->items[position].index;
}

// POKEPVP (ADR-096): called once per active frame from the move-slot and
// movepool list tasks, after ListMenu_ProcessInput so a same-frame D-pad
// move is already reflected. Only actually redraws on a real hover
// change, not every frame -- text redraws are cheap here, but there is no
// reason to touch VRAM every frame for an unchanging screen either.
static void UpdatePokePvPMoveInfo(u8 taskId, bool8 isMoveSlotList)
{
    u16 move;

    if (gTasks[taskId].tMoveInfoWindowId == WINDOW_NONE)
        return;

    move = GetHoveredMoveId(taskId, isMoveSlotList);
    if (move == gTasks[taskId].tLastInfoMoveId)
        return;

    gTasks[taskId].tLastInfoMoveId = move;
    // POKEPVP (ADR-217): every real hover change resets to page 1 -- a
    // flipped-to-page-2 view of the *previous* move would otherwise still
    // be showing when the cursor lands on a new one.
    gTasks[taskId].tMoveInfoPage = 0;
    DrawPokePvPMoveInfo(gTasks[taskId].tMoveInfoWindowId, move, 0);
}

static void ReturnToSlotMenu(u8 taskId)
{
    ClosePokePvPList(taskId);
    if (sPokePvPList != NULL)
    {
        Free(sPokePvPList);
        sPokePvPList = NULL;
    }
    DrawSlotMenuItems(1);
    gTasks[taskId].tSubCursorPos = 1;
    gTasks[taskId].func = Task_PokePvPSlotMenu;
}

// The team's members, by species name. Labels point straight into
// gSpeciesNames -- nothing is copied, and nothing can go stale, because the
// list is rebuilt whenever the team could have changed.
static void BuildMemberList(u8 taskId)
{
    u8 i;
    u8 count;
    u16 species;

    count = PokePvPTeamBuilder_MemberCount(gTasks[taskId].tTeamSlot);
    for (i = 0; i < count; i++)
    {
        species = PokePvPTeamBuilder_MemberSpecies(gTasks[taskId].tTeamSlot, i);
        sPokePvPList->items[i].label = gSpeciesNames[species];
        sPokePvPList->items[i].index = i;
    }
}

// One member's four move slots, by move name, "-" for an empty one.
static void BuildMoveSlotList(u8 taskId)
{
    u8 i;
    u16 move;

    for (i = 0; i < 4; i++)
    {
        move = PokePvPTeamBuilder_MemberMove(gTasks[taskId].tTeamSlot,
                                             gTasks[taskId].tMemberIndex, i);
        sPokePvPList->items[i].label = (move != MOVE_NONE) ? gMoveNames[move] : sText_NoMove;
        sPokePvPList->items[i].index = i;
    }
}

// Every move the member's species can legally learn, plus -- only when the
// mon would still have a move left afterwards -- a "-" row that clears the
// slot. Offering a clear that would be refused is a dead end; not offering
// one at all makes a mistakenly-filled fourth slot permanent.
//
// Returns the row count. `index` on each row is the move id itself (or
// MOVE_NONE for the clear row), so the chosen row needs no lookup back
// through a parallel array.
static u16 BuildLegalMoveList(u8 taskId)
{
    u16 species;
    u16 moveCount;
    u16 rows;
    u16 i;
    u8 known;

    species = PokePvPTeamBuilder_MemberSpecies(gTasks[taskId].tTeamSlot,
                                               gTasks[taskId].tMemberIndex);
    moveCount = PokePvPTeamBuilder_LegalMoves(species, sPokePvPList->moves,
                                              POKEPVP_MAX_LEGAL_MOVES);
    rows = 0;

    known = 0;
    for (i = 0; i < 4; i++)
    {
        if (PokePvPTeamBuilder_MemberMove(gTasks[taskId].tTeamSlot,
                                          gTasks[taskId].tMemberIndex, i) != MOVE_NONE)
            known++;
    }
    if (known > 1
     && PokePvPTeamBuilder_MemberMove(gTasks[taskId].tTeamSlot,
                                      gTasks[taskId].tMemberIndex,
                                      gTasks[taskId].tMoveSlot) != MOVE_NONE)
    {
        sPokePvPList->items[rows].label = sText_NoMove;
        sPokePvPList->items[rows].index = MOVE_NONE;
        rows++;
    }

    for (i = 0; i < moveCount; i++)
    {
        sPokePvPList->items[rows].label = gMoveNames[sPokePvPList->moves[i]];
        sPokePvPList->items[rows].index = sPokePvPList->moves[i];
        rows++;
    }
    return rows;
}

static void Task_PokePvPPickMember(u8 taskId)
{
    s32 chosen;

    if (gPaletteFade.active)
        return;

    // ListMenu_ProcessInput already handles B for us: it returns LIST_CANCEL
    // rather than leaving the press for a JOY_NEW check here, which is why
    // an earlier version of this screen could not be backed out of at all.
    chosen = ListMenu_ProcessInput(gTasks[taskId].tListTaskId);
    if (chosen == LIST_NOTHING_CHOSEN)
        return;
    if (chosen == LIST_CANCEL)
    {
        PlaySE(SE_SELECT);
        ReturnToSlotMenu(taskId);
        return;
    }

    PlaySE(SE_SELECT);
    gTasks[taskId].tMemberIndex = chosen;
    ClosePokePvPList(taskId);
    BuildMoveSlotList(taskId);
    OpenPokePvPList(taskId, 4, TRUE);
    gTasks[taskId].func = Task_PokePvPPickMoveSlot;
}

// POKEPVP (ADR-217): SELECT flips the move info panel between its stats
// page and its description page. Confirmed in ADR-216 that SELECT_BUTTON/
// L_BUTTON/R_BUTTON are used nowhere else in this whole file, so this is
// free. Handled once here, shared by both list tasks that own a panel,
// rather than duplicating the same four lines twice. Returns TRUE if a
// flip was handled this frame (caller returns immediately after, same
// "don't also run ListMenu_ProcessInput's own redraw path this frame"
// discipline UpdatePokePvPMoveInfo's own LIST_NOTHING_CHOSEN branch uses).
static bool8 TryFlipPokePvPMoveInfoPage(u8 taskId, bool8 isMoveSlotList)
{
    if (gTasks[taskId].tMoveInfoWindowId == WINDOW_NONE)
        return FALSE;
    if (!JOY_NEW(SELECT_BUTTON))
        return FALSE;

    PlaySE(SE_SELECT);
    gTasks[taskId].tMoveInfoPage ^= 1;
    DrawPokePvPMoveInfo(gTasks[taskId].tMoveInfoWindowId,
                         GetHoveredMoveId(taskId, isMoveSlotList),
                         gTasks[taskId].tMoveInfoPage);
    return TRUE;
}

// POKEPVP (ADR-096): the info panel update runs after ListMenu_ProcessInput
// so a same-frame D-pad move is already reflected, and only in the
// LIST_NOTHING_CHOSEN branch -- on A/B this list is about to close anyway,
// so there is nothing worth redrawing into a window that is torn down the
// same frame.
static void Task_PokePvPPickMoveSlot(u8 taskId)
{
    s32 chosen;
    u16 rows;

    if (gPaletteFade.active)
        return;

    if (TryFlipPokePvPMoveInfoPage(taskId, TRUE))
        return;

    chosen = ListMenu_ProcessInput(gTasks[taskId].tListTaskId);
    if (chosen == LIST_NOTHING_CHOSEN)
    {
        UpdatePokePvPMoveInfo(taskId, TRUE);
        return;
    }
    if (chosen == LIST_CANCEL)
    {
        PlaySE(SE_SELECT);
        ClosePokePvPList(taskId);
        BuildMemberList(taskId);
        OpenPokePvPList(taskId,
                        PokePvPTeamBuilder_MemberCount(gTasks[taskId].tTeamSlot), FALSE);
        gTasks[taskId].func = Task_PokePvPPickMember;
        return;
    }

    PlaySE(SE_SELECT);
    gTasks[taskId].tMoveSlot = chosen;
    ClosePokePvPList(taskId);
    rows = BuildLegalMoveList(taskId);
    OpenPokePvPList(taskId, rows, TRUE);
    gTasks[taskId].func = Task_PokePvPPickMove;
}

static void Task_PokePvPPickMove(u8 taskId)
{
    s32 chosen;

    if (gPaletteFade.active)
        return;

    if (TryFlipPokePvPMoveInfoPage(taskId, FALSE))
        return;

    chosen = ListMenu_ProcessInput(gTasks[taskId].tListTaskId);
    if (chosen == LIST_NOTHING_CHOSEN)
    {
        UpdatePokePvPMoveInfo(taskId, FALSE);
        return;
    }
    if (chosen == LIST_CANCEL)
    {
        PlaySE(SE_SELECT);
        ClosePokePvPList(taskId);
        BuildMoveSlotList(taskId);
        OpenPokePvPList(taskId, 4, TRUE);
        gTasks[taskId].func = Task_PokePvPPickMoveSlot;
        return;
    }

    PlaySE(SE_SELECT);
    // The write can still be refused (the species cannot learn it, or this
    // is the last move); the list is rebuilt either way, so the player sees
    // what actually happened rather than being told it worked.
    PokePvPTeamBuilder_SetMemberMove(gTasks[taskId].tTeamSlot,
                                     gTasks[taskId].tMemberIndex,
                                     gTasks[taskId].tMoveSlot,
                                     (u16)chosen);
    ClosePokePvPList(taskId);
    BuildMoveSlotList(taskId);
    OpenPokePvPList(taskId, 4, TRUE);
    gTasks[taskId].func = Task_PokePvPPickMoveSlot;
}

// POKEPVP (ADR-093): B out of the team list. Same shape as
// Task_PokePvPReturnToTopMenuFromSubmenu, but restores the cursor to the
// TEAM BUILDER row (1) the player came from rather than resetting to 0 --
// coming back to a different row than you left from reads as a bug.
static void Task_PokePvPReturnToTopMenuFromTeamList(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    DrawPokePvPMenuItems(1);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tCursorPos = 1;
    gTasks[taskId].func = Task_UpdateVisualSelection;
}

static void Task_ReturnToTileScreen(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetMainCallback2(CB2_InitTitleScreen);
        DestroyTask(taskId);
    }
}

static void MoveWindowByMenuTypeAndCursorPos(u8 menuType, u8 cursorPos)
{
    u16 win0vTop, win0vBot;
    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(18, 222));
    switch (menuType)
    {
    default:
    case MAIN_MENU_POKEPVP:
        // POKEPVP (main-menu backdrop redraw): 5 equal 16px slots packed
        // into rows 6-15 (pixel y 48-127), matching sWindowTemplate's
        // POKEPVP_0..4 tilemapTop values above -- keep both in lockstep.
        win0vTop = (0x30 + cursorPos * 0x10) << 8;
        win0vBot = 0x30 + (cursorPos + 1) * 0x10;
        break;
    case MAIN_MENU_NEWGAME:
        win0vTop = 0x00 << 8;
        win0vBot = 0x20;
        break;
    case MAIN_MENU_CONTINUE:
    case MAIN_MENU_MYSTERYGIFT:
        switch (cursorPos)
        {
        default:
        case 0: // CONTINUE
            win0vTop = 0x00 << 8;
            win0vBot = 0x60;
            break;
        case 1: // NEW GAME
            win0vTop = 0x60 << 8;
            win0vBot = 0x80;
            break;
        case 2: // MYSTERY GIFT
            win0vTop = 0x80 << 8;
            win0vBot = 0xA0;
            break;
        }
        break;
    }
    SetGpuReg(REG_OFFSET_WIN0V, (win0vTop + (2 << 8)) | (win0vBot - 2));
}

static bool8 HandleMenuInput(u8 taskId)
{
    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        IsWirelessAdapterConnected(); // called for its side effects only
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_ExecuteMainMenuSelection;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE(0, 240));
        SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(0, 160));
        gTasks[taskId].func = Task_ReturnToTileScreen;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tCursorPos > 0)
    {
        gTasks[taskId].tCursorPos--;
        return TRUE;
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tCursorPos < sMenuCursorYMax[gTasks[taskId].tMenuType])
    {
        gTasks[taskId].tCursorPos++;
        return TRUE;
    }

    return FALSE;
}

static void PrintMessageOnWindow4(const u8 *str)
{
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_ERROR, PIXEL_FILL(10));
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_ERROR, FONT_NORMAL, 0, 2, sTextColor1, 2, str);
    PutWindowTilemap(MAIN_MENU_WINDOW_ERROR);
    CopyWindowToVram(MAIN_MENU_WINDOW_ERROR, COPYWIN_GFX);
    SetGpuReg(REG_OFFSET_WIN0H, WIN_RANGE( 19, 221));
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(115, 157));
}

static void PrintContinueStats(void)
{
    PrintPlayerName();
    PrintDexCount();
    PrintPlayTime();
    PrintBadgeCount();
}

static void PrintPlayerName(void)
{
    s32 i;
    u8 name[PLAYER_NAME_LENGTH + 1];
    u8 *ptr;
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 2, 18, sTextColor2, -1, gText_Player);
    ptr = name;
    for (i = 0; i < PLAYER_NAME_LENGTH; i++)
        *ptr++ = gSaveBlock2Ptr->playerName[i];
    *ptr = EOS;
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 62, 18, sTextColor2, -1, name);
}

static void PrintPlayTime(void)
{
    u8 strbuf[30];
    u8 *ptr;

    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 2, 34, sTextColor2, -1, gText_Time);
    ptr = ConvertIntToDecimalStringN(strbuf, gSaveBlock2Ptr->playTimeHours, STR_CONV_MODE_LEFT_ALIGN, 3);
    *ptr++ = CHAR_COLON;
    ConvertIntToDecimalStringN(ptr, gSaveBlock2Ptr->playTimeMinutes, STR_CONV_MODE_LEADING_ZEROS, 2);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 62, 34, sTextColor2, -1, strbuf);
}

static void PrintDexCount(void)
{
    u8 strbuf[30];
    u8 *ptr;
    u16 dexcount;
    if (FlagGet(FLAG_SYS_POKEDEX_GET) == TRUE)
    {
        if (IsNationalPokedexEnabled())
            dexcount = GetNationalPokedexCount(FLAG_GET_CAUGHT);
        else
            dexcount = GetKantoPokedexCount(FLAG_GET_CAUGHT);
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 2, 50, sTextColor2, -1, gText_Pokedex);
        ptr = ConvertIntToDecimalStringN(strbuf, dexcount, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringAppend(ptr, gTextJPDummy_Hiki);
        AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 62, 50, sTextColor2, -1, strbuf);
    }
}

static void PrintBadgeCount(void)
{
    u8 strbuf[30];
    u8 *ptr;
    u32 flagId;
    u8 nbadges = 0;
    for (flagId = FLAG_BADGE01_GET; flagId < FLAG_BADGE01_GET + 8; flagId++)
    {
        if (FlagGet(flagId))
            nbadges++;
    }
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 2, 66, sTextColor2, -1, gText_Badges);
    ptr = ConvertIntToDecimalStringN(strbuf, nbadges, STR_CONV_MODE_LEADING_ZEROS, 1);
    StringAppend(ptr, gTextJPDummy_Ko);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_CONTINUE, FONT_NORMAL, 62, 66, sTextColor2, -1, strbuf);
}

// POKEPVP (ADR-186, de-skin): both loaders use FireRed's own stock window
// frame graphics, the same GetUserWindowGraphics() call option_menu.c/
// mon_markings.c already use for their own frames, instead of a
// hand-drawn navy/gold graphic. This is what "minimalistic" means here:
// no bespoke skin, just FireRed's own UI.
//
// POKEPVP (ADR-290, P6 border pinning; corrected ADR-291): the frame
// index is now a fixed PokePvP constant, not
// gSaveBlock2Ptr->optionsWindowFrameType. That field is the player's own
// real vanilla OPTIONS -> Frame Type choice (one of 20 decorative
// borders, still read/written by option_menu.c exactly as before --
// untouched here); reading it for PokePvP's own screens meant two
// players could see differently-colored/styled PokePvP UI, and it could
// change mid-session, which contradicts a "coherent... restrained
// palette in verified owned resources" P6 asks for.
// gUserFrames[POKEPVP_FIXED_WINDOW_FRAME_TYPE] (vanilla "Type1": a
// plain dark charcoal/navy double-outline, no texture, no saturated
// hue -- picked from an actual in-ROM headless capture of each
// candidate, not a static palette-file decode, after ADR-290's original
// pick (Type7) turned out to be a busy marbled/cloud texture that read
// as "unpolished dashes"/text-crowds-the-border in real play, and a
// second candidate (Type2) turned out to render magenta in practice
// despite looking like a plain black outline when the source tile data
// was decoded and re-rendered standalone -- that standalone decode had
// a bug, so ADR-291 settled on trusting only real in-ROM capture output
// from here on) is a static, always-owned asset, so this is a pure
// constant swap: zero new VRAM, zero new tiles/palette data.
#define POKEPVP_FIXED_WINDOW_FRAME_TYPE 0

static void LoadUserFrameToBg(u8 bgId)
{
    LoadBgTiles(bgId, GetUserWindowGraphics(POKEPVP_FIXED_WINDOW_FRAME_TYPE)->tiles, 0x120, POKEPVP_PANEL_FRAME_BASE_TILE);
    LoadPalette(GetUserWindowGraphics(POKEPVP_FIXED_WINDOW_FRAME_TYPE)->palette, BG_PLTT_ID(2), PLTT_SIZE_4BPP);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
}

static void SetStdFrame0OnBg(u8 bgId)
{
    LoadBgTiles(bgId, GetUserWindowGraphics(POKEPVP_FIXED_WINDOW_FRAME_TYPE)->tiles, 0x120, POKEPVP_PANEL_FRAME_BASE_TILE);
    LoadPalette(GetUserWindowGraphics(POKEPVP_FIXED_WINDOW_FRAME_TYPE)->palette, BG_PLTT_ID(2), PLTT_SIZE_4BPP);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
}

static void MainMenu_DrawWindow(const struct WindowTemplate * windowTemplate)
{
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 0, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop - 1,
        1,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg,
        POKEPVP_PANEL_FRAME_BASE_TILE + 1,
        windowTemplate->tilemapLeft,
        windowTemplate->tilemapTop - 1,
        windowTemplate->width,
        1, // POKEPVP fix: was `windowTemplate->height` -- stamped the top-edge
           // tile across the whole window body, not just its top row. Invisible
           // at the old per-row height=2 (mostly hidden under real content);
           // fatal once this drew ONE panel spanning height=10 (menu redesign).
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 2, 
        windowTemplate->tilemapLeft + 
        windowTemplate->width, 
        windowTemplate->tilemapTop - 1,
        1,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 3, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop,
        1, 
        windowTemplate->height,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 5, 
        windowTemplate->tilemapLeft + 
        windowTemplate->width, 
        windowTemplate->tilemapTop,
        1, 
        windowTemplate->height,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 6, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop + 
        windowTemplate->height,
        1,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 7, 
        windowTemplate->tilemapLeft, 
        windowTemplate->tilemapTop + 
        windowTemplate->height, 
        windowTemplate->width,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        POKEPVP_PANEL_FRAME_BASE_TILE + 8,
        windowTemplate->tilemapLeft +
        windowTemplate->width, 
        windowTemplate->tilemapTop + 
        windowTemplate->height,
        1,
        1,
        2
    );
    CopyBgTilemapBufferToVram(windowTemplate->bg);
}

static void MainMenu_EraseWindow(const struct WindowTemplate * windowTemplate)
{
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x000, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop - 1,  
        windowTemplate->tilemapLeft + 
        windowTemplate->width + 1, 
        windowTemplate->tilemapTop + 
        windowTemplate->height + 1,
        2
    );
    CopyBgTilemapBufferToVram(windowTemplate->bg);
}
