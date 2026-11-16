/*
 * inicio — tela inicial do iPhone 6 com Linux (LVGL). Fase 5.
 *
 * Mostra, ao vivo, o que os nossos drivers leem do hardware:
 *   bateria (bq27540-battery), carregador (sn2400-usb), temperatura (apple_temp_*),
 *   brilho (backlight), vibração (drv260x, via /usr/local/bin/vibrar), sistema,
 *   flash interna (ext4 em /mnt/flash, montado pelo /init com flash-linux),
 *   Wi-Fi (rede, sinal e endereço, perguntados ao wpa_supplicant).
 * Botões físicos: Power apaga/acende a tela, Volume +/- muda o brilho.
 * Botão "Desligar": salva a flash (flash-linux desligar) antes de cortar a energia.
 *
 * Desenho: LVGL v9 no /dev/fb0 (driver lv_linux_fbdev) e toque pelo /dev/input
 * (lv_evdev). O laço principal dorme até o próximo evento do LVGL ou um botão.
 */
#include "lvgl/lvgl.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/un.h>
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
static lv_obj_t *v_wifi, *n_wifi, *d_wifi;
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
static void colhe_filhos(void);

/* ---------- Wi-Fi: perguntas ao wpa_supplicant ---------- */
/* Pelo socket de controle dele, o mesmo que o wpa_cli usa: a resposta vem em menos de
 * 1 ms, sem rodar programa nenhum. Devolve o tamanho da resposta, ou -1 se o
 * wpa_supplicant não estiver rodando. */
static int wpa_pede(const char *cmd, char *resp, size_t n)
{
    struct sockaddr_un eu = { .sun_family = AF_UNIX };
    struct sockaddr_un ele = { .sun_family = AF_UNIX, .sun_path = "/run/wpa_supplicant/wlan0" };
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0), r = -1;
    if (fd < 0) return -1;
    /* bind só com a família: o kernel dá a este socket um endereço abstrato único, para
     * onde o wpa_supplicant manda a resposta (sem criar arquivo nenhum) */
    if (bind(fd, (struct sockaddr *)&eu, sizeof(sa_family_t)) == 0 &&
        connect(fd, (struct sockaddr *)&ele, sizeof(ele)) == 0 &&
        send(fd, cmd, strlen(cmd), 0) >= 0) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 300) > 0 && (r = recv(fd, resp, n - 1, 0)) >= 0)
            resp[r] = 0;
    }
    close(fd);
    return r;
}

/* valor de "chave=..." numa resposta do wpa_supplicant (uma chave por linha) */
static int campo(const char *resp, const char *chave, char *val, size_t n)
{
    size_t k = strlen(chave);
    for (const char *l = resp; l && *l; ) {
        if (!strncmp(l, chave, k) && l[k] == '=') {
            size_t m = strcspn(l + k + 1, "\n");
            if (m >= n) m = n - 1;
            memcpy(val, l + k + 1, m);
            val[m] = 0;
            return 1;
        }
        l = strchr(l, '\n');
        if (l) l++;
    }
    val[0] = 0;
    return 0;
}

/* O wpa_supplicant escreve os bytes fora do ASCII do nome da rede como \xNN
 * (é = \xc3\xa9), e \" e \\ para aspas e barra: devolve o texto original. */
static void desescapa(char *s)
{
    char *o = s;
    for (const char *i = s; *i; i++) {
        if (*i != '\\' || !i[1]) { *o++ = *i; continue; }
        i++;
        if (*i == 'x' && isxdigit((unsigned char)i[1]) && isxdigit((unsigned char)i[2])) {
            char h[3] = { i[1], i[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            i += 2;
        } else if (*i == 'n' || *i == 'r' || *i == 't' || *i == 'e') {
            *o++ = ' ';                                   /* controle: vira espaço */
        } else {
            *o++ = *i;                                    /* \" e \\ */
        }
    }
    *o = 0;
}

/* qualidade do sinal em palavras (sem "perna": g, p, q... encostariam na linha de baixo) */
static const char *qualidade(long rssi)
{
    return rssi >= -55 ? "Ótimo" : rssi >= -67 ? "Bom" : rssi >= -75 ? "Médio" : "Fraco";
}

static uint32_t cor_sinal(long rssi)
{
    return rssi >= -67 ? 0x51cf66 : rssi >= -75 ? 0xfcc419 : 0xff6b5b;
}

/* cartão do Wi-Fi; devolve 1 se está conectado e com endereço (ícone na barra) */
static int atualiza_wifi(void)
{
    char st[1024], sinal[256], estado[32], nome[160], ip[48], s[96];
    lv_obj_set_style_text_color(v_wifi, lv_color_white(), 0);
    lv_label_set_text(v_wifi, "--");
    lv_label_set_text(d_wifi, "toque para escolher a rede");
    if (wpa_pede("STATUS", st, sizeof(st)) <= 0) {
        /* wpa_supplicant parado: o script wifi deixa o motivo em /tmp/wifi-estado */
        le_txt("/tmp/wifi-estado", s, sizeof(s));
        if (!s[0])
            snprintf(s, sizeof(s), "%s",
                     access("/sys/class/net/wlan0", F_OK) ? "desligado" : "ligado, sem rede");
        lv_label_set_text(n_wifi, s);
        return 0;
    }
    campo(st, "wpa_state", estado, sizeof(estado));
    campo(st, "ssid", nome, sizeof(nome));
    desescapa(nome);
    campo(st, "ip_address", ip, sizeof(ip));
    if (strcmp(estado, "COMPLETED")) {
        /* associando e conferindo a senha, ou procurando uma rede salva ao alcance */
        if (!strcmp(estado, "INACTIVE"))                  /* ligado, sem rede salva */
            lv_label_set_text(n_wifi, "ligado, sem rede");
        else if (nome[0] && strcmp(estado, "SCANNING") && strcmp(estado, "DISCONNECTED"))
            lv_label_set_text_fmt(n_wifi, "conectando: %s", nome);
        else
            lv_label_set_text(n_wifi, "procurando rede...");
        return 0;
    }
    long rssi = 0;
    if (wpa_pede("SIGNAL_POLL", sinal, sizeof(sinal)) > 0 && campo(sinal, "RSSI", s, sizeof(s)))
        rssi = atol(s);
    const char *falta_ip = "pegando endereço...";
    if (rssi) {
        lv_label_set_text(v_wifi, qualidade(rssi));
        lv_obj_set_style_text_color(v_wifi, lv_color_hex(cor_sinal(rssi)), 0);
        lv_label_set_text_fmt(d_wifi, "%ld dBm · %s", rssi, ip[0] ? ip : falta_ip);
    } else {
        lv_label_set_text(v_wifi, "Ok");
        lv_label_set_text(d_wifi, ip[0] ? ip : falta_ip);
    }
    lv_label_set_text(n_wifi, nome);
    return ip[0] != 0;
}

static void atualiza(lv_timer_t *t)
{
    (void)t;
    retoma_tela();
    colhe_filhos();                                       /* sem processos zumbis */
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
    int wifi = atualiza_wifi();
    lv_label_set_text_fmt(l_bat, "%s%s%s  %ld%%", wifi ? LV_SYMBOL_WIFI "    " : "",
                          carregando ? LV_SYMBOL_CHARGE " " : "", icone, cap);
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
        lv_label_set_text_fmt(d_flash, "livres de %llu GB", total);
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
    lv_label_set_text(capa_texto, "A flash interna é salva e travada. Depois é só "
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
    capa_mostra(LV_SYMBOL_REFRESH "  Religando...", "Liberando a flash interna.");
    roda("/usr/local/sbin/flash-linux religar >/tmp/flash.log 2>&1 && echo ok >/tmp/flash-estado");
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

/* ---------- tela do Wi-Fi: escolher a rede ---------- */
/* Tocar no cartão do Wi-Fi abre esta tela (sobre tudo, em lv_layer_top). A lista vem do
 * wpa_supplicant (SCAN e SCAN_RESULTS, pelo socket). Conectar e esquecer usam o próprio
 * script wifi em segundo plano: ele salva a rede, confere a senha e pede o endereço
 * (até ~45 s) sem a tela parar. A senha vai pelo stdin do script, não pela linha de
 * comando (que qualquer processo vê no ps). */
#define WIFI_CMD  "/usr/local/sbin/wifi"
#define WIFI_LOG  "/tmp/wifi-tela.log"
#define WPA_CONF  "/etc/wpa_supplicant/wpa_supplicant.conf"
#define MAX_REDES 30

enum { P_LISTA, P_SENHA, P_REDE, P_ESPERA, P_FIM };

struct rede {
    char nome[100];
    int sinal, freq;
    char tipo;                  /* 'p' com senha (WPA/WPA2), 'a' aberta, 'x' outra (WPA3, empresa, WEP) */
    char salva, conectada;
};

static struct rede redes[MAX_REDES];
static int n_redes;
static char assinatura[4096];   /* o que a lista mostra agora: só refaz se mudar */

static lv_obj_t *wt, *wt_lista, *wt_info, *wt_senha, *wt_aviso;
static lv_timer_t *wt_timer;
static int wt_pagina;
static struct rede wt_rede;     /* a rede escolhida (cópia: a lista pode mudar) */
static char wt_texto[400];      /* mensagem da espera e do fim */
static int wt_ok;               /* P_FIM: deu certo? */
static int wt_busca;            /* 0: falta pedir a busca; 1..3: leituras feitas */
static int wt_ligou;            /* já rodou "wifi ligar" desde que a lista abriu */
static uint32_t wt_t0;
static pid_t wt_filho = -1;     /* o script wifi rodando em segundo plano */
static char wt_acao;            /* 'l' ligar, 'c' conectar, 'e' esquecer */
static int wt_terminou, wt_status;

/* recolhe os processos filhos que terminaram (o script wifi, o vibrar...) */
static void colhe_filhos(void)
{
    int st;
    pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
        if (p == wt_filho) {
            wt_filho = -1;
            wt_terminou = 1;
            wt_status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        }
    }
}

static int raiz_na_flash(void)
{
    char l[256];
    int sim = 0;
    FILE *f = fopen("/proc/mounts", "r");
    while (f && !sim && fgets(l, sizeof(l), f))
        sim = !strncmp(l, "/dev/mapper/linux / ", 20);
    if (f) fclose(f);
    return sim;
}

/* roda "wifi <acao> [nome]" em segundo plano; "entrada" vai para o stdin (a senha) */
static void wt_roda(char acao, const char *nome, const char *entrada)
{
    const char *sub = acao == 'l' ? "ligar" : acao == 'c' ? "conectar" : "esquecer";
    int p[2];
    wt_acao = acao;
    wt_terminou = 0;
    if (pipe(p)) { wt_terminou = 1; wt_status = -1; return; }
    pid_t f = fork();
    if (f == 0) {
        dup2(p[0], 0);
        int log = open(WIFI_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (log >= 0) { dup2(log, 1); dup2(log, 2); }
        /* tela, toque, tty...: não vão parar no wpa_supplicant que o script inicia */
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execl(WIFI_CMD, "wifi", sub, nome, (char *)NULL);
        _exit(127);
    }
    close(p[0]);
    if (f > 0) {
        wt_filho = f;
        /* poucos bytes: cabem no buffer do pipe, o write não espera o script ler */
        if (entrada && (write(p[1], entrada, strlen(entrada)) < 0 || write(p[1], "\n", 1) < 0))
            fprintf(stderr, "wifi: não consegui passar a senha\n");
    } else {
        wt_terminou = 1;
        wt_status = -1;
    }
    close(p[1]);
}

/* última mensagem "wifi: ..." que o script escreveu */
static void wt_ultima_msg(char *buf, size_t n)
{
    char l[300];
    FILE *f = fopen(WIFI_LOG, "r");
    buf[0] = 0;
    while (f && fgets(l, sizeof(l), f))
        if (!strncmp(l, "wifi: ", 6)) {
            l[strcspn(l, "\n")] = 0;
            snprintf(buf, n, "%s", l + 6);
        }
    if (f) fclose(f);
    if (!buf[0]) snprintf(buf, n, "erro (ver %s)", WIFI_LOG);
}

/* a rede está no arquivo das redes? (a mesma linha que o script procura) */
static int rede_salva(const char *nome)
{
    char l[300], procura[300];
    int sim = 0;
    snprintf(procura, sizeof(procura), "\tssid=\"%s\"\n", nome);
    FILE *f = fopen(WPA_CONF, "r");
    while (f && !sim && fgets(l, sizeof(l), f)) sim = !strcmp(l, procura);
    if (f) fclose(f);
    return sim;
}

static int compara_redes(const void *a, const void *b)
{
    const struct rede *x = a, *y = b;
    if (x->conectada != y->conectada) return y->conectada - x->conectada;
    return y->sinal - x->sinal;
}

/* SCAN_RESULTS: "bssid<TAB>freq<TAB>sinal<TAB>[flags]<TAB>nome", um por ponto de acesso.
 * Uma rede com vários pontos de acesso aparece uma vez só, com o sinal mais forte. */
static void le_redes(void)
{
    static char r[16384];
    char st[1024], estado[32], atual[100] = "";
    if (wpa_pede("SCAN_RESULTS", r, sizeof(r)) <= 0) return;
    if (wpa_pede("STATUS", st, sizeof(st)) > 0) {
        campo(st, "wpa_state", estado, sizeof(estado));
        if (!strcmp(estado, "COMPLETED")) {
            campo(st, "ssid", atual, sizeof(atual));
            desescapa(atual);
        }
    }
    n_redes = 0;
    char *l = strchr(r, '\n');                            /* pula o cabeçalho */
    while (l) {
        l++;
        char *fim = strchr(l, '\n');
        if (fim) *fim = 0;
        char *c[5];
        int k = 0;
        c[k++] = l;
        for (char *s = l; k < 5 && (s = strchr(s, '\t')); ) { *s++ = 0; c[k++] = s; }
        if (k == 5 && c[4][0]) {                          /* sem nome = rede oculta */
            char nome[100];
            snprintf(nome, sizeof(nome), "%s", c[4]);
            desescapa(nome);
            int j = 0, sinal = atoi(c[2]);
            while (j < n_redes && strcmp(redes[j].nome, nome)) j++;
            if (j == n_redes && n_redes < MAX_REDES) {
                struct rede *n = &redes[n_redes++];
                memset(n, 0, sizeof(*n));
                snprintf(n->nome, sizeof(n->nome), "%s", nome);
                n->sinal = -999;
                n->tipo = strstr(c[3], "PSK") ? 'p' :
                          !strstr(c[3], "WPA") && !strstr(c[3], "RSN") && !strstr(c[3], "WEP") ? 'a' : 'x';
                n->salva = rede_salva(nome);
                n->conectada = !strcmp(nome, atual);
            }
            if (j < n_redes && sinal > redes[j].sinal) {
                redes[j].sinal = sinal;
                redes[j].freq = atoi(c[1]);
            }
        }
        l = fim;
    }
    qsort(redes, n_redes, sizeof(redes[0]), compara_redes);
}

static void wt_vai(int pagina);

static void wt_toca_rede(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= n_redes) return;
    wt_rede = redes[i];
    wt_vai(!wt_rede.salva && !wt_rede.conectada && wt_rede.tipo == 'p' ? P_SENHA : P_REDE);
}

static void monta_lista(void)
{
    char sig[sizeof(assinatura)] = "";
    for (int i = 0; i < n_redes; i++) {
        size_t k = strlen(sig);
        snprintf(sig + k, sizeof(sig) - k, "%s|%c%d%d%s;", redes[i].nome, redes[i].tipo,
                 redes[i].salva, redes[i].conectada, qualidade(redes[i].sinal));
    }
    const char *ainda = wt_busca < 3 ? " · procurando..." : "";
    if (n_redes == 0) lv_label_set_text_fmt(wt_info, "nenhuma rede ainda%s", ainda);
    else if (n_redes == 1) lv_label_set_text_fmt(wt_info, "1 rede%s", ainda);
    else lv_label_set_text_fmt(wt_info, "%d redes%s", n_redes, ainda);
    if (!strcmp(sig, assinatura)) return;                 /* nada mudou: não mexe na lista */
    snprintf(assinatura, sizeof(assinatura), "%s", sig);
    lv_obj_clean(wt_lista);
    int32_t h28 = lv_font_get_line_height(f28);
    for (int i = 0; i < n_redes; i++) {
        struct rede *r = &redes[i];
        lv_obj_t *b = lv_button_create(wt_lista);
        lv_obj_set_size(b, lv_pct(100), 112);
        lv_obj_set_style_radius(b, 22, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_hor(b, 26, 0);
        lv_obj_set_style_pad_ver(b, 16, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(r->conectada ? 0x24406b : 0x1e2433), 0);
        lv_obj_add_event_cb(b, wt_toca_rede, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *n = lv_label_create(b);
        lv_obj_set_size(n, lv_pct(100), h28);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(n, r->nome);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *d = lv_label_create(b);
        lv_obj_set_style_text_font(d, f20, 0);
        lv_obj_set_style_text_color(d, lv_color_hex(0xb8c0d0), 0);
        lv_label_set_text_fmt(d, "%s%s · %s · %s%s", r->conectada ? LV_SYMBOL_OK " conectada · " : "",
                              qualidade(r->sinal),
                              r->tipo == 'p' ? "com senha" : r->tipo == 'a' ? "aberta" : "não suportada",
                              r->freq > 4000 ? "5 GHz" : "2,4 GHz",
                              r->salva && !r->conectada ? " · salva" : "");
        lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

/* de 0,5 em 0,5 s: liga o Wi-Fi se preciso, pede a busca, lê os resultados aos poucos
 * (os que o wpa_supplicant já conhece na hora; a busca nova termina em ~4 s) e espera
 * o script terminar */
static void wt_fim_da_acao(void);

static void wt_tick(lv_timer_t *t)
{
    (void)t;
    char r[64];
    colhe_filhos();
    if (!wt) return;
    if (wt_pagina == P_ESPERA) {
        if (wt_terminou) wt_fim_da_acao();
        return;
    }
    if (wt_pagina != P_LISTA || !wt_lista) return;
    if (wt_busca == 0) {
        if (wpa_pede("PING", r, sizeof(r)) > 0) {
            wpa_pede("SCAN", r, sizeof(r));
            wt_busca = 1;
            wt_t0 = lv_tick_get();
            le_redes();
            monta_lista();
        } else if (wt_filho > 0) {
            /* "wifi ligar" ainda rodando */
        } else if (!wt_ligou) {
            wt_ligou = 1;
            wt_roda('l', NULL, NULL);
            lv_label_set_text(wt_info, "ligando o Wi-Fi...");
        } else {
            wt_ultima_msg(r, sizeof(r));
            lv_label_set_text_fmt(wt_info, "não ligou: %s", r);
        }
        return;
    }
    if (wt_busca < 3 && lv_tick_elaps(wt_t0) >= (wt_busca == 1 ? 4000u : 7000u)) {
        wt_busca++;
        le_redes();
        monta_lista();
    }
}

static void wt_fecha(lv_event_t *e)
{
    (void)e;
    if (wt_timer) lv_timer_delete(wt_timer);
    wt_timer = NULL;
    lv_obj_delete_async(wt);
    wt = NULL;
}

static void wt_lista_de_novo(lv_event_t *e) { (void)e; wt_vai(P_LISTA); }

static void wt_procura(lv_event_t *e)
{
    (void)e;
    wt_busca = 0;
    wt_ligou = 0;
    lv_label_set_text(wt_info, "procurando...");
}

static void wt_conecta(lv_event_t *e)
{
    (void)e;
    char senha[80] = "";
    if (wt_pagina == P_SENHA) {
        snprintf(senha, sizeof(senha), "%s", lv_textarea_get_text(wt_senha));
        size_t n = strlen(senha);
        if (n < 8 || n > 63) {
            lv_label_set_text(wt_aviso, "a senha tem de 8 a 63 caracteres");
            lv_obj_set_style_text_color(wt_aviso, lv_color_hex(0xff6b5b), 0);
            return;
        }
    }
    /* senha: rede nova com senha; "": rede aberta nova; NULL: rede salva (ele não pergunta) */
    wt_roda('c', wt_rede.nome, wt_rede.salva || wt_rede.conectada ? NULL : senha);
    memset(senha, 0, sizeof(senha));
    if (wt_senha) lv_textarea_set_text(wt_senha, "");
    snprintf(wt_texto, sizeof(wt_texto), "Conectando a %s...\n\npode levar até 30 s", wt_rede.nome);
    wt_vai(P_ESPERA);
}

static void wt_esquece(lv_event_t *e)
{
    (void)e;
    wt_roda('e', wt_rede.nome, NULL);
    snprintf(wt_texto, sizeof(wt_texto), "Esquecendo %s...", wt_rede.nome);
    wt_vai(P_ESPERA);
}

static void wt_fim_da_acao(void)
{
    char msg[300];
    const char *ram = raiz_na_flash() ? "" :
        "\n\nO sistema está na RAM: para ela valer nos próximos boots, rode no PC "
        "./scripts/13-wifi-salvar.sh";
    wt_ok = wt_status == 0;
    if (!wt_ok)
        wt_ultima_msg(msg, sizeof(msg));
    if (wt_acao == 'c' && wt_ok)
        snprintf(wt_texto, sizeof(wt_texto), "Conectado a %s.%s", wt_rede.nome, wt_rede.salva ? "" : ram);
    else if (wt_acao == 'e' && wt_ok)
        snprintf(wt_texto, sizeof(wt_texto), "%s foi esquecida.%s", wt_rede.nome, ram);
    else
        snprintf(wt_texto, sizeof(wt_texto), "%s", msg);
    wt_vai(P_FIM);
}

static void wt_mostra_senha(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    int oculta = !lv_textarea_get_password_mode(wt_senha);
    lv_textarea_set_password_mode(wt_senha, oculta);
    lv_label_set_text(lv_obj_get_child(b, 0), oculta ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void wt_teclado(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) wt_conecta(e);
    else wt_vai(P_LISTA);                                 /* tecla de esconder o teclado */
}

/* linha transparente com filhos lado a lado */
static lv_obj_t *wt_linha(lv_obj_t *pai)
{
    lv_obj_t *l = lv_obj_create(pai);
    lv_obj_set_size(l, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_pad_column(l, 16, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(l, false);
    return l;
}

static lv_obj_t *wt_redondo(lv_obj_t *pai, const char *simbolo, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(pai);
    lv_obj_set_size(b, 96, 96);
    lv_obj_set_style_radius(b, 48, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x343a4a), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, simbolo);
    lv_obj_center(l);
    return b;
}

/* título da página: botão de voltar + nome da rede (numa linha só) */
static void wt_topo_rede(const char *acima)
{
    lv_obj_t *t = wt_linha(wt);
    wt_redondo(t, LV_SYMBOL_LEFT, wt_lista_de_novo);
    lv_obj_t *c = lv_obj_create(t);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(c, false);
    lv_obj_t *a = lv_label_create(c);
    lv_obj_set_style_text_font(a, f20, 0);
    lv_obj_set_style_text_color(a, lv_color_hex(0x8a93a8), 0);
    lv_label_set_text(a, acima);
    lv_obj_t *n = lv_label_create(c);
    lv_obj_set_size(n, lv_pct(100), lv_font_get_line_height(f28));
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(n, wt_rede.nome);
}

static lv_obj_t *wt_texto_longo(const char *txt)
{
    lv_obj_t *l = lv_label_create(wt);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(l, lv_color_hex(0xb8c0d0), 0);
    lv_label_set_text(l, txt);
    return l;
}

static void wt_espaco(void)
{
    lv_obj_t *s = lv_obj_create(wt);
    lv_obj_set_size(s, 1, 1);
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s, 0, 0);
}

/* monta a página pedida (fora de qualquer evento: lv_async_call) */
static void wt_monta(void *arg)
{
    char s[300];
    if (!wt) return;
    wt_pagina = (int)(intptr_t)arg;
    lv_obj_clean(wt);
    wt_lista = wt_info = wt_senha = wt_aviso = NULL;
    lv_obj_t *t, *b;
    switch (wt_pagina) {
    case P_LISTA:
        t = wt_linha(wt);
        b = lv_label_create(t);
        lv_obj_set_style_text_font(b, f48, 0);
        lv_label_set_text(b, LV_SYMBOL_WIFI "  Wi-Fi");
        lv_obj_set_flex_grow(b, 1);
        wt_redondo(t, LV_SYMBOL_REFRESH, wt_procura);
        wt_redondo(t, LV_SYMBOL_CLOSE, wt_fecha);
        wt_info = lv_label_create(wt);
        lv_obj_set_style_text_font(wt_info, f20, 0);
        lv_obj_set_style_text_color(wt_info, lv_color_hex(0x8a93a8), 0);
        lv_label_set_text(wt_info, "procurando...");
        wt_lista = lv_obj_create(wt);
        lv_obj_set_width(wt_lista, lv_pct(100));
        lv_obj_set_flex_grow(wt_lista, 1);
        lv_obj_set_style_bg_opa(wt_lista, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wt_lista, 0, 0);
        lv_obj_set_style_pad_all(wt_lista, 0, 0);
        lv_obj_set_style_pad_row(wt_lista, 14, 0);
        lv_obj_set_flex_flow(wt_lista, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(wt_lista, LV_DIR_VER);
        assinatura[0] = 0;
        wt_busca = 0;
        wt_ligou = 0;
        wt_tick(NULL);                                    /* já mostra o que houver */
        break;
    case P_SENHA:
        wt_topo_rede("Senha de");
        t = wt_linha(wt);
        wt_senha = lv_textarea_create(t);
        lv_obj_set_flex_grow(wt_senha, 1);
        lv_textarea_set_one_line(wt_senha, true);
        lv_textarea_set_password_mode(wt_senha, true);
        lv_textarea_set_max_length(wt_senha, 63);
        lv_textarea_set_placeholder_text(wt_senha, "senha");
        lv_obj_add_state(wt_senha, LV_STATE_FOCUSED);      /* mostra o cursor */
        wt_redondo(t, LV_SYMBOL_EYE_OPEN, wt_mostra_senha);
        wt_aviso = lv_label_create(wt);
        lv_obj_set_style_text_font(wt_aviso, f20, 0);
        lv_obj_set_style_text_color(wt_aviso, lv_color_hex(0x8a93a8), 0);
        lv_label_set_text(wt_aviso, "8 a 63 caracteres · " LV_SYMBOL_OK " no teclado conecta");
        wt_espaco();
        b = lv_keyboard_create(wt);
        lv_obj_set_size(b, lv_pct(100), 520);
        lv_obj_set_style_pad_all(b, 6, 0);                 /* vãos menores: teclas mais largas */
        lv_obj_set_style_pad_column(b, 6, 0);
        lv_obj_set_style_pad_row(b, 10, 0);
        lv_keyboard_set_textarea(b, wt_senha);
        lv_obj_add_event_cb(b, wt_teclado, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(b, wt_teclado, LV_EVENT_CANCEL, NULL);
        break;
    case P_REDE:
        wt_topo_rede(wt_rede.conectada ? "Conectado a" : wt_rede.salva ? "Rede salva" : "Rede");
        if (wt_rede.tipo == 'x' && !wt_rede.salva)
            snprintf(s, sizeof(s), "Esta rede usa uma segurança que o script wifi ainda não sabe "
                     "configurar (WPA3, empresa ou WEP).");
        else
            snprintf(s, sizeof(s), "Sinal %s (%d dBm), %s.%s", qualidade(wt_rede.sinal), wt_rede.sinal,
                     wt_rede.freq > 4000 ? "5 GHz" : "2,4 GHz",
                     wt_rede.tipo == 'a' ? "\n\nRede aberta, sem senha: o que passa por ela "
                                           "pode ser visto por outros." : "");
        wt_texto_longo(s);
        t = wt_linha(wt);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (!wt_rede.conectada && (wt_rede.salva || wt_rede.tipo == 'a'))
            botao(t, LV_SYMBOL_WIFI "  Conectar", wt_conecta, 0x3b5bdb);
        if (wt_rede.salva)
            botao(t, LV_SYMBOL_TRASH "  Esquecer", wt_esquece, 0xc92a2a);
        break;
    case P_ESPERA:
        wt_espaco();
        b = lv_spinner_create(wt);
        lv_obj_set_size(b, 120, 120);
        lv_obj_set_style_align(b, LV_ALIGN_CENTER, 0);
        lv_obj_set_flex_align(wt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_text_align(wt_texto_longo(wt_texto), LV_TEXT_ALIGN_CENTER, 0);
        wt_espaco();
        break;
    case P_FIM:
        lv_obj_set_flex_align(wt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        wt_espaco();
        b = lv_label_create(wt);
        lv_obj_set_style_text_font(b, f48, 0);
        lv_obj_set_style_text_color(b, lv_color_hex(wt_ok ? 0x51cf66 : 0xff6b5b), 0);
        lv_label_set_text(b, wt_ok ? LV_SYMBOL_OK "  Pronto" : LV_SYMBOL_WARNING "  Não deu");
        lv_obj_set_style_text_align(wt_texto_longo(wt_texto), LV_TEXT_ALIGN_CENTER, 0);
        wt_espaco();
        /* conectou: volta para a tela inicial; senão, para a lista */
        botao(wt, "OK", wt_ok && wt_acao == 'c' ? wt_fecha : wt_lista_de_novo, 0x3b5bdb);
        break;
    }
    if (wt_pagina != P_ESPERA && wt_pagina != P_FIM)
        lv_obj_set_flex_align(wt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

static void wt_vai(int pagina)
{
    lv_async_call(wt_monta, (void *)(intptr_t)pagina);
}

static void abre_wifi(lv_event_t *e)
{
    (void)e;
    if (wt || capa) return;
    wt = lv_obj_create(lv_layer_top());
    lv_obj_set_size(wt, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(wt, 0, 0);
    lv_obj_set_style_border_width(wt, 0, 0);
    lv_obj_set_style_bg_color(wt, lv_color_hex(0x0b0e16), 0);
    lv_obj_set_style_bg_opa(wt, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(wt, 30, 0);
    lv_obj_set_style_pad_top(wt, 40, 0);
    lv_obj_set_style_pad_row(wt, 24, 0);
    lv_obj_set_style_text_font(wt, f28, 0);
    lv_obj_set_style_text_color(wt, lv_color_white(), 0);
    lv_obj_set_flex_flow(wt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(wt, false);
    wt_timer = lv_timer_create(wt_tick, 500, NULL);
    wt_monta((void *)(intptr_t)P_LISTA);
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
    cartao(grade, LV_SYMBOL_SD_CARD, "Flash interna", &v_flash, &d_flash);
    /* Wi-Fi: qualidade do sinal grande; embaixo, o nome da rede e "dBm · endereço",
     * uma linha cada (texto comprido termina em "...") */
    lv_obj_t *w = cartao(grade, LV_SYMBOL_WIFI, "Wi-Fi", &v_wifi, &d_wifi);
    lv_obj_add_event_cb(w, abre_wifi, LV_EVENT_CLICKED, NULL);   /* toque: escolher a rede */
    lv_obj_set_style_bg_color(w, lv_color_hex(0x2c3550), LV_STATE_PRESSED);
    lv_obj_t *seta = lv_label_create(w);
    lv_obj_set_style_text_font(seta, f20, 0);
    lv_obj_set_style_text_color(seta, lv_color_hex(0x8a93a8), 0);
    lv_label_set_text(seta, LV_SYMBOL_RIGHT);
    lv_obj_align(seta, LV_ALIGN_TOP_RIGHT, 0, 0);
    int32_t linha = lv_font_get_line_height(f20);
    lv_obj_set_height(d_wifi, linha);
    lv_label_set_long_mode(d_wifi, LV_LABEL_LONG_MODE_DOTS);
    n_wifi = lv_label_create(w);
    lv_obj_set_style_text_font(n_wifi, f20, 0);
    lv_obj_set_size(n_wifi, lv_pct(100), linha);
    lv_label_set_long_mode(n_wifi, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(n_wifi, LV_ALIGN_BOTTOM_LEFT, 0, -linha - 2);

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

    lv_obj_set_width(botao(grade, LV_SYMBOL_BELL "  Vibrar", vibrar, 0x3b5bdb), lv_pct(30));
    lv_obj_set_width(botao(grade, LV_SYMBOL_EDIT "  Desenho", abre_desenho, 0x0ca678), lv_pct(30));
    lv_obj_set_width(botao(grade, LV_SYMBOL_POWER "  Desligar", abre_desligar, 0xc92a2a), lv_pct(30));
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
    signal(SIGPIPE, SIG_IGN);       /* script que termina antes de ler a senha: só erro no write */
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
