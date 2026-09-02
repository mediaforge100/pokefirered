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
#include "pokepvp_team_builder.h" // POKEPVP (ADR-093): TEAM BUILDER
#include "list_menu.h" // POKEPVP (ADR-093): the move editor's scrolling lists
#include "data.h"        // POKEPVP (ADR-093): gSpeciesNames, gMoveNames
#include "pokemon.h"     // POKEPVP (ADR-096): gBattleMoves, for the move info panel
#include "battle_main.h" // POKEPVP (ADR-096): gTypeNames, for the move info panel
#include "constants/moves.h"
#include "quest_log.h"
#include "mystery_gift_menu.h"
#include "strings.h"
#include "title_screen.h"
#include "help_system.h"
#include "pokedex.h"
#include "text_window.h"
#include "text_window_graphics.h"
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
    // POKEPVP (ADR-085): the five real menu slots, top to bottom.
    MAIN_MENU_WINDOW_POKEPVP_0, // START MATCH
    MAIN_MENU_WINDOW_POKEPVP_1, // TEAM BUILDER (stub)
    MAIN_MENU_WINDOW_POKEPVP_2, // PLAYER SETTINGS (stub)
    MAIN_MENU_WINDOW_POKEPVP_3, // LEADERBOARD (stub)
    MAIN_MENU_WINDOW_POKEPVP_4, // OPTIONS (stub)
    MAIN_MENU_WINDOW_ERROR,
    MAIN_MENU_WINDOW_COUNT
};

#define tMenuType  data[0]
#define tCursorPos data[1]
// POKEPVP (ADR-091): AUTO-MATCH/INVITE MATCH submenu cursor, separate from
// the top-level 5-item menu's tCursorPos so returning to the top menu
// doesn't need to remember/restore a shared field.
#define tSubCursorPos data[2]

// POKEPVP (ADR-093): move-editor state. data[3..7] are unused by every
// other menu type here, and the editor is only ever reachable from the
// team list, so it does not have to coexist with anything.
#define tTeamSlot        data[3]
#define tMemberIndex     data[4]
#define tMoveSlot        data[5]
#define tListTaskId      data[6]
#define tListWindowId    data[7]

#define tUnused8         data[8]
#define tMGErrorMsgState data[9]
#define tMGErrorType     data[10]

// POKEPVP (ADR-096): the move info panel shown alongside the move-slot and
// movepool lists. tMoveInfoWindowId is WINDOW_NONE when no panel is open
// (the species/member lists don't get one) -- ClosePokePvPList checks this
// to know whether there is a second window to tear down. tLastInfoMoveId
// starts at -1 (no real move id is negative) so the first frame after
// opening always draws, then only redraws on an actual hover change.
#define tMoveInfoWindowId data[11]
#define tLastInfoMoveId   data[12]

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
static void DrawPokePvPMenuItems(void);
// POKEPVP (ADR-091): START MATCH -> AUTO-MATCH/INVITE MATCH submenu.
static void DrawStartMatchSubmenuItems(void);
static void Task_PokePvPStartMatchSubmenu(u8 taskId);
static void Task_PokePvPInviteMatchStub(u8 taskId);
static void Task_PokePvPReturnToTopMenuFromSubmenu(u8 taskId);
// POKEPVP (ADR-095): AUTO-MATCH -> team-selector screen, a deliberately
// separate picker from TEAM BUILDER's own team-slot list below -- no
// EDIT TEAM/EDIT MOVES submenu, A here starts the match directly.
static void DrawTeamSelectorItems(void);
static void Task_PokePvPTeamSelector(u8 taskId);
// POKEPVP (ADR-093): TEAM BUILDER -> team-slot list.
static void DrawTeamListItems(void);
static void Task_PokePvPTeamList(u8 taskId);
static void Task_PokePvPReturnToTopMenuFromTeamList(u8 taskId);
static void Task_PokePvPPrepareRoster(u8 taskId);
// POKEPVP (ADR-093): the move editor -- slot submenu, then three nested
// ListMenu pickers (member -> move slot -> legal move).
static void DrawSlotMenuItems(void);
static void Task_PokePvPSlotMenu(u8 taskId);
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
static void DrawPokePvPMoveInfo(u8 windowId, u16 move);
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

static const u8 sString_Dummy[] = _("");
static const u8 sString_Newline[] = _("\n");
// POKEPVP (ADR-079/080, D7 step 1): replaces "NEW GAME" -- this screen's
// existing New-Game slot is retargeted to StartPokePvPMenuMatch instead
// of StartNewGameScene (see Task_ExecuteMainMenuSelection below), and
// the label needs to match. A local string, not an edit to the shared
// gText_NewGame (strings.c), since that constant may be referenced
// elsewhere in ways this change has no business touching.
static const u8 sText_StartMatch[] = _("START MATCH");
// POKEPVP (ADR-085, D7): the real five-item menu. Team Builder/Player
// Settings/Leaderboard/Options are stubs (Task_PokePvPMenuStub) -- real
// implementations are Phase 5-8 work, out of scope here (ADR-079's own
// "Non-goals").
static const u8 sText_TeamBuilder[] = _("TEAM BUILDER");
static const u8 sText_PlayerSettings[] = _("PLAYER SETTINGS");
static const u8 sText_Leaderboard[] = _("LEADERBOARD");
static const u8 sText_Options[] = _("OPTIONS");
static const u8 sText_NotYetImplemented[] = _("Not yet implemented.");
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
static const u8 sText_EditTeam[] = _("EDIT TEAM");
static const u8 sText_EditMoves[] = _("EDIT MOVES");
static const u8 sText_Back[] = _("BACK");
static const u8 sText_TeamIsEmpty[] = _("This team has no POKéMON yet.");
// The empty move slot, and the "clear this slot" row. One dash, used as
// both -- a slot showing "-" and a choice reading "-" are the same idea.
static const u8 sText_NoMove[] = _("-");
// POKEPVP (ADR-096): the move info panel's field labels.
static const u8 sText_MoveInfoType[] = _("TYPE");
static const u8 sText_MoveInfoPower[] = _("POWER");
static const u8 sText_MoveInfoAcc[] = _("ACC.");
static const u8 sText_MoveInfoPP[] = _("PP");
static const u8 sText_AutoMatch[] = _("AUTO-MATCH");
static const u8 sText_InviteMatch[] = _("INVITE MATCH");

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
    // POKEPVP (ADR-085): five equal slots, 4 tiles (32px) apart, spanning
    // the full 20-tile-tall screen (1,5,9,13,17) the same way the original
    // NEWGAME(13)/MYSTERYGIFT(17) pair was already spaced. Fresh baseBlocks
    // (0x151+) past every block FireRed's own menu already uses (highest is
    // MYSTERYGIFT's 0x121) so these never alias tiles with the still-compiled
    // but unreachable NEWGAME/CONTINUE/MYSTERYGIFT windows above.
    [MAIN_MENU_WINDOW_POKEPVP_0] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 1, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x151
    },
    [MAIN_MENU_WINDOW_POKEPVP_1] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 5, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x181
    },
    [MAIN_MENU_WINDOW_POKEPVP_2] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 9, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x1b1
    },
    [MAIN_MENU_WINDOW_POKEPVP_3] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 13, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x1e1
    },
    [MAIN_MENU_WINDOW_POKEPVP_4] = {
        .bg = 0, .tilemapLeft = 3, .tilemapTop = 17, .width = 24, .height = 2,
        .paletteNum = 15, .baseBlock = 0x211
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

// POKEPVP (ADR-093): the move editor's list window. Added and removed on
// demand (AddWindow/RemoveWindow) rather than living in sWindowTemplate:
// at 18x18 tiles its buffer is ~10KB of heap, which there is no reason to
// hold for the whole life of a menu that mostly is not the move editor.
// baseBlock 0x241 starts past MAIN_MENU_WINDOW_POKEPVP_4's own tiles
// (0x211 + 24*2), the same "fresh blocks past everything already in use"
// rule ADR-085 followed for the five menu slots.
static const struct WindowTemplate sPokePvPListWindowTemplate = {
    .bg = 0, .tilemapLeft = 2, .tilemapTop = 1, .width = 18, .height = 18,
    .paletteNum = 15, .baseBlock = 0x241
};

// Rows visible at once: two tiles per row over an 18-tile-tall window.
#define POKEPVP_LIST_ROWS 9

// POKEPVP (ADR-096/097): the move info panel, in the ~80px (tilemapLeft
// 20..29) the list window's own 18-tile width leaves unused on the
// right. Deliberately NOT the list's own 18-tile height -- an 18-tall
// window here (180 tiles) stacked on top of the list window's 324
// (baseBlock 0x241) pushed the total past this background's real tile
// budget (ADR-096): confirmed by capturing a frame and finding the
// bottom third rendering as the raw backdrop color instead of drawn
// text. 10x9 (90 tiles, baseBlock 0x385..0x3DF) stays comfortably clear
// of that. Height dropped further from ADR-096's original 11 once
// DrawPokePvPMoveInfo moved to one line per field instead of two
// (ADR-097) -- four 16px-tall lines is all this needs now. Same
// on-demand Add/RemoveWindow lifetime as the list window, opened only
// for the move-slot and movepool lists (not the species list, which has
// no move stats to show).
static const struct WindowTemplate sPokePvPMoveInfoWindowTemplate = {
    .bg = 0, .tilemapLeft = 20, .tilemapTop = 1, .width = 10, .height = 9,
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

static const u8 sTextColor1[] = { 10, 11, 12 };

static const u8 sTextColor2[] = { 10,  1, 12 };

static const struct BgTemplate sBgTemplate[] = {
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 30,
        .priority = 0
    }
};

static const u8 sMenuCursorYMax[] = { 0, 1, 2, 4 }; // POKEPVP (ADR-085): 5 items, cursor 0-4

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

static bool32 MainMenuGpuInit(u8 a0)
{
    u8 taskId;

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
    gTasks[taskId].tUnused8 = a0;
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
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(0, 0));
        SetGpuReg(REG_OFFSET_BLDY, 7);
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
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(0, 0));
        SetGpuReg(REG_OFFSET_BLDY, 7);
        if (gTasks[taskId].tMenuType == MAIN_MENU_NEWGAME)
            gTasks[taskId].func = Task_ExecuteMainMenuSelection;
        else
            gTasks[taskId].func = Task_WaitFadeAndPrintMainMenuText;
    }
}

static void Task_WaitFadeAndPrintMainMenuText(u8 taskId)
{
    if (!gPaletteFade.active)
    {
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
    SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN);
    SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(0, 0));
    SetGpuReg(REG_OFFSET_BLDY, 7);
    if (gSaveBlock2Ptr->playerGender == MALE)
        pal = RGB(4, 16, 31);
    else
        pal = RGB(31, 3, 21);
    LoadPalette(&pal, BG_PLTT_ID(15) + 1, PLTT_SIZEOF(1));
    switch (gTasks[taskId].tMenuType)
    {
    case MAIN_MENU_POKEPVP:
    default:
        DrawPokePvPMenuItems();
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

// POKEPVP (ADR-085): draws all 5 real menu items. Shared by
// Task_PrintMainMenuText (first draw, with the fade-in) and
// Task_PokePvPMenuStub's return path (redraw after a stub message, no fade).
static void DrawPokePvPMenuItems(void)
{
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_0, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_1, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_2, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_3, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_4, PIXEL_FILL(10));
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_0, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_StartMatch);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_1, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_TeamBuilder);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_2, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_PlayerSettings);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_3, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_Leaderboard);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_4, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_Options);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_0]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_1]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_2]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_3]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_4]);
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
}

static void Task_WaitDma3AndFadeIn(u8 taskId)
{
    if (WaitDma3Request(-1) != -1)
    {
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
        DrawTeamListItems();
        gTasks[taskId].tCursorPos = 1;
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPTeamList;
        return;
    }
    MoveWindowByMenuTypeAndCursorPos(gTasks[taskId].tMenuType, gTasks[taskId].tCursorPos);
    gTasks[taskId].func = Task_HandleMenuInput;
}

static void Task_HandleMenuInput(u8 taskId)
{
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
                DrawStartMatchSubmenuItems();
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
                DrawTeamListItems();
                BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
                gTasks[taskId].tSubCursorPos = 0;
                gTasks[taskId].func = Task_PokePvPTeamList;
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
                // Live this reads as "select a stub -> black screen", not a
                // hang -- confirmed distinct from ADR-085's already-fixed
                // permanently-black dismissal bug.
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
            DrawPokePvPMenuItems();
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_UpdateVisualSelection;
        }
        break;
    }
}

// POKEPVP (ADR-091): draws the 2-item AUTO-MATCH/INVITE MATCH submenu into
// the same 5 window slots DrawPokePvPMenuItems uses -- rows 0/1 get real
// labels, rows 2-4 are left blank (still drawn/tilemapped so any leftover
// top-level text is actually erased, not just occluded). Exact same
// draw/tilemap/vram-copy shape as DrawPokePvPMenuItems, just fewer labels
// -- same "reuse, don't duplicate" discipline as that function itself.
static void DrawStartMatchSubmenuItems(void)
{
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_0, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_1, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_2, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_3, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_4, PIXEL_FILL(10));
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_0, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_AutoMatch);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_1, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_InviteMatch);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_0]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_1]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_2]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_3]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_4]);
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
}

// POKEPVP (ADR-091): drives the 2-item submenu -- D-pad moves the WIN0
// highlight between rows 0/1 (reusing MoveWindowByMenuTypeAndCursorPos's
// existing MAIN_MENU_POKEPVP 32px-slot geometry, which already matches
// these windows' tilemapTop spacing exactly), A selects, B returns to the
// top-level 5-item menu.
static void Task_PokePvPStartMatchSubmenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        if (gTasks[taskId].tSubCursorPos == 0)
        {
            // POKEPVP (ADR-095): AUTO-MATCH now opens a team-selector
            // screen first instead of firing StartPokePvPAutoMatch()
            // directly -- until now there was no way to choose which
            // built team entered the match at all. Live redraw within the
            // same still-faded-in menu CB2, same as Task_PokePvPTeamList's
            // own transition into its slot submenu -- no palette fade.
            DrawTeamSelectorItems();
            gTasks[taskId].tSubCursorPos = 0;
            gTasks[taskId].func = Task_PokePvPTeamSelector;
        }
        else
        {
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPInviteMatchStub;
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
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
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 1)
    {
        gTasks[taskId].tSubCursorPos++;
    }
}

// POKEPVP (ADR-091): INVITE MATCH stub -- identical shape to
// Task_PokePvPMenuStub, but returns into the submenu (not the top-level
// menu) on dismissal, since that's where the player selected it from.
static void Task_PokePvPInviteMatchStub(u8 taskId)
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
            PlaySE(SE_SELECT);
            ClearWindowTilemap(MAIN_MENU_WINDOW_ERROR);
            MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
            DrawStartMatchSubmenuItems();
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
            gTasks[taskId].tMGErrorMsgState = 0;
            gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
        }
        break;
    }
}

// POKEPVP (ADR-091): B out of the submenu -- fades to black (done by the
// caller before switching to this func), redraws the real 5-item top-level
// menu, fades back in, hands off to Task_UpdateVisualSelection exactly like
// Task_PokePvPMenuStub's own dismissal path does.
static void Task_PokePvPReturnToTopMenuFromSubmenu(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    DrawPokePvPMenuItems();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, 0xFFFF);
    gTasks[taskId].tCursorPos = 0;
    gTasks[taskId].func = Task_UpdateVisualSelection;
}

// POKEPVP (ADR-095): one team-selector row. Deliberately terser than the
// builder's own row below (exact "x/6" member count) -- this screen is a
// picker, not an editor, so READY/EMPTY is all a player needs to decide.
static void DrawOneSelectorRow(u8 windowId, u8 slot)
{
    u8 buf[24];
    u8 *dest;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(10));
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
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
    MainMenu_DrawWindow(&sWindowTemplate[windowId]);
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
static void DrawTeamSelectorItems(void)
{
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_0, 0);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_1, 1);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_2, 2);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_3, 3);
    DrawOneSelectorRow(MAIN_MENU_WINDOW_POKEPVP_4, 4);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_0, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_1, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_FULL);
}

// POKEPVP (ADR-095): drives the team-selector list. Same D-pad/32px-slot
// input shape as every other list in this file. A on an empty slot is
// silently ignored -- an unselectable row is expected UI here, not a
// mistake worth a dialog. A on a populated slot loads that team's real
// Pokemon into gPlayerParty (PokePvPTeamBuilder_LoadTeamForBattle) before
// handing off to StartPokePvPAutoMatch, so gPlayerPartyCount is non-zero by
// the time StartPokePvPDebugBattle runs and its own gPlayerPartyCount == 0
// synthesis (ADR-067) never overwrites it.
static void Task_PokePvPTeamSelector(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    MoveWindowByMenuTypeAndCursorPos(MAIN_MENU_POKEPVP, gTasks[taskId].tSubCursorPos);

    if (JOY_NEW(A_BUTTON))
    {
        if (PokePvPTeamBuilder_MemberCount(gTasks[taskId].tSubCursorPos) == 0)
            return;

        PlaySE(SE_SELECT);
        PokePvPTeamBuilder_LoadTeamForBattle(gTasks[taskId].tSubCursorPos);
        gExitStairsMovementDisabled = FALSE;
        FreeAllWindowBuffers();
        DestroyTask(taskId);
        StartPokePvPAutoMatch();
    }
    else if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_SELECT);
        DrawStartMatchSubmenuItems();
        gTasks[taskId].tSubCursorPos = 0;
        gTasks[taskId].func = Task_PokePvPStartMatchSubmenu;
    }
    else if (JOY_NEW(DPAD_UP) && gTasks[taskId].tSubCursorPos > 0)
    {
        gTasks[taskId].tSubCursorPos--;
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_TEAM_SLOTS - 1)
    {
        gTasks[taskId].tSubCursorPos++;
    }
}

// POKEPVP (ADR-093): one team-slot row. Shows the slot number and how many
// members it holds -- enough to tell the slots apart at a glance without a
// naming screen, which is real work (naming_screen.c) and its own decision.
static void DrawOneTeamRow(u8 windowId, u8 slot)
{
    u8 buf[24];
    u8 *dest;
    u8 count;

    count = PokePvPTeamBuilder_MemberCount(slot);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(10));
    dest = StringCopy(buf, sText_Team);
    dest = ConvertIntToDecimalStringN(dest, slot + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
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
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, sTextColor1, -1, buf);
    MainMenu_DrawWindow(&sWindowTemplate[windowId]);
    PutWindowTilemap(windowId);
}

// POKEPVP (ADR-093): five team slots in the five existing windows -- the
// list is exactly as long as the window row count, so B (not a sixth
// "EXIT" row) is the way out, matching the START MATCH submenu's own
// convention rather than inventing a second one.
static void DrawTeamListItems(void)
{
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_0, 0);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_1, 1);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_2, 2);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_3, 3);
    DrawOneTeamRow(MAIN_MENU_WINDOW_POKEPVP_4, 4);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_0, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_1, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_2, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_3, COPYWIN_GFX);
    CopyWindowToVram(MAIN_MENU_WINDOW_POKEPVP_4, COPYWIN_FULL);
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
        DrawSlotMenuItems();
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
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < POKEPVP_TEAM_SLOTS - 1)
    {
        gTasks[taskId].tSubCursorPos++;
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

// POKEPVP (ADR-093): the per-slot submenu. Three items in the same five
// windows every other menu here uses.
static void DrawSlotMenuItems(void)
{
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_0, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_1, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_2, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_3, PIXEL_FILL(10));
    FillWindowPixelBuffer(MAIN_MENU_WINDOW_POKEPVP_4, PIXEL_FILL(10));
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_0, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_EditTeam);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_1, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_EditMoves);
    AddTextPrinterParameterized3(MAIN_MENU_WINDOW_POKEPVP_2, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_Back);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_0]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_1]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_2]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_3]);
    MainMenu_DrawWindow(&sWindowTemplate[MAIN_MENU_WINDOW_POKEPVP_4]);
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
}

// POKEPVP (ADR-093): EDIT TEAM / EDIT MOVES / BACK for one team slot.
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
        default:
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            gTasks[taskId].func = Task_PokePvPReturnToTeamListFromSlotMenu;
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
    }
    else if (JOY_NEW(DPAD_DOWN) && gTasks[taskId].tSubCursorPos < 2)
    {
        gTasks[taskId].tSubCursorPos++;
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
    DrawTeamListItems();
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
            DrawSlotMenuItems();
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

// POKEPVP (ADR-096): Type/Power/Accuracy/PP for one move, or three-hyphen
// placeholders for MOVE_NONE (an empty move slot, or the movepool list's
// own "clear this slot" row) -- the same "read gBattleMoves[0] as if it
// were a real move" mistake would otherwise be silent and wrong rather
// than a crash, so MOVE_NONE is handled explicitly instead of falling
// through.
static void DrawPokePvPMoveInfo(u8 windowId, u16 move)
{
    u8 buf[8];
    u8 *dest;
    u8 typeValueX;
    u8 statValueX;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(10));

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
    // had to fix once.
    typeValueX = 2 + GetStringWidth(FONT_NORMAL, sText_MoveInfoType, 0) + 4;
    statValueX = 2 + GetStringWidth(FONT_NORMAL, sText_MoveInfoPower, 0) + 4;

    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 2, sTextColor1, -1, sText_MoveInfoType);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 18, sTextColor1, -1, sText_MoveInfoPower);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 34, sTextColor1, -1, sText_MoveInfoAcc);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 2, 50, sTextColor1, -1, sText_MoveInfoPP);

    if (move == MOVE_NONE)
    {
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, typeValueX, 2, sTextColor1, -1, gText_ThreeHyphens);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 18, sTextColor1, -1, gText_ThreeHyphens);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 34, sTextColor1, -1, gText_ThreeHyphens);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 50, sTextColor1, -1, gText_ThreeHyphens);
        CopyWindowToVram(windowId, COPYWIN_GFX);
        return;
    }

    AddTextPrinterParameterized3(windowId, FONT_NORMAL, typeValueX, 2, sTextColor1, -1, gTypeNames[gBattleMoves[move].type]);

    if (gBattleMoves[move].power < 2)
        dest = StringCopy(buf, gText_ThreeHyphens);
    else
        dest = ConvertIntToDecimalStringN(buf, gBattleMoves[move].power, STR_CONV_MODE_LEFT_ALIGN, 3);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 18, sTextColor1, -1, buf);

    if (gBattleMoves[move].accuracy == 0)
        dest = StringCopy(buf, gText_ThreeHyphens);
    else
        dest = ConvertIntToDecimalStringN(buf, gBattleMoves[move].accuracy, STR_CONV_MODE_LEFT_ALIGN, 3);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 34, sTextColor1, -1, buf);

    dest = ConvertIntToDecimalStringN(buf, gBattleMoves[move].pp, STR_CONV_MODE_LEFT_ALIGN, 2);
    *dest = EOS;
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, statValueX, 50, sTextColor1, -1, buf);

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
    DrawPokePvPMoveInfo(gTasks[taskId].tMoveInfoWindowId, move);
}

static void ReturnToSlotMenu(u8 taskId)
{
    ClosePokePvPList(taskId);
    if (sPokePvPList != NULL)
    {
        Free(sPokePvPList);
        sPokePvPList = NULL;
    }
    DrawSlotMenuItems();
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

    DrawPokePvPMenuItems();
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
        // POKEPVP (ADR-085): 5 equal 32px slots covering the full screen,
        // matching sWindowTemplate's POKEPVP_0..4 tilemapTop spacing.
        win0vTop = (cursorPos * 0x20) << 8;
        win0vBot = (cursorPos + 1) * 0x20;
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

static void LoadUserFrameToBg(u8 bgId)
{
    LoadBgTiles(bgId, GetUserWindowGraphics(gSaveBlock2Ptr->optionsWindowFrameType)->tiles, 0x120, 0x1B1);
    LoadPalette(GetUserWindowGraphics(gSaveBlock2Ptr->optionsWindowFrameType)->palette, BG_PLTT_ID(2), PLTT_SIZE_4BPP);
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
}

static void SetStdFrame0OnBg(u8 bgId)
{
    LoadStdWindowGfx(MAIN_MENU_WINDOW_NEWGAME_ONLY, 0x1B1, BG_PLTT_ID(2));
    MainMenu_EraseWindow(&sWindowTemplate[MAIN_MENU_WINDOW_ERROR]);
}

static void MainMenu_DrawWindow(const struct WindowTemplate * windowTemplate)
{
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B1, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop - 1,
        1,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B2, 
        windowTemplate->tilemapLeft, 
        windowTemplate->tilemapTop - 1, 
        windowTemplate->width, 
        windowTemplate->height, 
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B3, 
        windowTemplate->tilemapLeft + 
        windowTemplate->width, 
        windowTemplate->tilemapTop - 1,
        1,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B4, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop,
        1, 
        windowTemplate->height,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B6, 
        windowTemplate->tilemapLeft + 
        windowTemplate->width, 
        windowTemplate->tilemapTop,
        1, 
        windowTemplate->height,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B7, 
        windowTemplate->tilemapLeft - 1, 
        windowTemplate->tilemapTop + 
        windowTemplate->height,
        1,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B8, 
        windowTemplate->tilemapLeft, 
        windowTemplate->tilemapTop + 
        windowTemplate->height, 
        windowTemplate->width,
        1,
        2
    );
    FillBgTilemapBufferRect(
        windowTemplate->bg, 
        0x1B9, 
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
