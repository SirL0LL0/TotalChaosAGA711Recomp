/* amiga_layers.c - layers.library (V40) in C: sul Kickstart sta in ROM.
 *
 * I layer veri del runner stanno dentro amiga_intuition.c (finestre, backing
 * store, coperture); qui c'e' quel che chiedono le librerie del Workbench
 * 3.1 (asl.library la apre all'init): i lock non servono (un solo task
 * disegna alla volta), la clip region si ricorda nel Layer (lr_ClipRegion,
 * +84 nella V39) senza ritagliare, BeginUpdate dice sempre di si'. Le altre
 * funzioni, se qualcuno le chiama, le segnala os_manca.
 */
#include <stdio.h>
#include "genesis_runtime.h"
#include "amiga_os.h"

#define LR_CLIPREGION 84

static void l_niente(void) {}
static void l_vero(void) { D0 = 1; }
static void l_installclipregion(void) {
    /* InstallClipRegion(layer A0, region A1): la vecchia */
    if (!A0) { D0 = 0; return; }
    D0 = R32(A0 + LR_CLIPREGION);
    W32(A0 + LR_CLIPREGION, A1);
}
static void l_whichlayer(void) { D0 = 0; }

#define F(lvo, fn) [(lvo) / 6 - 1] = fn
#define N(lvo, nome) [(lvo) / 6 - 1] = nome
static const os_funzione s_fn[36] = {
    F(30, l_niente), F(78, l_vero), F(84, l_niente), F(96, l_niente), F(102, l_niente),
    F(108, l_niente), F(114, l_niente), F(120, l_niente), F(132, l_whichlayer), F(138, l_niente),
    F(174, l_installclipregion),
};
static const char *const s_nomi[36] = {
    N(6, "Open"), N(12, "Close"), N(18, "Expunge"), N(24, "Reserved"),
    N(30, "InitLayers"), N(36, "CreateUpfrontLayer"), N(42, "CreateBehindLayer"), N(48, "UpfrontLayer"),
    N(54, "BehindLayer"), N(60, "MoveLayer"), N(66, "SizeLayer"), N(72, "ScrollLayer"),
    N(78, "BeginUpdate"), N(84, "EndUpdate"), N(90, "DeleteLayer"), N(96, "LockLayer"),
    N(102, "UnlockLayer"), N(108, "LockLayers"), N(114, "UnlockLayers"), N(120, "LockLayerInfo"),
    N(126, "SwapBitsRastPortClipRect"), N(132, "WhichLayer"), N(138, "UnlockLayerInfo"),
    N(144, "NewLayerInfo"), N(150, "DisposeLayerInfo"), N(156, "FattenLayerInfo"),
    N(162, "ThinLayerInfo"), N(168, "MoveLayerInFrontOf"), N(174, "InstallClipRegion"),
    N(180, "MoveSizeLayer"), N(186, "CreateUpfrontHookLayer"), N(192, "CreateBehindHookLayer"),
    N(198, "InstallLayerHook"), N(204, "InstallLayerInfoHook"), N(210, "SortLayerCR"),
    N(216, "DoHookClipRects"),
};

void os_layers_init(void) {
    static const os_libreria_c def = { "layers.library", 40, 36, 34, s_nomi, s_fn };
    os_crea_libreria(&def, NT_LIBRARY);
}
