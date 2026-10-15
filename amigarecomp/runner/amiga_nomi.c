/* amiga_nomi.c - i nomi delle funzioni di graphics e intuition (V34), per
 * l'indice LVO / 6 - 1: servono ai messaggi delle funzioni mancanti e alla
 * traccia (AMIGA_TRACCIA_OS). */
#include <stddef.h>

const char *const os_nomi_graphics[101] = {
    "Open", "Close", "Expunge", "Reserved", "BltBitMap", "BltTemplate", "ClearEOL", "ClearScreen",
    "TextLength", "Text", "SetFont", "OpenFont", "CloseFont", "AskSoftStyle", "SetSoftStyle",
    "AddBob", "AddVSprite", "DoCollision", "DrawGList", "InitGels", "InitMasks", "RemIBob",
    "RemVSprite", "SetCollision", "SortGList", "AddAnimOb", "Animate", "GetGBuffers", "InitGMasks",
    "DrawEllipse", "AreaEllipse", "LoadRGB4", "InitRastPort", "InitVPort", "MrgCop", "MakeVPort",
    "LoadView", "WaitBlit", "SetRast", "Move", "Draw", "AreaMove", "AreaDraw", "AreaEnd", "WaitTOF",
    "QBlit", "InitArea", "SetRGB4", "QBSBlit", "BltClear", "RectFill", "BltPattern", "ReadPixel",
    "WritePixel", "Flood", "PolyDraw", "SetAPen", "SetBPen", "SetDrMd", "InitView", "CBump",
    "CMove", "CWait", "VBeamPos", "InitBitMap", "ScrollRaster", "WaitBOVP", "GetSprite",
    "FreeSprite", "ChangeSprite", "MoveSprite", "LockLayerRom", "UnlockLayerRom", "SyncSBitMap",
    "CopySBitMap", "OwnBlitter", "DisownBlitter", "InitTmpRas", "AskFont", "AddFont", "RemFont",
    "AllocRaster", "FreeRaster", "AndRectRegion", "OrRectRegion", "NewRegion", "ClearRectRegion",
    "ClearRegion", "DisposeRegion", "FreeVPortCopLists", "FreeCopList", "ClipBlit",
    "XorRectRegion", "FreeCprList", "GetColorMap", "FreeColorMap", "GetRGB4", "ScrollVPort",
    "UCopperListInit", "FreeGBuffers", "BltBitMapRastPort",
};

const char *const os_nomi_intuition[78] = {
    "Open", "Close", "Expunge", "Reserved", "OpenIntuition", "Intuition", "AddGadget",
    "ClearDMRequest", "ClearMenuStrip", "ClearPointer", "CloseScreen", "CloseWindow",
    "CloseWorkBench", "CurrentTime", "DisplayAlert", "DisplayBeep", "DoubleClick", "DrawBorder",
    "DrawImage", "EndRequest", "GetDefPrefs", "GetPrefs", "InitRequester", "ItemAddress",
    "ModifyIDCMP", "ModifyProp", "MoveScreen", "MoveWindow", "OffGadget", "OffMenu", "OnGadget",
    "OnMenu", "OpenScreen", "OpenWindow", "OpenWorkBench", "PrintIText", "RefreshGadgets",
    "RemoveGadget", "ReportMouse", "Request", "ScreenToBack", "ScreenToFront", "SetDMRequest",
    "SetMenuStrip", "SetPointer", "SetWindowTitles", "ShowTitle", "SizeWindow", "ViewAddress",
    "ViewPortAddress", "WindowToBack", "WindowToFront", "WindowLimits", "SetPrefs",
    "IntuiTextLength", "WBenchToBack", "WBenchToFront", "AutoRequest", "BeginRefresh",
    "BuildSysRequest", "EndRefresh", "FreeSysRequest", "MakeScreen", "RemakeDisplay",
    "RethinkDisplay", "AllocRemember", "AlohaWorkbench", "FreeRemember", "LockIBase",
    "UnlockIBase", "GetScreenData", "RefreshGList", "AddGList", "RemoveGList", "ActivateWindow",
    "RefreshWindowFrame", "ActivateGadget", "NewModifyProp",
};
