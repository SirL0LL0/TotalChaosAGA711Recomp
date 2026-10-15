/* amiga_crash.c - un crash del programma nativo (accesso a memoria non
 * valida nel C del runner o in quello generato): invece di sparire senza
 * dire niente, scrive su stderr (o in Chaos_debug.log con --debug) il codice
 * dell'eccezione, l'indirizzo nel programma e lo stato 68k, e salva
 * errore_quadro.bmp ed errore_memoria.bin come gli altri errori.
 * Tenuto a parte per non mescolare windows.h con il resto del runner. */
#include <windows.h>
#include <stdio.h>

static void (*s_al_crash)(const char *perche);

static LONG WINAPI filtro(EXCEPTION_POINTERS *e) {
    static volatile LONG dentro;
    if (InterlockedExchange(&dentro, 1)) return EXCEPTION_CONTINUE_SEARCH;   /* crash nel crash */
    EXCEPTION_RECORD *r = e->ExceptionRecord;
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)r->ExceptionAddress, &m);
    fprintf(stderr, "[crash] eccezione $%08lX all'indirizzo %p (modulo %p, +$%llX)",
            r->ExceptionCode, r->ExceptionAddress, (void *)m,
            m ? (unsigned long long)((char *)r->ExceptionAddress - (char *)m) : 0ull);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        fprintf(stderr, ", %s di %p", r->ExceptionInformation[0] ? "scrittura" : "lettura",
                (void *)r->ExceptionInformation[1]);
    fprintf(stderr, "\n");
    if (s_al_crash) s_al_crash("crash del programma");
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void amiga_crash_init(void (*al_crash)(const char *perche)) {
    s_al_crash = al_crash;
    SetUnhandledExceptionFilter(filtro);
}
