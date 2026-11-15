/*
 * vibrar — faz o iPhone vibrar usando o driver drv260x (force-feedback do Linux).
 *
 *   vibrar                 vibra 500 ms, força 100%
 *   vibrar 200 60          vibra 200 ms com 60% de força
 *   vibrar 300 100 /dev/input/event1
 *
 * Sem libc: fala direto com o kernel por chamadas de sistema (svc #0), para não
 * precisar de um ambiente de compilação ARM64 completo no PC. É um bom exemplo de
 * como um programa conversa com um driver:
 *   1. openat()  abre /dev/input/eventN (o "arquivo" do driver)
 *   2. ioctl(EVIOCSFF) envia ao driver a descrição do efeito (struct ff_effect)
 *   3. write()   manda um evento EV_FF "toque o efeito N"
 *   4. nanosleep() espera; close() fecha (e o kernel apaga o efeito)
 */
#include <linux/input.h>
#include <linux/fcntl.h>
#include <linux/time.h>

/* Números das syscalls no arm64 (include/uapi/asm-generic/unistd.h) */
#define SYS_ioctl     29
#define SYS_openat    56
#define SYS_close     57
#define SYS_read      63
#define SYS_write     64
#define SYS_exit      93
#define SYS_nanosleep 101

static long syscall3(long n, long a, long b, long c)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
static long syscall4(long n, long a, long b, long c, long d)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static unsigned long tamanho(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static void escreve(const char *s) { syscall3(SYS_write, 2, (long)s, tamanho(s)); }
static long numero(const char *s) { long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static void sai(int codigo) { syscall3(SYS_exit, codigo, 0, 0); for (;;) {} }

static void erro(const char *msg, long r)
{
    char num[24]; int i = 23; num[i] = 0;
    r = -r;
    do { num[--i] = '0' + r % 10; r /= 10; } while (r && i);
    escreve("vibrar: "); escreve(msg); escreve(" (erro -"); escreve(&num[i]); escreve(")\n");
    sai(1);
}

/*
 * Acha o /dev/input/eventN do motor: o número muda conforme a ordem em que os
 * drivers carregam, então procuramos pelo nome em /sys/class/input/eventN/device/name.
 */
static long abre_motor(void)
{
    static char sys[] = "/sys/class/input/eventN/device/name";
    static char dev[] = "/dev/input/eventN";
    static const char nome[] = "drv260x:haptics";
    for (char n = '0'; n <= '9'; n++) {
        sys[22] = n;                                  /* troca o 'N' pelo número */
        long f = syscall4(SYS_openat, AT_FDCWD, (long)sys, O_RDONLY, 0);
        if (f < 0)
            continue;
        char buf[32] = {0};
        syscall3(SYS_read, f, (long)buf, sizeof(buf) - 1);
        syscall3(SYS_close, f, 0, 0);
        unsigned long i = 0;
        while (nome[i] && buf[i] == nome[i])
            i++;
        if (!nome[i]) {
            dev[16] = n;
            return syscall4(SYS_openat, AT_FDCWD, (long)dev, O_RDWR, 0);
        }
    }
    return -19;                                       /* -ENODEV */
}

static int principal(int argc, char **argv)
{
    long ms     = argc > 1 ? numero(argv[1]) : 500;
    long forca  = argc > 2 ? numero(argv[2]) : 100;

    if (forca > 100) forca = 100;
    if (ms > 5000) ms = 5000;          /* não deixa vibrar mais de 5 s sem querer */

    long fd = argc > 3 ? syscall4(SYS_openat, AT_FDCWD, (long)argv[3], O_RDWR, 0)
                       : abre_motor();
    if (fd < 0)
        erro("não consegui abrir o dispositivo de vibração", fd);

    struct ff_effect efeito = {0};
    efeito.type = FF_RUMBLE;
    efeito.id = -1;                                  /* -1 = o kernel escolhe o id */
    efeito.replay.length = (unsigned short)ms;
    efeito.u.rumble.strong_magnitude = (unsigned short)(forca * 0xffff / 100);
    long r = syscall3(SYS_ioctl, fd, EVIOCSFF, (long)&efeito);
    if (r < 0)
        erro("o driver recusou o efeito (EVIOCSFF)", r);

    struct input_event tocar = {0};
    tocar.type = EV_FF;
    tocar.code = efeito.id;
    tocar.value = 1;                                 /* tocar 1 vez */
    r = syscall3(SYS_write, fd, (long)&tocar, sizeof(tocar));
    if (r < 0)
        erro("falha ao mandar tocar", r);

    struct timespec espera = { ms / 1000, (ms % 1000) * 1000000L };
    syscall3(SYS_nanosleep, (long)&espera, 0, 0);
    syscall3(SYS_close, fd, 0, 0);
    return 0;
}

/* Ponto de entrada sem libc: o kernel deixa argc e argv na pilha. */
__attribute__((used)) static void inicio_c(long *pilha)
{
    sai(principal((int)pilha[0], (char **)(pilha + 1)));
}
__asm__(".global _start\n_start:\n  mov x0, sp\n  b inicio_c\n");

/* Conferências em tempo de compilação: o layout tem que bater com o do kernel arm64. */
_Static_assert(sizeof(struct input_event) == 24, "input_event deveria ter 24 bytes");
_Static_assert(sizeof(struct ff_effect) == 48, "ff_effect deveria ter 48 bytes");
