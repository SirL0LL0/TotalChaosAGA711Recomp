/* amiga_glue.c - l'ABI che il C generato da m68k-recomp-core si aspetta,
 * implementata per un programma AmigaOS, e il ciclo dei quadri del runner.
 *
 * Il programma non programma l'hardware: chiama AmigaOS (amiga_os.c e le
 * librerie in C). Il tempo scorre in due modi: quando il programma aspetta
 * (Wait, WaitTOF, Delay) il sistema fa girare i quadri finche' serve; quando
 * calcola senza aspettare, il C generato somma i cicli a g_cycle_accumulator
 * e chiama glue_check_vblank() a ogni quadro di CPU. Un quadro: ingressi,
 * interrupt del programma (VBlank, timer CIA), device, Paula, video, 50 Hz.
 *
 * Uso: <gioco>.exe [cartella dei dati]
 *   AMIGA_QUADRI=n        senza finestra: n quadri, poi salva quadro.bmp ed esce
 *   AMIGA_TRACCIA_OS=1    ogni chiamata alle librerie su stderr
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "genesis_runtime.h"
#include "game_spec.h"
#include "amiga_hw.h"
#include "amiga_os.h"
#include "amiga_debug.h"
#include "amiga_paula.h"
#include "amiga_video_gl.h"
#include "amiga_ingressi.h"
#include "amiga_console.h"
#include "amiga_menu.h"

M68KState g_cpu;
uint64_t  g_native_insn_count;
uint32_t  g_cycle_accumulator;
uint32_t  g_vblank_threshold = AMIGA_CICLI_QUADRO;
uint32_t  g_amiga_cpu_x = 1;
uint32_t  g_audio_cycle_counter;
uint64_t  g_os_quadri;
double    g_amiga_fps;                       /* quadri al secondo (debug) */
uint32_t  os_fine_programma;
static int s_rte_pending;
int *g_rte_pending_ptr = &s_rte_pending;

#define CPU_A4000 12u                        /* 68040 a 25 MHz contro 68000 a 7,09 */

static void imposta_cpu(int a4000) {
    uint32_t x = a4000 ? CPU_A4000 : 1u;
    g_cycle_accumulator = g_cycle_accumulator / g_amiga_cpu_x * x;
    g_amiga_cpu_x = x;
    g_vblank_threshold = AMIGA_CICLI_QUADRO * x;
}

static long s_quadri_max;
static int s_senza_finestra;
static SDL_Window *s_win;
static SDL_AudioDeviceID s_audio;
static uint64_t s_prossimo_quadro;
static int s_gl;

/* il video del sistema (amiga_intuition.c): lo schermo davanti, ARGB */
const uint32_t *os_video(int *larghezza, int *altezza);
/* gli ingressi verso Intuition (amiga_intuition.c): mouse nel video, -1 = fermo */
void os_intuition_mouse(int x, int y, int sinistro, int destro);
void os_intuition_tasto(int codice_raw, int rilasciato);
void os_intuition_quadro(void);
void os_exec_vblank(void);
void os_device_quadro(void);
void os_task_quadro(void);
void os_dos_processo(void);

/* ------------------------------------------------------------ memoria */

/* AMIGA_GUARDIA=indirizzo esadecimale: ogni scrittura sui suoi 4 byte, con
 * l'ultima chiamata del programma (per trovare chi sporca una variabile) */
static uint32_t s_guardia = 0xFFFFFFFFu, s_ultima_chiamata;
static void guardia(uint32_t a, int n, uint32_t v) {
    if (a + (uint32_t)n <= s_guardia || a >= s_guardia + 4) return;
    fprintf(stderr, "[guardia] $%06X <- %0*X (%d byte) quadro %llu, ultima chiamata ritorna a $%06X, A7=$%06X\n",
            a, n * 2, v, n, (unsigned long long)g_os_quadri, s_ultima_chiamata, g_cpu.A[7]);
}

/* la guardia anche per le scritture del runner (primitive grafiche, layer):
 * chi = la funzione che scrive */
void os_guardia_c(uint32_t a, const char *chi) {
    if (s_guardia == 0xFFFFFFFFu || a < s_guardia || a >= s_guardia + 4) return;
    fprintf(stderr, "[guardia] $%06X scritto da %s (runner) quadro %llu, task $%06X, ultima chiamata ritorna a $%06X\n",
            a, chi, (unsigned long long)g_os_quadri, os_task, s_ultima_chiamata);
}

uint8_t m68k_read8(uint32_t a) {
    a &= 0xFFFFFFu;
    return a < g_amiga_mem_size ? g_amiga_mem[a] : amiga_hw_read8(a);
}
uint16_t m68k_read16(uint32_t a) {
    a &= 0xFFFFFFu;
    if (a + 1 < g_amiga_mem_size) return (uint16_t)(g_amiga_mem[a] << 8 | g_amiga_mem[a + 1]);
    return amiga_hw_read16(a);
}
uint32_t m68k_read32(uint32_t a) {
    return (uint32_t)m68k_read16(a) << 16 | m68k_read16(a + 2);
}
void m68k_write8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFFu;
    if (s_guardia != 0xFFFFFFFFu) guardia(a, 1, v);
    if (a < g_amiga_mem_size) {
        if (amiga_debug_osservato(a)) amiga_debug_scrittura(a, 1, g_amiga_mem[a], v);
        g_amiga_mem[a] = v;
    } else amiga_hw_write8(a, v);
}
void m68k_write16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFFu;
    if (s_guardia != 0xFFFFFFFFu) guardia(a, 2, v);
    if (a + 1 < g_amiga_mem_size) {
        if (amiga_debug_osservato(a) || amiga_debug_osservato(a + 1))
            amiga_debug_scrittura(a, 2, (uint32_t)(g_amiga_mem[a] << 8 | g_amiga_mem[a + 1]), v);
        g_amiga_mem[a] = (uint8_t)(v >> 8); g_amiga_mem[a + 1] = (uint8_t)v;
    }
    else amiga_hw_write16(a, v);
}
void m68k_write32(uint32_t a, uint32_t v) {
    m68k_write16(a, (uint16_t)(v >> 16));
    m68k_write16(a + 2, (uint16_t)v);
}

/* ------------------------------------------------------------ pila e salti */

static FILE *s_traccia_chiamate;              /* AMIGA_TRACCIA_CHIAMATE=file */

/* la pila ombra degli slot delle JSR attive (vedi recomp_rts): una per task
 * (amiga_task.c la cambia con glue_pila_ombra) */
#define MAX_SLOT 8192
typedef struct { uint32_t slot[MAX_SLOT]; int n; } pila_ombra_t;
static pila_ombra_t s_pila_principale, *s_pila = &s_pila_principale;
#define s_slot (s_pila->slot)
#define s_n_slot (s_pila->n)

void *glue_pila_ombra_nuova(void) { return calloc(1, sizeof(pila_ombra_t)); }
void *glue_pila_ombra(void *nuova) {
    pila_ombra_t *vecchia = s_pila;
    if (nuova) s_pila = (pila_ombra_t *)nuova;
    return vecchia;
}
int *glue_rte_pending(void) { return &s_rte_pending; }

void recomp_push_return(uint32_t ret_addr) {
    amiga_debug_chiamata(ret_addr & 0xFFFFFFu, g_cpu.A[7]);
    s_ultima_chiamata = ret_addr & 0xFFFFFFu;
    if (s_traccia_chiamate)
        fprintf(s_traccia_chiamate, "q%llu rit=%06X A7=%06X D0=%08X D2=%08X A0=%06X A4=%06X\n", (unsigned long long)g_os_quadri,
                ret_addr & 0xFFFFFFu, g_cpu.A[7], g_cpu.D[0], g_cpu.D[2], g_cpu.A[0], g_cpu.A[4]);
    g_cpu.A[7] -= 4;
    m68k_write32(g_cpu.A[7], ret_addr & 0xFFFFFFu);
    if (s_n_slot < MAX_SLOT) s_slot[s_n_slot] = g_cpu.A[7];
    s_n_slot++;
}

void recomp_pop_slot(void) { if (s_n_slot > 0) s_n_slot--; }

/* RTS: l'indirizzo di ritorno sta in (A7). Di solito e' lo slot della JSR
 * piu' interna; se la routine ha rimesso un puntatore di pila salvato prima
 * (longjmp, exit, l'uscita d'errore di una routine assembler), e' lo slot di
 * una JSR piu' esterna: si srotolano le chiamate in mezzo */
/* gli slot dei ritorni spinti a mano (sotto) hanno il bit 0 a 1: valgono come
 * punto d'arrivo di un RTS, ma non sono chiamate da srotolare (il codice qui
 * sotto ne esce da solo) */
#define SLOT(i) (s_slot[i] & ~1u)
#define A_MANO(i) (s_slot[i] & 1u)
void recomp_rts(void) {
    uint32_t a7 = g_cpu.A[7] & 0xFFFFFFu;
    int top = s_n_slot < MAX_SLOT ? s_n_slot : MAX_SLOT;
    if (!top || SLOT(top - 1) == a7) return;
    /* sotto lo slot della JSR: un indirizzo di ritorno spinto a mano (pea
     * indirizzo; jmp routine). Come il 68000: lo si toglie e si continua da
     * li'; quel codice finira' con l'RTS della JSR vera */
    if (a7 < SLOT(top - 1)) {
        uint32_t r = m68k_read32(a7) & 0xFFFFFFu;
        g_cpu.A[7] += 4;
        /* sotto c'e' il ritorno spinto a mano (lo stub delle librerie del
         * gioco: pea ritorno; base+LVO in pila; rts): e' lo slot di questa
         * chiamata. Cosi' un RTS che ci arriva da molto piu' in basso (il
         * file requester di req.library esce rimettendo un A7 salvato)
         * trova la chiamata e srotola quelle in mezzo */
        uint32_t slot = g_cpu.A[7] & 0xFFFFFFu;
        int n = s_n_slot;
        if (s_n_slot < MAX_SLOT) s_slot[s_n_slot] = slot | 1u;
        s_n_slot++;
        call_by_address(r);
        s_n_slot = n;
        /* tornata allo slot: come la CPU, si prosegue all'indirizzo che c'e' */
        if (os_traccia)
            fprintf(stderr, "[cpu] ritorno a mano: $%06X tornata con A7=$%06X (slot $%06X) D0=%08X rte=%d\n",
                    r, g_cpu.A[7], slot, g_cpu.D[0], g_rte_pending);
        if (!g_rte_pending && (g_cpu.A[7] & 0xFFFFFFu) == slot && n > 0 && slot < SLOT(n - 1 < MAX_SLOT ? n - 1 : MAX_SLOT - 1))
            recomp_rts();
        return;
    }
    int n = 0, sotto = 0, i = top - 1;
    while (i >= 0 && SLOT(i) < a7) { if (!A_MANO(i)) n++; sotto++; i--; }
    if (sotto && i >= 0 && SLOT(i) == a7) {
        if (os_traccia) fprintf(stderr, "[cpu] RTS a $%06X salta %d chiamate (D0=%08X)\n", a7, n, g_cpu.D[0]);
        g_rte_pending = n;                 /* 0: solo livelli a mano, escono da soli */
        return;
    }
    if (sotto) fprintf(stderr, "[cpu] RTS con A7=$%06X fra gli slot (dentro $%06X): nessuna chiamata corrisponde\n",
                       a7, SLOT(top - 1));
}

uint32_t recomp_resolve_ram_trampoline(uint32_t addr) { return addr & 0xFFFFFFu; }

int genesis_game_instruction_hook(uint32_t pc) {
    return g_game_spec.instruction_hook ? g_game_spec.instruction_hook(pc) : 0;
}

/* quando il programma si ferma per un errore: l'ultima immagine, la memoria
 * e i registri accanto all'eseguibile, per capire dove era */
static void salva_bmp(const char *nome);
static void salva_emergenza(const char *perche) {
    char *qui = SDL_GetBasePath();
    char p[1100];
    snprintf(p, sizeof p, "%serrore_quadro.bmp", qui ? qui : "");
    salva_bmp(p);
    snprintf(p, sizeof p, "%serrore_memoria.bin", qui ? qui : "");
    FILE *m = fopen(p, "wb");
    if (m) { fwrite(g_amiga_mem, 1, g_amiga_mem_size, m); fclose(m); }
    SDL_free(qui);
    fprintf(stderr, "[errore] %s al quadro %llu, task $%06X\n", perche, (unsigned long long)g_os_quadri, os_task);
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "  D%d=%08X A%d=%08X\n", i, g_cpu.D[i], i, g_cpu.A[i]);
    fprintf(stderr, "  salvati errore_quadro.bmp ed errore_memoria.bin\n");
    fflush(stderr);
}

void genesis_log_dispatch_miss(uint32_t addr) {
    FILE *f = fopen("dispatch_misses.toml", "w");
    if (f) {
        fprintf(f, "# salto a codice non tradotto (quadro %llu)\n[functions]\nextra = [0x%06X]\n",
                (unsigned long long)g_os_quadri, addr & 0xFFFFFFu);
        fclose(f);
    }
    fprintf(stderr, "[DISPATCH MISS] $%06X al quadro %llu (A7=$%06X): vedi dispatch_misses.toml\n",
            addr & 0xFFFFFFu, (unsigned long long)g_os_quadri, g_cpu.A[7]);
    salva_emergenza("salto a codice non tradotto");
    exit(2);
}

/* le tabelle di salto che il ricompilatore non ha risolto: un salto a
 * codice non tradotto, come sopra */
void hybrid_jmp_interpret(uint32_t target_pc) { call_by_address(target_pc); }
void hybrid_call_interpret(uint32_t target_pc) { genesis_log_dispatch_miss(target_pc); }

void m68k_trap_vector(uint8_t vec) {
    fprintf(stderr, "[cpu] trap, vettore %u, al quadro %llu\n", vec, (unsigned long long)g_os_quadri);
    salva_emergenza("trap");
    exit(3);
}
void m68k_illegal_trap(uint32_t pc, uint16_t opcode) {
    fprintf(stderr, "[cpu] istruzione illegale $%04X a $%06X\n", opcode, pc);
    salva_emergenza("istruzione illegale");
    exit(3);
}
void genesis_reset_devices(void) {}
void genesis_stop_until_interrupt(uint16_t sr) { (void)sr; if (os_quadro) os_quadro(); }
uint32_t m68k_movec_read(uint16_t c) { (void)c; return 0; }
void m68k_movec_write(uint16_t c, uint32_t v) { (void)c; (void)v; }

/* ------------------------------------------------------------ quadro */

static int codice_amiga(SDL_Keycode k) {
    static const char r1[] = "1234567890", rq[] = "qwertyuiop", ra[] = "asdfghjkl", rz[] = "zxcvbnm";
    const char *p;
    if (k < 128 && k > 0) {
        if ((p = strchr(r1, (int)k)) != NULL) return 0x01 + (int)(p - r1);
        if ((p = strchr(rq, (int)k)) != NULL) return 0x10 + (int)(p - rq);
        if ((p = strchr(ra, (int)k)) != NULL) return 0x20 + (int)(p - ra);
        if ((p = strchr(rz, (int)k)) != NULL) return 0x31 + (int)(p - rz);
    }
    switch (k) {
    case SDLK_SPACE: return 0x40; case SDLK_BACKSPACE: return 0x41; case SDLK_TAB: return 0x42;
    case SDLK_RETURN: return 0x44; case SDLK_ESCAPE: return 0x45; case SDLK_DELETE: return 0x46;
    case SDLK_UP: return 0x4C; case SDLK_DOWN: return 0x4D; case SDLK_RIGHT: return 0x4E; case SDLK_LEFT: return 0x4F;
    case SDLK_F1: return 0x50; case SDLK_F2: return 0x51; case SDLK_F3: return 0x52;
    case SDLK_F4: return 0x53; case SDLK_F5: return 0x54; case SDLK_F6: return 0x55;
    case SDLK_F7: return 0x56; case SDLK_F8: return 0x57; case SDLK_F9: return 0x58; case SDLK_F10: return 0x59;
    case SDLK_MINUS: return 0x0B; case SDLK_EQUALS: return 0x0C; case SDLK_PERIOD: return 0x39;
    case SDLK_COMMA: return 0x38; case SDLK_SLASH: return 0x3A; case SDLK_SEMICOLON: return 0x29;
    case SDLK_LSHIFT: return 0x60; case SDLK_RSHIFT: return 0x61; case SDLK_LCTRL: return 0x63;
    case SDLK_LALT: return 0x64; case SDLK_RALT: return 0x65;
    }
    return -1;
}

/* il mouse del PC nel video Amiga: la finestra mostra il quadro nel
 * rettangolo centrato di amiga_video_gl_rettangolo */
static void mouse_nel_video(int wx, int wy, int *x, int *y) {
    int W, H, vw, vh, qw, qh;
    os_video(&qw, &qh);
    SDL_GetWindowSize(s_win, &W, &H);
    amiga_video_gl_rettangolo(SCHERMO_NITIDO, qh, 1, ASPETTO_PAL, W, H, &vw, &vh);
    if (vw <= 0 || vh <= 0) { *x = *y = 0; return; }
    *x = (wx - (W - vw) / 2) * qw / vw;
    *y = (wy - (H - vh) / 2) * qh / vh;
}

static int s_log_ingressi;                    /* --debug: gli ingressi nel registro */

/* il mouse catturato: all'avvio la finestra lo trattiene e si vede solo il
 * puntatore di Intuition. Tasto centrale o F12 lo rilasciano (torna il
 * cursore di Windows, il gioco non riceve piu' il mouse); un clic nella
 * finestra lo riprende, e quel clic non arriva al gioco */
static int s_catturato, s_ingoia_clic;
static void cattura_mouse(int si) {
    s_catturato = si;
    SDL_SetWindowGrab(s_win, si ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(si ? SDL_DISABLE : SDL_ENABLE);
}

static void ingressi_sdl(void) {
    static int sx, dx, mx = 320, my = 256;
    SDL_Event ev;
    if (s_log_ingressi) fflush(stderr);               /* il registro e' completo fino al quadro prima */
    uint32_t id = SDL_GetWindowID(s_win);
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) exit(0);
        if (amiga_debug_evento(&ev)) continue;
        if (ev.type == SDL_WINDOWEVENT && ev.window.windowID == id) {
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) exit(0);
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST && s_catturato) { cattura_mouse(0); sx = dx = 0; }
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F12 && !ev.key.repeat) {
            cattura_mouse(!s_catturato);
            sx = dx = 0;
            continue;
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.windowID == id && ev.button.button == SDL_BUTTON_MIDDLE) {
            cattura_mouse(0);
            sx = dx = 0;
            continue;
        }
        if ((ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) && ev.button.windowID == id && !s_catturato) {
            if (ev.type == SDL_MOUSEBUTTONDOWN) { cattura_mouse(1); s_ingoia_clic = 1; }
            continue;                                  /* il clic che riprende il mouse e' nostro */
        }
        if (ev.type == SDL_MOUSEBUTTONUP && s_ingoia_clic) { s_ingoia_clic = 0; continue; }
        if (ev.type == SDL_MOUSEMOTION && !s_catturato) continue;
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            if (!ev.key.repeat) {
                int c = codice_amiga(ev.key.keysym.sym);
                if (c >= 0) os_intuition_tasto(c, ev.type == SDL_KEYUP);
                if (s_log_ingressi)
                    fprintf(stderr, "[ingresso] q%llu tasto %s %s (raw $%02X)\n", (unsigned long long)g_os_quadri,
                            SDL_GetKeyName(ev.key.keysym.sym), ev.type == SDL_KEYUP ? "su" : "giu", c & 0xFF);
            }
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            int giu = ev.type == SDL_MOUSEBUTTONDOWN;
            if (s_log_ingressi)
                fprintf(stderr, "[ingresso] q%llu mouse %d %s a %d,%d del video\n", (unsigned long long)g_os_quadri,
                        ev.button.button, giu ? "giu" : "su", mx, my);
            if (ev.button.button == SDL_BUTTON_LEFT) sx = giu;
            if (ev.button.button == SDL_BUTTON_RIGHT) dx = giu;
        }
        if (ev.type == SDL_MOUSEMOTION) mouse_nel_video(ev.motion.x, ev.motion.y, &mx, &my);
    }
    /* --debug: anche dove va il mouse (una riga per quadro in cui si muove),
     * per rifare la partita con tools/riproduci.py */
    static int vx = -1, vy = -1;
    if (s_log_ingressi && (mx != vx || my != vy)) {
        fprintf(stderr, "[ingresso] q%llu mouse a %d,%d\n", (unsigned long long)g_os_quadri, mx, my);
        vx = mx; vy = my;
    }
    os_intuition_mouse(mx, my, sx, dx);
}

/* ingressi finti per i test senza finestra: AMIGA_INGRESSI="m:quadro,..."
 * (o AMIGA_INGRESSI=@file: la lista in un file, per le partite registrate
 * con --debug e ricostruite da tools/riproduci.py) */
static char s_ingressi_test[1 << 21];
/* la lista letta una volta: m clic (5 quadri), l/r sinistro/destro tenuti
 * (a = durata), p mouse in (a, b) del video 640 x 512, k tasto a */
typedef struct { char tipo; int a, b; long quando; } ingresso_t;
static ingresso_t *s_ing;
static int s_n_ing = -1;
static void leggi_ingressi(void) {
    int n = 1;
    for (const char *c = s_ingressi_test; *c; c++) n += *c == ',';
    s_ing = calloc((size_t)n, sizeof *s_ing);
    s_n_ing = 0;
    const char *p = s_ingressi_test;
    while (*p && s_ing) {
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;
        ingresso_t e = { *p++, 0, 0, 0 };
        if (e.tipo == 'k') e.a = (int)strtol(p, (char **)&p, 16);
        else if (e.tipo == 'r' || e.tipo == 'l' || e.tipo == 'p') e.a = (int)strtol(p, (char **)&p, 10);
        if (e.tipo == 'p' && *p == '.') e.b = (int)strtol(p + 1, (char **)&p, 10);
        if (*p == ':') e.quando = strtol(p + 1, (char **)&p, 10);
        if ((e.tipo == 'l' || e.tipo == 'r') && e.a <= 0) e.a = 5;
        if (e.tipo == 'm') { e.tipo = 'l'; e.a = 5; }
        s_ing[s_n_ing++] = e;
        while (*p && *p != ',') p++;
    }
}
static void ingressi_test(void) {
    if (s_n_ing < 0) leggi_ingressi();
    int sinistro = 0, destro = 0, px = -1, py = -1;
    long q = (long)g_os_quadri;
    for (int i = 0; i < s_n_ing; i++) {
        const ingresso_t *e = &s_ing[i];
        if (e->quando > q) continue;
        switch (e->tipo) {
        case 'l': if (q < e->quando + e->a) sinistro = 1; break;
        case 'r': if (q < e->quando + e->a) destro = 1; break;
        case 'p': px = e->a; py = e->b; break;            /* l'ultimo della lista gia' passato */
        case 'k':
            if (q == e->quando) os_intuition_tasto(e->a, 0);
            if (q == e->quando + 3) os_intuition_tasto(e->a, 1);
            break;
        }
    }
    os_intuition_mouse(px, py, sinistro, destro);
}

static void salva_bmp(const char *nome) {
    int w, h;
    const uint32_t *px = os_video(&w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)px, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(s, nome);
    SDL_FreeSurface(s);
}

static FILE *s_wav;                          /* AMIGA_WAV=file: registra l'audio (prove) */
static uint32_t s_wav_byte;

static void scarica_audio(void) {
    int16_t *c;
    int n = amiga_hw_audio(&c);
    if (s_wav && n) { fwrite(c, 4, (size_t)n, s_wav); s_wav_byte += (uint32_t)n * 4; }
    if (s_audio && n && SDL_GetQueuedAudioSize(s_audio) < 48000u * 4u / 5u)
        SDL_QueueAudio(s_audio, c, (Uint32)n * 4);
}

static void chiudi_wav(void) {
    if (!s_wav) return;
    uint32_t h[11] = { 0x46464952, 36 + s_wav_byte, 0x45564157, 0x20746D66, 16, 0x00020001,
                       48000, 48000 * 4, 0x00100004, 0x61746164, s_wav_byte };
    fseek(s_wav, 0, SEEK_SET);
    fwrite(h, 4, 11, s_wav);
    fclose(s_wav);
    s_wav = NULL;
}

static int s_nel_quadro;
static void quadro(void) {
    if (s_nel_quadro) return;                 /* un gestore che aspetta dentro il quadro */
    s_nel_quadro = 1;
    g_os_quadri++;
    /* AMIGA_TRACCIA_DA=n: la traccia delle chiamate solo dal quadro n (le
     * prove lunghe restano veloci fino al punto che interessa) */
    static long traccia_da = -2;
    if (traccia_da == -2) traccia_da = getenv("AMIGA_TRACCIA_DA") ? atol(getenv("AMIGA_TRACCIA_DA")) : -1;
    if (traccia_da >= 0 && (long)g_os_quadri == traccia_da) os_traccia = 1;
    if (s_senza_finestra) ingressi_test(); else ingressi_sdl();
    os_intuition_quadro();
    os_exec_vblank();
    os_device_quadro();
    os_task_quadro();
    amiga_debug_quadro(g_os_quadri);
    amiga_hw_fine_quadro();
    scarica_audio();
    s_nel_quadro = 0;
    if (s_senza_finestra) {
        /* AMIGA_FOTO=<da>-<a>/<passo>,<quadro>,...: foto_<quadro>.bmp lungo la
         * strada, per vedere piu' momenti di una partita in una sola prova */
        static const char *foto = (const char *)1;
        if (foto == (const char *)1) foto = getenv("AMIGA_FOTO");
        if (foto) {
            long q = (long)g_os_quadri;
            int scatta = 0;
            for (const char *p = foto; *p && !scatta; ) {
                char *f;
                long da = strtol(p, &f, 10), a = da, passo = 1;
                if (*f == '-') a = strtol(f + 1, &f, 10);
                if (*f == '/') passo = strtol(f + 1, &f, 10);
                if (passo < 1) passo = 1;
                if (q >= da && q <= a && (q - da) % passo == 0) scatta = 1;
                p = *f == ',' ? f + 1 : f;
                if (f == p && *p) break;
            }
            if (scatta) {
                char nome[64];
                snprintf(nome, sizeof nome, "foto_%ld.bmp", q);
                salva_bmp(nome);
            }
        }
        if ((long)g_os_quadri >= s_quadri_max) {
            extern void os_intuition_stato(void);
            os_intuition_stato();
            chiudi_wav();
            salva_bmp("quadro.bmp");
            FILE *m = fopen("memoria_fine.bin", "wb");
            if (m) { fwrite(g_amiga_mem, 1, g_amiga_mem_size, m); fclose(m); }
            extern uint64_t g_quadri_cpu;
            printf("quadri %llu (%llu finiti per cicli di CPU, gli altri per attesa), salvati quadro.bmp e memoria_fine.bin\n",
                   (unsigned long long)g_os_quadri, (unsigned long long)g_quadri_cpu);
            exit(0);
        }
        return;
    }
    int w, h;
    const uint32_t *px = os_video(&w, &h);
    if (s_gl) amiga_video_gl_presenta(px);
    /* 50 Hz PAL veri */
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t periodo = freq * AMIGA_CICLI_QUADRO / 7093790u;
    uint64_t ora = SDL_GetPerformanceCounter();
    if (!s_prossimo_quadro || ora > s_prossimo_quadro + 5 * periodo) s_prossimo_quadro = ora;
    s_prossimo_quadro += periodo;
    if (s_prossimo_quadro > ora) {
        uint64_t ms = (s_prossimo_quadro - ora) * 1000 / freq;
        if (ms > 1) SDL_Delay((Uint32)(ms - 1));
        while (SDL_GetPerformanceCounter() < s_prossimo_quadro) { }
    }
}

/* il programma calcola senza aspettare: un quadro ogni quadro di CPU; un
 * task secondario invece cede il posto al principale fino al quadro dopo */
int  os_task_secondario(void);
void os_task_attendi_quadro(void);
extern int g_verifica_attiva;
void amiga_verifica_init(void);
uint64_t g_quadri_cpu;                           /* quadri finiti perche' la CPU ha speso i suoi cicli */

/* un'attesa del programma principale senza nessun task pronto: invece di
 * saltare al quadro dopo, il tempo avanza a passi di 1/16 di quadro; a ogni
 * passo Paula suona fino a li' e le note finite rispondono subito. Cosi' un
 * gioco che aspetta la fine di un suono corto (Chaos anima i lanci a colpi
 * di note da pochi millisecondi) riparte quando il suono finisce, come
 * sull'Amiga, e non al quadro dopo. 0 = il quadro e' finito: tocca a lui */
void amiga_hw_audio_adesso(void);
void os_device_risposte_audio(void);
int os_attesa_breve(void) {
    uint32_t passo = g_vblank_threshold / 16;
    if (s_nel_quadro || g_cycle_accumulator + passo >= g_vblank_threshold) return 0;
    g_cycle_accumulator += passo;
    amiga_hw_audio_adesso();
    os_device_risposte_audio();
    return 1;
}
void glue_check_vblank(void) {
    if (g_verifica_attiva) return;               /* il quadro dopo il confronto */
    while (g_cycle_accumulator >= g_vblank_threshold) {
        g_cycle_accumulator -= g_vblank_threshold;
        g_quadri_cpu++;
        if (s_log_ingressi) fprintf(stderr, "[cpu] quadro %llu finito per cicli (task %s)\n",
                                    (unsigned long long)g_os_quadri, os_task_secondario() ? "secondario" : "principale");
        if (os_task_secondario()) os_task_attendi_quadro();
        else quadro();
    }
}
void glue_yield_for_interrupt_poll(void) {
    if (g_cycle_accumulator >= g_vblank_threshold) glue_check_vblank();
}
void glue_yield_for_vblank(void) { quadro(); }

/* quando il programma aspetta, il quadro riparte da zero cicli */
static void quadro_in_attesa(void) {
    g_cycle_accumulator = 0;
    quadro();
}

/* ------------------------------------------------------------ avvio */

int main(int argc, char **argv) {
    int debug = getenv("AMIGA_DEBUG") != NULL;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--debug")) debug = 1;
    amiga_console_prepara(debug);
    /* --debug: tutto in chaos_debug.log accanto all'eseguibile (chiamate al
     * sistema con registri e risultato, ingressi, messaggi IDCMP, file) */
    if (debug && !getenv("AMIGA_QUADRI")) {
        char *qui = SDL_GetBasePath();
        char p[1100];
        snprintf(p, sizeof p, "%s%s_debug.log", qui ? qui : "",
                 g_game_spec.short_name ? g_game_spec.short_name : "amiga");
        SDL_free(qui);
        if (freopen(p, "w", stderr)) setvbuf(stderr, NULL, _IOFBF, 1 << 16);
        _putenv("AMIGA_TRACCIA_OS=1");
        s_log_ingressi = 1;
        fprintf(stderr, "[debug] registro di %s\n", g_game_spec.display_name ? g_game_spec.display_name : "?");
    }

    void amiga_crash_init(void (*al_crash)(const char *));     /* amiga_crash.c */
    amiga_crash_init(salva_emergenza);

    const char *q = getenv("AMIGA_QUADRI");
    s_senza_finestra = q != NULL;
    s_quadri_max = q ? atol(q) : 0;
    if (getenv("AMIGA_INGRESSI")) {
        const char *e = getenv("AMIGA_INGRESSI");
        FILE *f = e[0] == '@' ? fopen(e + 1, "rb") : NULL;
        if (f) {
            size_t n = fread(s_ingressi_test, 1, sizeof s_ingressi_test - 1, f);
            s_ingressi_test[n] = 0;
            fclose(f);
            for (size_t i = 0; i < n; i++) if (s_ingressi_test[i] == '\r' || s_ingressi_test[i] == '\n') s_ingressi_test[i] = ',';
        } else snprintf(s_ingressi_test, sizeof s_ingressi_test, "%s", e);
    }
    imposta_cpu(getenv("AMIGA_CPU") ? atoi(getenv("AMIGA_CPU")) : 1);
    if (getenv("AMIGA_TRACCIA_CHIAMATE")) s_traccia_chiamate = fopen(getenv("AMIGA_TRACCIA_CHIAMATE"), "w");
    if (getenv("AMIGA_GUARDIA")) s_guardia = (uint32_t)strtoul(getenv("AMIGA_GUARDIA"), NULL, 16);
    if (getenv("AMIGA_WAV")) {
        s_wav = fopen(getenv("AMIGA_WAV"), "wb");
        if (s_wav) fseek(s_wav, 44, SEEK_SET);
    }

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_Init(s_senza_finestra ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);
    amiga_hw_init();
    amiga_paula_volume(80);
    if (!s_senza_finestra) {
        SDL_AudioSpec voglio = { 0 }, ho;
        voglio.freq = 48000; voglio.format = AUDIO_S16SYS; voglio.channels = 2; voglio.samples = 1024;
        s_audio = SDL_OpenAudioDevice(NULL, 0, &voglio, &ho, 0);
        if (s_audio) SDL_PauseAudioDevice(s_audio, 0);
        const char *titolo = g_game_spec.display_name ? g_game_spec.display_name : "Amiga";
        /* la finestra: il fattore intero piu' grande (sulle 256 righe PAL) che
         * entra nello schermo, lasciando spazio a barra del titolo e taskbar */
        SDL_DisplayMode dm;
        int k = 3;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { k = (dm.h - 120) / 256; if (k < 1) k = 1; }
        int alto = 256 * k, largo = (int)(alto * 4.0 / 3.0 + 0.5);
        s_win = SDL_CreateWindow(titolo, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, largo, alto,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
        s_gl = amiga_video_gl_init(s_win, SCHERMO_NITIDO);
        if (s_gl) { amiga_video_gl_dimensioni(640, 512); amiga_video_gl_righe_scala(256); }
        cattura_mouse(1);                            /* si vede il puntatore di Intuition */
    }
    amiga_debug_init(debug || s_senza_finestra, debug && !s_senza_finestra);

    g_amiga_mem = calloc(1, OS_MEM + 16);
    g_amiga_mem_size = OS_MEM;
    os_quadro = quadro_in_attesa;
    /* il gioco mette in memoria il programma, registra le sue librerie e parte */
    if (!g_game_spec.call_entry_point) { fprintf(stderr, "nessun punto di ingresso\n"); return 1; }
    amiga_verifica_init();                       /* AMIGA_VERIFICA=n: prova contro Musashi */
    g_game_spec.call_entry_point();
    fprintf(stderr, "il programma e' finito (D0=%d)\n", (int)g_cpu.D[0]);
    return 0;
}
