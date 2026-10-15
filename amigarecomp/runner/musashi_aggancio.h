/* musashi_aggancio.h - incluso in ogni file di Musashi (-include): il gancio
 * d'istruzione che amiga_verifica.c usa per fermare l'interprete al ritorno
 * della funzione o quando esce dal codice del gioco. m68kconf.h definisce
 * questi valori solo se mancano. */
#pragma once
void verifica_istruzione(unsigned int pc);
#define M68K_INSTRUCTION_HOOK         2   /* M68K_OPT_SPECIFY_HANDLER */
#define M68K_INSTRUCTION_CALLBACK(pc) verifica_istruzione(pc)
#define M68K_EMULATE_PMMU             0   /* M68K_OPT_OFF */
