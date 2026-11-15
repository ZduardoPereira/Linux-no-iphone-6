/*
 * regs — lê (ou escreve) registradores de hardware pelo endereço físico, via /dev/mem.
 *
 *   regs 0x20e02b000              lê 1 registrador de 32 bits
 *   regs 0x20e02b000 0x88         lê 0x88 bytes (34 registradores de 32 bits)
 *   regs 0x202230000 0x80 64      lê em palavras de 64 bits
 *   regs -w 0x20e02b000 0x1       ESCREVE 0x1 (cuidado: hardware de verdade!)
 *
 * Como funciona: o /dev/mem representa a memória física inteira. mmap() pede ao kernel
 * para colocar a página física do registrador dentro do espaço de endereços do programa;
 * a partir daí, ler um ponteiro "volatile" é ler o registrador.
 */
#include "mini.h"

static int principal(int argc, char **argv)
{
    int escrever = argc > 1 && argv[1][0] == '-' && argv[1][1] == 'w';
    if (escrever) { argv++; argc--; }
    if (argc < 2) {
        err("uso: regs ENDERECO [BYTES] [32|64]   |   regs -w ENDERECO VALOR [32|64]\n");
        return 2;
    }
    unsigned long end = num(argv[1]);
    unsigned long qtd = escrever ? 0 : (argc > 2 ? num(argv[2]) : 4);
    unsigned long valor = escrever ? num(argv[2]) : 0;
    int largura = argc > 3 ? (int)num(argv[3]) : 32;
    int passo = largura == 64 ? 8 : 4;
    if (end % passo) { err("regs: endereço desalinhado\n"); return 2; }

    long fd = sc3(SYS_openat, AT_FDCWD, "/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { err("regs: não abri /dev/mem (precisa ser root)\n"); return 1; }

    unsigned long pagina = end & ~0xfffUL, desloc = end - pagina;
    unsigned long tam = (desloc + (qtd ? qtd : passo) + 0xfff) & ~0xfffUL;
    long m = sc6(SYS_mmap, 0, tam, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pagina);
    if (m < 0 && m > -4096) { err("regs: mmap falhou\n"); return 1; }
    volatile unsigned char *base = (volatile unsigned char *)m + desloc;

    if (escrever) {
        if (largura == 64) *(volatile unsigned long *)base = valor;
        else *(volatile unsigned int *)base = (unsigned int)valor;
        __asm__ volatile("dsb sy" ::: "memory");
        out("escrito "); hex(valor, largura / 4); out(" em "); hex(end, 9); out("\n");
    }
    for (unsigned long i = 0; i < qtd; i += passo) {
        unsigned long v = largura == 64 ? *(volatile unsigned long *)(base + i)
                                        : *(volatile unsigned int *)(base + i);
        hex(end + i, 9); out(": "); hex(v, largura / 4); out("\n");
    }
    sc6(SYS_munmap, m, tam, 0, 0, 0, 0);
    sc3(SYS_close, fd, 0, 0);
    return 0;
}
