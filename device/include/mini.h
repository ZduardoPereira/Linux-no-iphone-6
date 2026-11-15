/*
 * mini.h — o mínimo para programas do iPhone sem libc (ARM64 Linux).
 * Chamadas de sistema diretas (svc #0), escrita na tela e conversão de números.
 * Uso: #include "mini.h" e defina  static int principal(int argc, char **argv).
 */
#pragma once

/* Números das syscalls no arm64 (include/uapi/asm-generic/unistd.h) */
#define SYS_ioctl     29
#define SYS_openat    56
#define SYS_close     57
#define SYS_read      63
#define SYS_write     64
#define SYS_exit      93
#define SYS_nanosleep 101
#define SYS_munmap    215
#define SYS_mmap      222

#define AT_FDCWD  -100
#define O_RDONLY  0
#define O_RDWR    2
#define O_SYNC    04010000
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_SHARED 1

static inline long sc6(long n, long a, long b, long c, long d, long e, long f)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#define sc3(n, a, b, c) sc6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

static inline unsigned long mini_len(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static inline void out(const char *s) { sc3(SYS_write, 1, s, mini_len(s)); }
static inline void err(const char *s) { sc3(SYS_write, 2, s, mini_len(s)); }
static _Noreturn void sair(int c) { sc3(SYS_exit, c, 0, 0); for (;;) {} }

/* número decimal ou hexadecimal ("0x...") */
static inline unsigned long num(const char *s)
{
    unsigned long v = 0, base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = v * base + d;
    }
    return v;
}

/* escreve v em hexadecimal com 'digitos' dígitos */
static inline void hex(unsigned long v, int digitos)
{
    char b[17];
    for (int i = digitos - 1; i >= 0; i--, v >>= 4)
        b[i] = "0123456789abcdef"[v & 15];
    b[digitos] = 0;
    out(b);
}

static inline void dec(long v)
{
    char b[24]; int i = 23; b[i] = 0;
    int neg = v < 0; unsigned long u = neg ? -v : v;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (neg) b[--i] = '-';
    out(&b[i]);
}

static int principal(int argc, char **argv);

/* Ponto de entrada sem libc: o kernel deixa argc e argv na pilha. */
__attribute__((used)) static void inicio_c(long *pilha)
{
    sair(principal((int)pilha[0], (char **)(pilha + 1)));
}
__asm__(".global _start\n_start:\n  mov x0, sp\n  b inicio_c\n");

/*
 * O compilador pode transformar laços em chamadas a memset/memcpy mesmo com
 * -ffreestanding; como não há libc, damos versões simples (fracas: quem quiser,
 * define a sua).
 */
__attribute__((weak, used)) void *memset(void *d, int c, unsigned long n)
{
    volatile unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
__attribute__((weak, used)) void *memcpy(void *d, const void *s, unsigned long n)
{
    volatile unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}
