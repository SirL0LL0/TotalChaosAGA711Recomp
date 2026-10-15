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

/* prima di tutto il resto (anche dentro una fiber, anche con la pila quasi
 * finita): codice e indirizzo scritti subito con WriteFile, senza il buffer
 * del C, e lo stato della pila; poi il filtro sopra fa il resto */
static LONG WINAPI vettoriale(EXCEPTION_POINTERS *e) {
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_STACK_OVERFLOW && c != EXCEPTION_ILLEGAL_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG volte;
    if (InterlockedIncrement(&volte) > 1) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m);
    char b[256];
    int n = wsprintfA(b, "[crash] (subito) eccezione $%08lX a +$%lX del modulo, pila %p\r\n", c,
                      (unsigned long)((char *)e->ExceptionRecord->ExceptionAddress - (char *)m), (void *)&b);
    DWORD scritti;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), b, (DWORD)n, &scritti, NULL);
    /* la pila delle chiamate C (scostamenti nel modulo del runner, per
     * llvm-symbolizer): in una ricorsione si vede il giro che si ripete */
    void *f[40];
    USHORT k = RtlCaptureStackBackTrace(0, 40, f, NULL);
    HMODULE io = GetModuleHandleA(NULL);
    n = wsprintfA(b, "[crash] pila:");
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), b, (DWORD)n, &scritti, NULL);
    for (USHORT i = 0; i < k; i++) {
        n = wsprintfA(b, " +%lX", (unsigned long)((char *)f[i] - (char *)io));
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), b, (DWORD)n, &scritti, NULL);
    }
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), "\r\n", 2, &scritti, NULL);
    return EXCEPTION_CONTINUE_SEARCH;
}

void amiga_crash_init(void (*al_crash)(const char *perche)) {
    s_al_crash = al_crash;
    ULONG riserva = 64 * 1024;
    SetThreadStackGuarantee(&riserva);
    AddVectoredExceptionHandler(1, vettoriale);
    SetUnhandledExceptionFilter(filtro);
}
