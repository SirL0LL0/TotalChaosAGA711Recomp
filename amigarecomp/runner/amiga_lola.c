/* amiga_lola.c - il modello di schermo "Lola Retro 1084S": il preset
 * Commodore 1084S di Lola Retro per Amiberry (github.com/zoran123456/
 * lola-retro, licenza MIT, di Zoki; sorgenti in terze_parti/lola-retro,
 * dentro l'eseguibile con tools/genera_lola.py). Nove passaggi GLSL 3.30 nel
 * formato dei preset .glslp di RetroArch: segnale composito codificato e
 * decodificato, bagliore, fascio, semiquadri, tubo e presentazione.
 *
 * Ogni passaggio disegna in una texture (float dove il preset lo chiede)
 * grande come dice la sua scala: "source" (quella dell'ingresso), "viewport"
 * (il rettangolo nella finestra) o assoluta. Gli shader leggono Source (il
 * passaggio prima), Original (il quadro del gioco) e i passaggi per nome
 * (LR_Signal, LR_Glow...). Le righe contano dal basso, come in OpenGL: il
 * quadro si carica capovolto. Serve un contesto OpenGL 3.3 (quello di
 * compatibilita' che Windows da' di solito): altrimenti lola_init() fallisce
 * e resta il modello 1084S nostro.
 */
#include "amiga_lola.h"
#include "amiga_lola_dati.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif

static PFNGLCREATESHADERPROC p_CreateShader;
static PFNGLSHADERSOURCEPROC p_ShaderSource;
static PFNGLCOMPILESHADERPROC p_CompileShader;
static PFNGLGETSHADERIVPROC p_GetShaderiv;
static PFNGLGETSHADERINFOLOGPROC p_GetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC p_CreateProgram;
static PFNGLATTACHSHADERPROC p_AttachShader;
static PFNGLBINDATTRIBLOCATIONPROC p_BindAttribLocation;
static PFNGLLINKPROGRAMPROC p_LinkProgram;
static PFNGLGETPROGRAMIVPROC p_GetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC p_GetProgramInfoLog;
static PFNGLUSEPROGRAMPROC p_UseProgram;
static PFNGLGETUNIFORMLOCATIONPROC p_GetUniformLocation;
static PFNGLUNIFORM1IPROC p_Uniform1i;
static PFNGLUNIFORM1FPROC p_Uniform1f;
static PFNGLUNIFORM2FPROC p_Uniform2f;
static PFNGLUNIFORMMATRIX4FVPROC p_UniformMatrix4fv;
static PFNGLVERTEXATTRIBPOINTERPROC p_VertexAttribPointer;
static PFNGLENABLEVERTEXATTRIBARRAYPROC p_EnableVertexAttribArray;
static PFNGLACTIVETEXTUREPROC p_ActiveTexture;
static PFNGLGENFRAMEBUFFERSPROC p_GenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC p_BindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_FramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_CheckFramebufferStatus;

typedef struct {
    GLuint prog, fbo, tex;
    int w, h;                                   /* l'uscita */
    GLint u_mvp, u_frame, u_tsize, u_osize, u_out, u_source, u_original;
    GLint u_alias[LOLA_N_PASSI];                /* i sampler dei passaggi con nome */
} passo_t;
static passo_t s_p[LOLA_N_PASSI];
static GLuint s_orig;                           /* il quadro del gioco, capovolto */
static int s_ow, s_oh, s_pronto = -1;
static unsigned s_quadro;
static uint32_t *s_capovolto;
static size_t s_cap_n;

#define CARICA(v, nome) if (!(v = (void *)SDL_GL_GetProcAddress(nome))) { fprintf(stderr, "[lola] manca %s\n", nome); return 0; }

static GLuint compila(GLenum tipo, const char *src, const char *def) {
    /* la prima riga e' "#version 330 core": il define va subito dopo */
    const char *nl = strchr(src, '\n');
    size_t lv = nl ? (size_t)(nl - src + 1) : 0;
    const char *parti[3];
    char versione[64];
    snprintf(versione, sizeof versione, "%.*s", (int)lv, src);
    parti[0] = versione;
    parti[1] = def;
    parti[2] = src + lv;
    GLuint s = p_CreateShader(tipo);
    p_ShaderSource(s, 3, parti, NULL);
    p_CompileShader(s);
    GLint ok = 0;
    p_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        p_GetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "[lola] shader non compilato: %s\n", log);
        return 0;
    }
    return s;
}

/* i "#pragma parameter NOME "etichetta" predefinito min max passo": il
 * valore predefinito nell'uniform con lo stesso nome */
static void parametri(GLuint prog, const char *src) {
    for (const char *p = strstr(src, "#pragma parameter"); p; p = strstr(p + 1, "#pragma parameter")) {
        char nome[64];
        const char *q = p + 17;
        while (*q == ' ') q++;
        int n = 0;
        while (q[n] && q[n] != ' ' && n < 63) { nome[n] = q[n]; n++; }
        nome[n] = 0;
        const char *v = strchr(q + n, '"');
        v = v ? strchr(v + 1, '"') : NULL;
        if (!v) continue;
        GLint loc = p_GetUniformLocation(prog, nome);
        if (loc >= 0) p_Uniform1f(loc, (float)atof(v + 1));
    }
}

static int prepara(void) {
    CARICA(p_CreateShader, "glCreateShader"); CARICA(p_ShaderSource, "glShaderSource");
    CARICA(p_CompileShader, "glCompileShader"); CARICA(p_GetShaderiv, "glGetShaderiv");
    CARICA(p_GetShaderInfoLog, "glGetShaderInfoLog"); CARICA(p_CreateProgram, "glCreateProgram");
    CARICA(p_AttachShader, "glAttachShader"); CARICA(p_BindAttribLocation, "glBindAttribLocation");
    CARICA(p_LinkProgram, "glLinkProgram"); CARICA(p_GetProgramiv, "glGetProgramiv");
    CARICA(p_GetProgramInfoLog, "glGetProgramInfoLog"); CARICA(p_UseProgram, "glUseProgram");
    CARICA(p_GetUniformLocation, "glGetUniformLocation"); CARICA(p_Uniform1i, "glUniform1i");
    CARICA(p_Uniform1f, "glUniform1f"); CARICA(p_Uniform2f, "glUniform2f");
    CARICA(p_UniformMatrix4fv, "glUniformMatrix4fv"); CARICA(p_VertexAttribPointer, "glVertexAttribPointer");
    CARICA(p_EnableVertexAttribArray, "glEnableVertexAttribArray"); CARICA(p_ActiveTexture, "glActiveTexture");
    CARICA(p_GenFramebuffers, "glGenFramebuffers"); CARICA(p_BindFramebuffer, "glBindFramebuffer");
    CARICA(p_FramebufferTexture2D, "glFramebufferTexture2D");
    CARICA(p_CheckFramebufferStatus, "glCheckFramebufferStatus");
    const char *ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (!ver || atof(ver) < 3.3) {
        fprintf(stderr, "[lola] GLSL %s: serve la 3.30\n", ver ? ver : "?");
        return 0;
    }
    for (int i = 0; i < LOLA_N_PASSI; i++) {
        passo_t *p = &s_p[i];
        const char *src = LOLA_SORGENTI[LOLA_PASSI[i].src];
        GLuint vs = compila(GL_VERTEX_SHADER, src, "#define VERTEX\n");
        GLuint fs = compila(GL_FRAGMENT_SHADER, src, "#define FRAGMENT\n");
        if (!vs || !fs) return 0;
        p->prog = p_CreateProgram();
        p_AttachShader(p->prog, vs);
        p_AttachShader(p->prog, fs);
        p_BindAttribLocation(p->prog, 0, "VertexCoord");
        p_BindAttribLocation(p->prog, 1, "TexCoord");
        p_LinkProgram(p->prog);
        GLint ok = 0;
        p_GetProgramiv(p->prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            p_GetProgramInfoLog(p->prog, sizeof log, NULL, log);
            fprintf(stderr, "[lola] passaggio %d non collegato: %s\n", i, log);
            return 0;
        }
        p_UseProgram(p->prog);
        p->u_mvp = p_GetUniformLocation(p->prog, "MVPMatrix");
        p->u_frame = p_GetUniformLocation(p->prog, "FrameCount");
        p->u_tsize = p_GetUniformLocation(p->prog, "TextureSize");
        p->u_osize = p_GetUniformLocation(p->prog, "OrigTextureSize");
        p->u_out = p_GetUniformLocation(p->prog, "OutputSize");
        p->u_source = p_GetUniformLocation(p->prog, "Source");
        p->u_original = p_GetUniformLocation(p->prog, "Original");
        for (int j = 0; j < LOLA_N_PASSI; j++)
            p->u_alias[j] = LOLA_PASSI[j].alias[0] ? p_GetUniformLocation(p->prog, LOLA_PASSI[j].alias) : -1;
        parametri(p->prog, src);
        if (i < LOLA_N_PASSI - 1) {
            glGenTextures(1, &p->tex);
            p_GenFramebuffers(1, &p->fbo);
        }
    }
    glGenTextures(1, &s_orig);
    p_UseProgram(0);
    return 1;
}

int lola_init(void) {
    if (s_pronto < 0) s_pronto = prepara();
    return s_pronto;
}

/* filtro e bordi della texture che il passaggio i legge come Source */
static void parametri_ingresso(GLuint tex, int i) {
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint f = LOLA_PASSI[i].lineare ? GL_LINEAR : GL_NEAREST;
    GLint w = LOLA_PASSI[i].bordo ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE;
    static const float nero[4] = { 0, 0, 0, 0 };
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, w);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, w);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, nero);
}

static int scala(int tipo, float v, int ingresso, int viewport) {
    float r = tipo == 0 ? ingresso * v : tipo == 1 ? viewport * v : v;
    int n = (int)(r + 0.5f);
    return n < 1 ? 1 : n;
}

void lola_presenta(const uint32_t *pixel, int w, int h, int vx, int vy, int vw, int vh) {
    if (s_pronto != 1) return;
    /* il quadro capovolto: le righe contano dal basso */
    size_t n = (size_t)w * (size_t)h;
    if (n > s_cap_n) { free(s_capovolto); s_capovolto = malloc(n * 4); s_cap_n = s_capovolto ? n : 0; }
    if (!s_capovolto) return;
    for (int y = 0; y < h; y++) memcpy(s_capovolto + (size_t)y * w, pixel + (size_t)(h - 1 - y) * w, (size_t)w * 4);
    glBindTexture(GL_TEXTURE_2D, s_orig);
    if (w != s_ow || h != s_oh) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        s_ow = w; s_oh = h;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, s_capovolto);

    static const float mvp[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, -1, -1, 0, 1 };   /* [0,1] -> [-1,1] */
    static const float vert[16] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1 };
    static const float coord[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    p_VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, vert);
    p_EnableVertexAttribArray(0);
    p_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, coord);
    p_EnableVertexAttribArray(1);

    GLuint ingresso = s_orig;
    int iw = w, ih = h;
    for (int i = 0; i < LOLA_N_PASSI; i++) {
        passo_t *p = &s_p[i];
        const lola_passo_t *d = &LOLA_PASSI[i];
        int ultimo = i == LOLA_N_PASSI - 1;
        int ow = scala(d->tx, d->sx, iw, vw), oh = scala(d->ty, d->sy, ih, vh);
        if (!ultimo) {
            if (ow != p->w || oh != p->h) {
                glBindTexture(GL_TEXTURE_2D, p->tex);
                glTexImage2D(GL_TEXTURE_2D, 0, d->fl ? GL_RGBA16F : GL_RGBA8, ow, oh, 0, GL_RGBA,
                             d->fl ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
                p_BindFramebuffer(GL_FRAMEBUFFER, p->fbo);
                p_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
                if (p_CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    fprintf(stderr, "[lola] framebuffer del passaggio %d (%dx%d) incompleto\n", i, ow, oh);
                    s_pronto = 0;
                    p_BindFramebuffer(GL_FRAMEBUFFER, 0);
                    return;
                }
                p->w = ow; p->h = oh;
            }
            p_BindFramebuffer(GL_FRAMEBUFFER, p->fbo);
            glViewport(0, 0, ow, oh);
        } else {
            p_BindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(vx, vy, vw, vh);
        }
        p_UseProgram(p->prog);
        p_UniformMatrix4fv(p->u_mvp, 1, GL_FALSE, mvp);
        p_Uniform1i(p->u_frame, (int)s_quadro);
        p_Uniform2f(p->u_tsize, (float)iw, (float)ih);
        p_Uniform2f(p->u_osize, (float)w, (float)h);
        p_Uniform2f(p->u_out, (float)ow, (float)oh);
        int unita = 0;
        p_ActiveTexture(GL_TEXTURE0 + unita);
        parametri_ingresso(ingresso, i);
        p_Uniform1i(p->u_source, unita++);
        if (p->u_original >= 0) {
            p_ActiveTexture(GL_TEXTURE0 + unita);
            glBindTexture(GL_TEXTURE_2D, s_orig);
            p_Uniform1i(p->u_original, unita++);
        }
        for (int j = 0; j < i; j++)
            if (p->u_alias[j] >= 0) {
                p_ActiveTexture(GL_TEXTURE0 + unita);
                glBindTexture(GL_TEXTURE_2D, s_p[j].tex);
                p_Uniform1i(p->u_alias[j], unita++);
            }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        for (int u = unita - 1; u >= 0; u--) { p_ActiveTexture(GL_TEXTURE0 + u); glBindTexture(GL_TEXTURE_2D, 0); }
        ingresso = p->tex;
        iw = ow; ih = oh;
    }
    p_ActiveTexture(GL_TEXTURE0);
    p_UseProgram(0);
    s_quadro++;
}
