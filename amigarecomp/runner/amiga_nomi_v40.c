/* amiga_nomi_v40.c - le funzioni aggiunte da Kickstart 2.0 a 3.1 (V36-V40)
 * in exec, dos, graphics e intuition: quante voci ha la tabella dei salti e
 * come si chiamano quelle oltre la 1.3. Le tabelle in C delle librerie
 * coprono la 1.3; os_crea_libreria allunga la tabella dei salti fino alla
 * V40 e dichiara la versione 40, cosi' le librerie del Workbench 3.1 si
 * aprono e una funzione nuova non ancora scritta si vede per nome
 * ("manca graphics.library/LoadRGB32") invece di saltare nel vuoto. */
#include <string.h>

typedef struct {
    const char *libreria;
    int primo_lvo;                    /* LVO del primo nome qui sotto */
    int ultimo_lvo;                   /* l'ultima voce della V40 */
    const char *const *nomi;          /* un nome ogni 6 byte da primo_lvo, NULL = privata */
    int n_nomi;
} tabella_v40_t;

static const char *const s_exec[] = {
    "ChildFree", "ChildOrphan", "ChildStatus", "ChildWait", "CachePreDMA", "CachePostDMA",
    "AddMemHandler", "RemMemHandler", "ObtainQuickVector",
};

static const char *const s_dos[] = {
    "AllocDosObject", "FreeDosObject", "DoPkt", "SendPkt", "WaitPkt", "ReplyPkt", "AbortPkt",
    "LockRecord", "LockRecords", "UnLockRecord", "UnLockRecords", "SelectInput", "SelectOutput",
    "FGetC", "FPutC", "UnGetC", "FRead", "FWrite", "FGets", "FPuts", "VFWritef", "VFPrintf",
    "Flush", "SetVBuf", "DupLockFromFH", "OpenFromLock", "ParentOfFH", "ExamineFH",
    "SetFileDate", "NameFromLock", "NameFromFH", "SplitName", "SameLock", "SetMode", "ExAll",
    "ReadLink", "MakeLink", "ChangeMode", "SetFileSize", "SetIoErr", "Fault", "PrintFault",
    "ErrorReport", NULL, "Cli", "CreateNewProc", "RunCommand", "GetConsoleTask",
    "SetConsoleTask", "GetFileSysTask", "SetFileSysTask", "GetArgStr", "SetArgStr",
    "FindCliProc", "MaxCli", "SetCurrentDirName", "GetCurrentDirName", "SetProgramName",
    "GetProgramName", "SetPrompt", "GetPrompt", "SetProgramDir", "GetProgramDir",
    "SystemTagList", "AssignLock", "AssignLate", "AssignPath", "AssignAdd", "RemAssignList",
    "GetDeviceProc", "FreeDeviceProc", "LockDosList", "UnLockDosList", "AttemptLockDosList",
    "RemDosEntry", "AddDosEntry", "FindDosEntry", "NextDosEntry", "MakeDosEntry",
    "FreeDosEntry", "IsFileSystem", "Format", "Relabel", "Inhibit", "AddBuffers",
    "CompareDates", "DateToStr", "StrToDate", "InternalLoadSeg", "InternalUnLoadSeg",
    "NewLoadSeg", "AddSegment", "FindSegment", "RemSegment", "CheckSignal", "ReadArgs",
    "FindArg", "ReadItem", "StrToLong", "MatchFirst", "MatchNext", "MatchEnd", "ParsePattern",
    "MatchPattern", NULL, "FreeArgs", NULL, "FilePart", "PathPart", "AddPart", "StartNotify",
    "EndNotify", "SetVar", "GetVar", "DeleteVar", "FindVar", NULL, "CliInitNewcli",
    "CliInitRun", "WriteChars", "PutStr", "VPrintf", NULL, "ParsePatternNoCase",
    "MatchPatternNoCase", NULL, "SameDevice", "ExAllEnd", "SetOwner",
};

static const char *const s_graphics[] = {
    "OrRegionRegion", "XorRegionRegion", "AndRegionRegion", "SetRGB4CM", "BltMaskBitMapRastPort",
    NULL, NULL, "AttemptLockLayerRom", "GfxNew", "GfxFree", "GfxAssociate", "BitMapScale",
    "ScalerDiv", "TextExtent", "TextFit", "GfxLookUp", "VideoControl", "OpenMonitor",
    "CloseMonitor", "FindDisplayInfo", "NextDisplayInfo", NULL, NULL, NULL,
    "GetDisplayInfoData", "FontExtent", "ReadPixelLine8", "WritePixelLine8", "ReadPixelArray8",
    "WritePixelArray8", "GetVPModeID", "ModeNotAvailable", "WeighTAMatch", "EraseRect",
    "ExtendFont", "StripFont", "CalcIVG", "AttachPalExtra", "ObtainBestPenA", NULL, "SetRGB32",
    "GetAPen", "GetBPen", "GetDrMd", "GetOutlinePen", "LoadRGB32", "SetChipRev",
    "SetABPenDrMd", "GetRGB32", NULL, NULL, "AllocBitMap", "FreeBitMap", "GetExtSpriteA",
    "CoerceMode", "ChangeVPBitMap", "ReleasePen", "ObtainPen", "GetBitMapAttr",
    "AllocDBufInfo", "FreeDBufInfo", "SetOutlinePen", "SetWriteMask", "SetMaxPen",
    "SetRGB32CM", "ScrollRasterBF", "FindColor", NULL, "AllocSpriteDataA", "ChangeExtSpriteA",
    "FreeSpriteData", "SetRPAttrsA", "GetRPAttrsA", "BestModeIDA", "WriteChunkyPixels",
};

static const char *const s_intuition[] = {
    "QueryOverscan", "MoveWindowInFrontOf", "ChangeWindowBox", "SetEditHook", "SetMouseQueue",
    "ZipWindow", "LockPubScreen", "UnlockPubScreen", "LockPubScreenList", "UnlockPubScreenList",
    "NextPubScreen", "SetDefaultPubScreen", "SetPubScreenModes", "PubScreenStatus",
    "ObtainGIRPort", "ReleaseGIRPort", "GadgetMouse", NULL, "GetDefaultPubScreen",
    "EasyRequestArgs", "BuildEasyRequestArgs", "SysReqHandler", "OpenWindowTagList",
    "OpenScreenTagList", "DrawImageState", "PointInImage", "EraseImage", "NewObjectA",
    "DisposeObject", "SetAttrsA", "GetAttr", "SetGadgetAttrsA", "NextObject", NULL, "MakeClass",
    "AddClass", "GetScreenDrawInfo", "FreeScreenDrawInfo", "ResetMenuStrip", "RemoveClass",
    "FreeClass", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "AllocScreenBuffer",
    "FreeScreenBuffer", "ChangeScreenBuffer", "ScreenDepth", "ScreenPosition",
    "ScrollWindowRaster", "LendMenus", "DoGadgetMethodA", "SetWindowPointerA",
    "TimedDisplayAlert", "HelpControl",
};

#define T(lib, primo, nomi) { lib, primo, primo + 6 * (int)(sizeof nomi / sizeof nomi[0] - 1), nomi, \
                              (int)(sizeof nomi / sizeof nomi[0]) }
static const tabella_v40_t s_tab[] = {
    T("exec.library", 738, s_exec),
    T("dos.library", 228, s_dos),
    T("graphics.library", 612, s_graphics),
    T("intuition.library", 474, s_intuition),
};

static const tabella_v40_t *tabella(const char *libreria) {
    for (size_t i = 0; i < sizeof s_tab / sizeof s_tab[0]; i++)
        if (!strcmp(s_tab[i].libreria, libreria)) return &s_tab[i];
    return NULL;
}

/* voci della tabella dei salti nella V40, 0 = libreria non in elenco */
int os_voci_v40(const char *libreria) {
    const tabella_v40_t *t = tabella(libreria);
    return t ? t->ultimo_lvo / 6 : 0;
}

/* il nome di una funzione V36-V40 (NULL se non e' qui o e' privata) */
const char *os_nome_v40(const char *libreria, int lvo) {
    const tabella_v40_t *t = tabella(libreria);
    if (!t || lvo < t->primo_lvo || (lvo - t->primo_lvo) % 6) return NULL;
    int i = (lvo - t->primo_lvo) / 6;
    return i < t->n_nomi ? t->nomi[i] : NULL;
}
