/*
 * inicio — tela inicial do iPhone 6 com Linux (LVGL). Fase 5.
 *
 * Mostra, ao vivo, o que os nossos drivers leem do hardware:
 *   bateria (bq27540-battery), carregador (sn2400-usb), temperatura (apple_temp_*),
 *   brilho (backlight), vibração (drv260x, via /usr/local/bin/vibrar), sistema,
 *   flash interna (ext4 em /mnt/flash, montado pelo /init com flash-linux).
 * Botões físicos: Power apaga/acende a tela, Volume +/- muda o brilho.
 * Botão "Desligar": salva a flash (flash-linux desligar) antes de cortar a energia.
 *
 * Desenho: LVGL v9 no /dev/fb0 (driver lv_linux_fbdev) e toque pelo /dev/input
 * (lv_evdev). O laço principal dorme até o próximo evento do LVGL ou um botão.
 */
#include "lvgl/lvgl.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define BAT     "/sys/class/power_supply/bq27540-battery/"
#define USB     "/sys/class/power_supply/sn2400-usb/"
#define BRILHO  "/sys/class/backlight/20e200010.backlight/"

/* Fonte Montserrat completa (com acentos), embutida no programa. As fontes
 * prontas do LVGL só têm ASCII; elas ficam como reserva para os ícones. */
__asm__(".section .rodata\n.balign 4\nttf_ini:\n"
        ".incbin \"../../src/lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf\"\n"
        "ttf_fim:\n.previous");
extern const unsigned char ttf_ini[], ttf_fim[];
static lv_font_t *f20, *f28, *f48, *f120;

static lv_font_t *fonte(int px, const lv_font_t *reserva)
{
    lv_font_t *f = lv_tiny_ttf_create_data(ttf_ini, ttf_fim - ttf_ini, px);
    f->fallback = reserva;
    return f;
}

/* ---------- leitura de arquivos do sysfs ---------- */
static long le_num(const char *arq, long padrao)
{
    FILE *f = fopen(arq, "r");
    long v = padrao;
    if (f) { if (fscanf(f, "%ld", &v) != 1) v = padrao; fclose(f); }
    return v;
}

static void le_txt(const char *arq, char *buf, size_t n)
{
    FILE *f = fopen(arq, "r");
    buf[0] = 0;
    if (f) { if (fgets(buf, n, f)) buf[strcspn(buf, "\n")] = 0; fclose(f); }
}

static void escreve_num(const char *arq, long v)
{
    FILE *f = fopen(arq, "w");
    if (f) { fprintf(f, "%ld", v); fclose(f); }
}

/* maior temperatura entre os sensores apple_temp_* (em décimos de °C) */
static int temp_max(void)
{
    int max = -1000;
    DIR *d = opendir("/sys/class/hwmon");
    struct dirent *e;
    char p[300], nome[64];
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/name", e->d_name);
        le_txt(p, nome, sizeof(nome));
        if (strncmp(nome, "apple_temp", 10)) continue;
        for (int i = 1; i <= 8; i++) {
            snprintf(p, sizeof(p), "/sys/class/hwmon/%s/temp%d_input", e->d_name, i);
            long t = le_num(p, -1000000);
            if (t > -1000000 && t / 100 > max) max = t / 100;
        }
    }
    if (d) closedir(d);
    return max;
}

/* ---------- interface ---------- */
static lv_obj_t *l_hora, *l_bat, *l_relogio, *l_data;
static lv_obj_t *v_bat, *d_bat, *v_temp, *d_temp, *v_sis, *d_sis, *v_usb, *d_usb;
static lv_obj_t *v_flash, *d_flash;
static lv_obj_t *slider;
static long brilho_max = 2047;

static lv_obj_t *cartao(lv_obj_t *pai, const char *icone, const char *titulo,
                        lv_obj_t **valor, lv_obj_t **detalhe)
{
    lv_obj_t *c = lv_obj_create(pai);
    lv_obj_set_size(c, lv_pct(48), 180);
    lv_obj_set_style_radius(c, 28, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x1e2433), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 20, 0);
    lv_obj_set_scrollable(c, false);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text_fmt(t, "%s  %s", icone, titulo);
    lv_obj_set_style_text_font(t, f20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8a93a8), 0);

    *valor = lv_label_create(c);
    lv_obj_set_style_text_font(*valor, f48, 0);
    lv_obj_set_style_text_color(*valor, lv_color_white(), 0);
    lv_obj_align(*valor, LV_ALIGN_LEFT_MID, 0, 0);

    *detalhe = lv_label_create(c);
    lv_obj_set_style_text_font(*detalhe, f20, 0);
    lv_obj_set_style_text_color(*detalhe, lv_color_hex(0xb8c0d0), 0);
    lv_obj_set_width(*detalhe, lv_pct(100));
    lv_label_set_long_mode(*detalhe, LV_LABEL_LONG_WRAP);
    lv_obj_align(*detalhe, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return c;
}

static int tty = -1;

/* killall inicio: devolve o console de texto antes de sair (senão a tela fica parada) */
static void ao_sair(int sinal)
{
    (void)sinal;
    if (tty >= 0) ioctl(tty, KDSETMODE, KD_TEXT);
    _exit(0);
}

static void retoma_tela(void)
{
    int modo = KD_GRAPHICS;
    if (tty >= 0 && ioctl(tty, KDGETMODE, &modo) == 0 && modo != KD_GRAPHICS) {
        ioctl(tty, KDSETMODE, KD_GRAPHICS);
        lv_obj_invalidate(lv_screen_active());
    }
}

static int flash_montada(void);

static void atualiza(lv_timer_t *t)
{
    (void)t;
    retoma_tela();
    char s[64];
    time_t agora = time(NULL);
    struct tm tm;
    localtime_r(&agora, &tm);
    static const char *dias[] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };
    static const char *meses[] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };
    lv_label_set_text_fmt(l_hora, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text_fmt(l_relogio, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text_fmt(l_data, "%s, %d de %s", dias[tm.tm_wday], tm.tm_mday, meses[tm.tm_mon]);

    long cap = le_num(BAT "capacity", -1), mv = le_num(BAT "voltage_now", 0) / 1000;
    long tb = le_num(BAT "temp", 0), ciclos = le_num(BAT "cycle_count", 0);
    le_txt(BAT "status", s, sizeof(s));
    int carregando = !strcmp(s, "Charging") || !strcmp(s, "Full");
    const char *icone = cap > 80 ? LV_SYMBOL_BATTERY_FULL : cap > 55 ? LV_SYMBOL_BATTERY_3 :
                        cap > 30 ? LV_SYMBOL_BATTERY_2 : cap > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(l_bat, "%s%s  %ld%%", carregando ? LV_SYMBOL_CHARGE " " : "", icone, cap);
    lv_label_set_text_fmt(v_bat, "%ld%%", cap);
    lv_label_set_text_fmt(d_bat, "%ld,%02ld V · %ld,%ld °C · %ld ciclos%s", mv / 1000, (mv % 1000) / 10,
                          tb / 10, tb % 10, ciclos, carregando ? " · carregando" : "");

    int tm10 = temp_max();
    lv_label_set_text_fmt(v_temp, "%d °C", tm10 / 10);
    lv_label_set_text(d_temp, "maior dos 5 sensores do chip A8");
    lv_obj_set_style_text_color(v_temp, tm10 > 600 ? lv_color_hex(0xff6b5b) : lv_color_white(), 0);

    long total = 0, livre = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    char linha[128];
    while (f && fgets(linha, sizeof(linha), f)) {
        sscanf(linha, "MemTotal: %ld", &total);
        sscanf(linha, "MemAvailable: %ld", &livre);
    }
    if (f) fclose(f);
    long f0 = le_num("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 0) / 1000;
    int nucleos = (int)sysconf(_SC_NPROCESSORS_ONLN);
    lv_label_set_text_fmt(v_sis, "%ld MB", (total - livre) / 1024);
    lv_label_set_text_fmt(d_sis, "de %ld MB em uso · %d núcleos · %ld MHz", total / 1024, nucleos, f0);

    long online = le_num(USB "online", 0), ma = le_num(USB "current_now", 0) / 1000;
    lv_label_set_text(v_usb, online ? "USB" : "--");
    lv_label_set_text_fmt(d_usb, online ? "cabo conectado · %ld mA de entrada" : "sem cabo", ma);

    /* flash: só a contagem do superbloco em memória (statvfs não acorda o ANS) */
    struct statvfs sv;
    if (flash_montada() && statvfs("/mnt/flash", &sv) == 0) {
        unsigned long long bs = sv.f_frsize;
        unsigned long long livre = sv.f_bavail * bs / 100000000ULL;   /* décimos de GB */
        unsigned long long total = sv.f_blocks * bs / 1000000000ULL;
        lv_label_set_text_fmt(v_flash, "%llu,%llu GB", livre / 10, livre % 10);
        lv_label_set_text_fmt(d_flash, "livres de %llu GB · ext4 em /mnt/flash", total);
    } else {
        le_txt("/tmp/flash-estado", s, sizeof(s));
        lv_label_set_text(v_flash, "--");
        lv_label_set_text_fmt(d_flash, "não montada%s%s", s[0] ? " · " : "", s);
    }
}

static int flash_montada(void)
{
    char linha[256];
    int sim = 0;
    FILE *f = fopen("/proc/mounts", "r");
    while (f && !sim && fgets(linha, sizeof(linha), f))
        sim = strstr(linha, " /mnt/flash ") != NULL;
    if (f) fclose(f);
    return sim;
}

static void ao_mudar_brilho(lv_event_t *e)
{
    long v = lv_slider_get_value(lv_event_get_target(e));
    escreve_num(BRILHO "brightness", v * brilho_max / 100);
}

static void vibrar(lv_event_t *e)
{
    (void)e;
    if (fork() == 0) {
        execl("/usr/local/bin/vibrar", "vibrar", "60", "80", (char *)NULL);
        _exit(1);
    }
}

static lv_indev_t *toque;
static char toque_dev[64];

static void abre_desenho(lv_event_t *e)
{
    (void)e;
    /* o desenho usa a tela e o toque; ao voltar, recria o toque (descarta eventos velhos) */
    if (toque) lv_indev_delete(toque);
    pid_t p = fork();
    if (p == 0) {
        execl("/usr/local/bin/desenho", "desenho", (char *)NULL);
        _exit(1);
    }
    waitpid(p, NULL, 0);
    retoma_tela();
    toque = lv_evdev_create(LV_INDEV_TYPE_POINTER, toque_dev);
    lv_obj_invalidate(lv_screen_active());
}

/* roda um comando do shell e espera (a tela fica parada enquanto isso) */
static int roda(const char *cmd)
{
    int st = -1;
    pid_t p = fork();
    if (p == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (p > 0) waitpid(p, &st, 0);
    return p > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* ---------- desligar: capa sobre a tela inteira ---------- */
static lv_obj_t *capa, *capa_titulo, *capa_texto, *capa_botoes;

static lv_obj_t *botao(lv_obj_t *pai, const char *txt, lv_event_cb_t cb, uint32_t cor);
static void capa_confirma(lv_event_t *e);
static void capa_volta(lv_event_t *e);

static void capa_fecha(lv_event_t *e)
{
    (void)e;
    lv_obj_delete_async(capa);
    capa = NULL;
}

static void capa_mostra(const char *titulo, const char *texto)
{
    lv_label_set_text(capa_titulo, titulo);
    lv_label_set_text(capa_texto, texto);
    /* o botão tocado ainda está no meio do próprio evento: some agora, é apagado depois */
    for (int i = lv_obj_get_child_count(capa_botoes) - 1; i >= 0; i--) {
        lv_obj_t *b = lv_obj_get_child(capa_botoes, i);
        lv_obj_set_hidden(b, true);
        lv_obj_delete_async(b);
    }
    lv_refr_now(NULL);                                    /* aparece antes do comando lento */
}

static void abre_desligar(lv_event_t *e)
{
    (void)e;
    if (capa) return;
    capa = lv_obj_create(lv_layer_top());
    lv_obj_set_size(capa, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(capa, 0, 0);
    lv_obj_set_style_border_width(capa, 0, 0);
    lv_obj_set_style_bg_color(capa, lv_color_hex(0x0b0e16), 0);
    lv_obj_set_style_bg_opa(capa, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(capa, 50, 0);
    lv_obj_set_flex_flow(capa, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(capa, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(capa, 40, 0);
    lv_obj_set_scrollable(capa, false);

    capa_titulo = lv_label_create(capa);
    lv_obj_set_style_text_font(capa_titulo, f48, 0);
    capa_texto = lv_label_create(capa);
    lv_obj_set_width(capa_texto, lv_pct(100));
    lv_label_set_long_mode(capa_texto, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(capa_texto, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(capa_texto, lv_color_hex(0xb8c0d0), 0);
    capa_botoes = lv_obj_create(capa);
    lv_obj_set_size(capa_botoes, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(capa_botoes, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(capa_botoes, 0, 0);
    lv_obj_set_flex_flow(capa_botoes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(capa_botoes, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_label_set_text(capa_titulo, LV_SYMBOL_POWER "  Desligar?");
    lv_label_set_text(capa_texto, "A flash interna é salva e desmontada. Depois é só "
                                  "segurar Power + Home até a tela apagar.");
    botao(capa_botoes, "Cancelar", capa_fecha, 0x343a4a);
    botao(capa_botoes, LV_SYMBOL_POWER "  Desligar", capa_confirma, 0xc92a2a);
}

static void capa_confirma(lv_event_t *e)
{
    (void)e;
    capa_mostra(LV_SYMBOL_REFRESH "  Salvando...", "Gravando o que falta e pondo a flash para dormir.");
    if (roda("/usr/local/sbin/flash-linux desligar >/tmp/flash.log 2>&1") == 0)
        capa_mostra(LV_SYMBOL_OK "  Pode desligar",
                    "Flash salva. Segure Power + Home até a tela apagar (ou entre em DFU).");
    else
        capa_mostra(LV_SYMBOL_WARNING "  Erro", "flash-linux desligar falhou (ver /tmp/flash.log).");
    botao(capa_botoes, LV_SYMBOL_LEFT "  Voltar", capa_volta, 0x3b5bdb);
}

static void capa_volta(lv_event_t *e)
{
    capa_mostra(LV_SYMBOL_REFRESH "  Montando...", "Religando a flash interna.");
    roda("/usr/local/sbin/flash-linux montar >/tmp/flash.log 2>&1 && echo ok >/tmp/flash-estado");
    capa_fecha(e);
}

static lv_obj_t *botao(lv_obj_t *pai, const char *txt, lv_event_cb_t cb, uint32_t cor)
{
    lv_obj_t *b = lv_button_create(pai);
    lv_obj_set_size(b, lv_pct(48), 120);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(cor), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static void monta_tela(void)
{
    lv_obj_t *tela = lv_screen_active();
    lv_obj_set_style_bg_color(tela, lv_color_hex(0x0b0e16), 0);
    lv_obj_set_style_bg_grad_color(tela, lv_color_hex(0x1a2340), 0);
    lv_obj_set_style_bg_grad_dir(tela, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(tela, lv_color_white(), 0);
    lv_obj_set_style_text_font(tela, f28, 0);

    l_hora = lv_label_create(tela);                       /* barra de status */
    lv_obj_align(l_hora, LV_ALIGN_TOP_LEFT, 30, 18);
    l_bat = lv_label_create(tela);
    lv_obj_align(l_bat, LV_ALIGN_TOP_RIGHT, -30, 18);

    l_relogio = lv_label_create(tela);                    /* relógio grande */
    lv_obj_set_style_text_font(l_relogio, f120, 0);
    lv_obj_align(l_relogio, LV_ALIGN_TOP_MID, 0, 110);
    l_data = lv_label_create(tela);
    lv_obj_set_style_text_color(l_data, lv_color_hex(0xb8c0d0), 0);
    lv_obj_align(l_data, LV_ALIGN_TOP_MID, 0, 260);

    lv_obj_t *grade = lv_obj_create(tela);                /* cartões */
    lv_obj_set_size(grade, lv_pct(100), 1000);
    lv_obj_align(grade, LV_ALIGN_TOP_MID, 0, 330);
    lv_obj_set_style_bg_opa(grade, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grade, 0, 0);
    lv_obj_set_style_pad_all(grade, 20, 0);
    lv_obj_set_style_pad_row(grade, 20, 0);
    lv_obj_set_flex_flow(grade, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grade, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(grade, false);
    cartao(grade, LV_SYMBOL_BATTERY_FULL, "Bateria", &v_bat, &d_bat);
    cartao(grade, LV_SYMBOL_WARNING, "Temperatura", &v_temp, &d_temp);
    cartao(grade, LV_SYMBOL_SETTINGS, "Memória", &v_sis, &d_sis);
    cartao(grade, LV_SYMBOL_USB, "Carregador", &v_usb, &d_usb);
    lv_obj_set_width(cartao(grade, LV_SYMBOL_SD_CARD, "Flash interna", &v_flash, &d_flash),
                     lv_pct(100));

    lv_obj_t *l = lv_label_create(grade);                 /* brilho */
    lv_label_set_text(l, LV_SYMBOL_IMAGE "  Brilho");
    lv_obj_set_width(l, lv_pct(100));
    slider = lv_slider_create(grade);
    lv_obj_set_width(slider, lv_pct(94));
    lv_obj_set_height(slider, 30);
    brilho_max = le_num(BRILHO "max_brightness", 2047);
    lv_slider_set_range(slider, 2, 100);
    lv_slider_set_value(slider, le_num(BRILHO "brightness", 1500) * 100 / brilho_max, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, ao_mudar_brilho, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_width(botao(grade, LV_SYMBOL_BELL "  Vibrar", vibrar, 0x3b5bdb), lv_pct(31));
    lv_obj_set_width(botao(grade, LV_SYMBOL_EDIT "  Desenho", abre_desenho, 0x0ca678), lv_pct(31));
    lv_obj_set_width(botao(grade, LV_SYMBOL_POWER "  Desligar", abre_desligar, 0xc92a2a), lv_pct(31));
}

/* ---------- botões físicos ---------- */
static int achar_evento(const char *nome, char *dev, size_t n)
{
    char p[96], s[64];
    for (int i = 0; i < 10; i++) {
        snprintf(p, sizeof(p), "/sys/class/input/event%d/device/name", i);
        le_txt(p, s, sizeof(s));
        if (!strcmp(s, nome)) { snprintf(dev, n, "/dev/input/event%d", i); return 0; }
    }
    return -1;
}

static long brilho_salvo = -1;

static void botao_fisico(int fd)
{
    struct input_event ev[16];
    ssize_t n = read(fd, ev, sizeof(ev));
    for (ssize_t i = 0; i < n / (ssize_t)sizeof(ev[0]); i++) {
        if (ev[i].type != EV_KEY || ev[i].value != 1) continue;
        if (ev[i].code == KEY_POWER) {                    /* apaga / acende a tela */
            if (brilho_salvo < 0) {
                brilho_salvo = le_num(BRILHO "brightness", 1500);
                escreve_num(BRILHO "brightness", 0);
            } else {
                escreve_num(BRILHO "brightness", brilho_salvo);
                brilho_salvo = -1;
                lv_obj_invalidate(lv_screen_active());
            }
        } else if (ev[i].code == KEY_VOLUMEUP || ev[i].code == KEY_VOLUMEDOWN) {
            int v = lv_slider_get_value(slider) + (ev[i].code == KEY_VOLUMEUP ? 10 : -10);
            v = v < 2 ? 2 : v > 100 ? 100 : v;
            lv_slider_set_value(slider, v, LV_ANIM_ON);
            escreve_num(BRILHO "brightness", v * brilho_max / 100);
        }
    }
}

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(void)
{
    if (!getenv("TZ"))
        setenv("TZ", "<-03>3", 1);                        /* horário de Brasília */
    tty = open("/dev/tty0", O_RDWR);
    signal(SIGTERM, ao_sair);
    signal(SIGINT, ao_sair);
    if (tty >= 0) ioctl(tty, KDSETMODE, KD_GRAPHICS);     /* cala o console de texto */

    lv_init();
    lv_tick_set_cb(tick_ms);
    f20 = fonte(20, &lv_font_montserrat_20);
    f28 = fonte(28, &lv_font_montserrat_28);
    f48 = fonte(48, &lv_font_montserrat_48);
    f120 = fonte(120, &lv_font_montserrat_48);
    lv_display_t *tela = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(tela, "/dev/fb0");
    if (achar_evento("Apple iPhone 6 touchscreen", toque_dev, sizeof(toque_dev)) == 0)
        toque = lv_evdev_create(LV_INDEV_TYPE_POINTER, toque_dev);
    char bot_dev[64];
    int bot = achar_evento("gpio-keys", bot_dev, sizeof(bot_dev)) == 0 ? open(bot_dev, O_RDONLY) : -1;

    monta_tela();
    atualiza(NULL);
    lv_timer_create(atualiza, 2000, NULL);

    struct pollfd pf = { bot, POLLIN, 0 };
    for (;;) {
        uint32_t espera = lv_timer_handler();             /* ms até o próximo trabalho */
        if (espera > 500) espera = 500;
        if (poll(&pf, bot >= 0, espera) > 0 && (pf.revents & POLLIN))
            botao_fisico(bot);
    }
}
