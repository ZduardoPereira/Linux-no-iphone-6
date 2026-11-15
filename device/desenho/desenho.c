/*
 * desenho — o primeiro "app" do iPhone 6 com Linux: pinta na tela onde o dedo passa.
 *
 *   desenho          (Power sai, Home limpa, Volume +/- muda a espessura)
 *
 * Sem libc: só chamadas de sistema (mini.h). Mostra o caminho mínimo de um programa
 * gráfico no Linux:
 *   tela   -> /dev/fb0, mapeado com mmap: escrever um u32 = pintar um pixel
 *   console-> /dev/tty0 em modo gráfico (KDSETMODE), senão o texto do kernel apaga o desenho
 *   toque  -> /dev/input/eventN do "Apple iPhone 6 touchscreen" (protocolo multitoque B)
 *   botões -> /dev/input/eventN do "gpio-keys"
 *   espera -> ppoll() nos dois ao mesmo tempo
 */
#include "mini.h"
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/poll.h>

#define SYS_ppoll 73

/* ---------- tela ---------- */
static volatile unsigned int *fb;
static int larg, alt, passo;                  /* em pixels; passo = pixels por linha */
static struct fb_var_screeninfo vi;

static unsigned int cor(unsigned r, unsigned g, unsigned b)
{
    return (r >> (8 - vi.red.length)) << vi.red.offset |
           (g >> (8 - vi.green.length)) << vi.green.offset |
           (b >> (8 - vi.blue.length)) << vi.blue.offset;
}

static void retangulo(int x0, int y0, int x1, int y1, unsigned int c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > larg) x1 = larg;
    if (y1 > alt) y1 = alt;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            fb[y * passo + x] = c;
}

static void bola(int cx, int cy, int r, unsigned int c)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && py >= 0 && px < larg && py < alt)
                    fb[py * passo + px] = c;
            }
}

/* linha grossa: "carimba" bolas ao longo do segmento */
static void traco(int x0, int y0, int x1, int y1, int r, unsigned int c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (n == 0) { bola(x0, y0, r, c); return; }
    for (int i = 0; i <= n; i += (r > 2 ? r / 2 : 1))
        bola(x0 + dx * i / n, y0 + dy * i / n, r, c);
    bola(x1, y1, r, c);
}

/* ---------- interface: paleta embaixo ---------- */
#define N_CORES 7
static unsigned int paleta[N_CORES + 1];
static int cor_atual = 0, raio = 8;
#define ALTURA_BARRA 110

static void desenha_barra(void)
{
    int w = larg / (N_CORES + 1);
    retangulo(0, alt - ALTURA_BARRA - 4, larg, alt - ALTURA_BARRA, cor(80, 80, 80));
    for (int i = 0; i < N_CORES; i++) {
        retangulo(i * w, alt - ALTURA_BARRA, (i + 1) * w, alt, paleta[i]);
        if (i == cor_atual)                    /* marca a cor escolhida */
            retangulo(i * w + w / 3, alt - ALTURA_BARRA + 35, i * w + 2 * w / 3,
                      alt - 35, cor(255, 255, 255) ^ paleta[i]);
    }
    /* último quadrado = "limpar" (um X) */
    int x0 = N_CORES * w;
    retangulo(x0, alt - ALTURA_BARRA, larg, alt, cor(30, 30, 30));
    traco(x0 + 20, alt - ALTURA_BARRA + 20, larg - 20, alt - 20, 4, cor(230, 230, 230));
    traco(larg - 20, alt - ALTURA_BARRA + 20, x0 + 20, alt - 20, 4, cor(230, 230, 230));
}

static void limpa(void)
{
    retangulo(0, 0, larg, alt - ALTURA_BARRA - 4, cor(12, 14, 20));
    desenha_barra();
}

/* ---------- entrada ---------- */
static long abre_por_nome(const char *nome)
{
    static char sys[] = "/sys/class/input/eventN/device/name";
    static char dev[] = "/dev/input/eventN";
    for (char n = '0'; n <= '9'; n++) {
        sys[22] = n;
        long f = sc3(SYS_openat, AT_FDCWD, sys, O_RDONLY);
        if (f < 0) continue;
        char buf[48] = {0};
        sc3(SYS_read, f, buf, sizeof(buf) - 1);
        sc3(SYS_close, f, 0, 0);
        unsigned long i = 0;
        while (nome[i] && buf[i] == nome[i]) i++;
        if (!nome[i]) { dev[16] = n; return sc3(SYS_openat, AT_FDCWD, dev, O_RDONLY); }
    }
    return -1;
}

#define SLOTS 16
static struct { int ativo, x, y, tinha; int ux, uy; } dedo[SLOTS];
static int xmax = 5850, ymax = 10405;

static void toque_quadro(void)                /* chamado a cada SYN_REPORT */
{
    for (int s = 0; s < SLOTS; s++) {
        if (!dedo[s].ativo) { dedo[s].tinha = 0; continue; }
        int px = dedo[s].x * (larg - 1) / xmax, py = dedo[s].y * (alt - 1) / ymax;
        if (py >= alt - ALTURA_BARRA) {       /* tocou na barra */
            if (!dedo[s].tinha) {
                int i = px / (larg / (N_CORES + 1));
                if (i >= N_CORES) limpa(); else { cor_atual = i; desenha_barra(); }
            }
            dedo[s].tinha = 1;
            dedo[s].ux = -1;
            continue;
        }
        if (dedo[s].tinha && dedo[s].ux >= 0)
            traco(dedo[s].ux, dedo[s].uy, px, py, raio, paleta[cor_atual]);
        else
            bola(px, py, raio, paleta[cor_atual]);
        dedo[s].ux = px; dedo[s].uy = py; dedo[s].tinha = 1;
    }
}

static int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    long tty = sc3(SYS_openat, AT_FDCWD, "/dev/tty0", O_RDWR);
    long ffb = sc3(SYS_openat, AT_FDCWD, "/dev/fb0", O_RDWR);
    long ftoque = abre_por_nome("Apple iPhone 6 touchscreen");
    long fbot = abre_por_nome("gpio-keys");
    if (ffb < 0 || ftoque < 0) { err("desenho: falta /dev/fb0 ou o touchscreen\n"); return 1; }

    struct fb_fix_screeninfo fi;
    sc3(SYS_ioctl, ffb, FBIOGET_VSCREENINFO, &vi);
    sc3(SYS_ioctl, ffb, FBIOGET_FSCREENINFO, &fi);
    larg = vi.xres; alt = vi.yres; passo = fi.line_length / 4;
    if (vi.bits_per_pixel != 32) { err("desenho: só sei 32 bits por pixel\n"); return 1; }
    long m = sc6(SYS_mmap, 0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, ffb, 0);
    if (m < 0 && m > -4096) { err("desenho: mmap do fb0 falhou\n"); return 1; }
    fb = (volatile unsigned int *)m;

    struct input_absinfo ai;                  /* faixas reais do touch, perguntadas ao kernel */
    if (!sc3(SYS_ioctl, ftoque, EVIOCGABS(ABS_MT_POSITION_X), &ai)) xmax = ai.maximum;
    if (!sc3(SYS_ioctl, ftoque, EVIOCGABS(ABS_MT_POSITION_Y), &ai)) ymax = ai.maximum;

    if (tty >= 0) sc3(SYS_ioctl, tty, KDSETMODE, KD_GRAPHICS);   /* cala o console */

    paleta[0] = cor(255, 255, 255); paleta[1] = cor(240, 60, 60);  paleta[2] = cor(255, 170, 30);
    paleta[3] = cor(250, 230, 50);  paleta[4] = cor(60, 200, 90);  paleta[5] = cor(60, 140, 255);
    paleta[6] = cor(190, 90, 230);
    limpa();
    out("desenho: rodando (Power sai, Home limpa, Volume muda a espessura)\n");

    struct pollfd pf[2] = { { (int)ftoque, POLLIN, 0 }, { (int)fbot, POLLIN, 0 } };
    struct input_event ev[32];
    int slot = 0, sair = 0;
    while (!sair) {
        if (sc6(SYS_ppoll, (long)pf, fbot >= 0 ? 2 : 1, 0, 0, 0, 0) <= 0) continue;
        if (pf[0].revents & POLLIN) {
            long n = sc3(SYS_read, ftoque, ev, sizeof(ev)) / (long)sizeof(ev[0]);
            for (long i = 0; i < n; i++) {
                struct input_event *e = &ev[i];
                if (e->type == EV_ABS) {
                    if (e->code == ABS_MT_SLOT) slot = e->value % SLOTS;
                    else if (e->code == ABS_MT_TRACKING_ID) {
                        dedo[slot].ativo = e->value >= 0;
                        if (e->value >= 0) dedo[slot].ux = -1;
                    } else if (e->code == ABS_MT_POSITION_X) dedo[slot].x = e->value;
                    else if (e->code == ABS_MT_POSITION_Y) dedo[slot].y = e->value;
                } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
                    toque_quadro();
                }
            }
        }
        if (fbot >= 0 && (pf[1].revents & POLLIN)) {
            long n = sc3(SYS_read, fbot, ev, sizeof(ev)) / (long)sizeof(ev[0]);
            for (long i = 0; i < n; i++) {
                if (ev[i].type != EV_KEY || ev[i].value != 1) continue;   /* só "apertou" */
                if (ev[i].code == KEY_POWER) sair = 1;
                else if (ev[i].code == KEY_HOMEPAGE) limpa();
                else if (ev[i].code == KEY_VOLUMEUP && raio < 40) raio += 3;
                else if (ev[i].code == KEY_VOLUMEDOWN && raio > 2) raio -= 3;
            }
        }
    }

    if (tty >= 0) sc3(SYS_ioctl, tty, KDSETMODE, KD_TEXT);       /* devolve o console */
    out("desenho: fim\n");
    return 0;
}

_Static_assert(sizeof(struct input_event) == 24, "input_event deveria ter 24 bytes");
