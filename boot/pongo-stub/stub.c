/*
 * Prepara o terreno para o m1n1 quando ele é iniciado pelo pongoOS antigo (bootr):
 *  1. Restaura /chosen/firmware-version no device tree da Apple (o pongoOS troca por
 *     "pongoOS-..."; o m1n1 usa essa versão para interpretar os boot_args).
 *  2. Move o m1n1 para um endereço alinhado a 16 KB acima de topOfKernelData e sobe
 *     topOfKernelData para depois dele (o heap do m1n1 começa em topOfKernelData).
 *  3. Devolve o endereço de entrada do m1n1 (_start fica em +0x800).
 *
 * Roda com a MMU desligada: toda memória é "Device", então nada de acesso desalinhado
 * (-mstrict-align) e nada de FP/SIMD (-mgeneral-regs-only). Ponteiros volatile impedem o
 * compilador de trocar os laços por chamadas a memcpy/memmove (não há libc aqui).
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

struct boot_args {          /* mesmo layout de m1n1/src/xnuboot.h */
    u16 revision;
    u16 version;
    u64 virt_base;
    u64 phys_base;
    u64 mem_size;
    u64 top_of_kernel_data;
    u64 video[6];
    u32 machine_type;
    u64 devtree;
    u32 devtree_size;
};

extern u64 stub_payload_size;
extern char stub_iboot_version[64];

#define STUB_SIZE  0x4000UL
#define ALIGN16K(x) (((x) + 0x3fffUL) & ~0x3fffUL)

static void fix_firmware_version(struct boot_args *ba)
{
    volatile u8 *dt = (volatile u8 *)(ba->devtree - ba->virt_base + ba->phys_base);
    static const char key[] = "pongoOS-";
    u32 n = ba->devtree_size;

    if (!stub_iboot_version[0])
        return;
    for (u32 i = 0; i + 8 < n; i++) {
        u32 k = 0;
        while (k < 8 && dt[i + k] == (u8)key[k])
            k++;
        if (k < 8)
            continue;
        u32 old_len = 0;                       /* tamanho da string do pongoOS */
        while (i + old_len < n && dt[i + old_len])
            old_len++;
        u32 j = 0;
        for (; j < old_len && stub_iboot_version[j]; j++)
            dt[i + j] = (u8)stub_iboot_version[j];
        for (; j < old_len; j++)
            dt[i + j] = 0;                     /* completa com NUL */
        return;
    }
}

/*
 * 2º núcleo (CPU 1). Ao ligar um núcleo, o "recfg" do PMGR (sequência programada
 * pelo iBoot) restaura o RVBAR dele para o endereço que o iBoot escolheu — o mesmo
 * para os dois núcleos. O m1n1 até escreve o próprio endereço no RVBAR, mas o recfg
 * desfaz. Então colocamos NESSE endereço um trampolim que pula para o m1n1, cujo
 * vetor de reset (offset 0) já sabe receber um núcleo novo, inclusive em EL3.
 * RVBAR da CPU 1 do A8: ADT cpus/cpu1 "reg-private" (0x202110000) + 0x40000.
 */
#ifndef STUB_RVBAR_CPU1
#define STUB_RVBAR_CPU1 0x202150000UL
#endif
#define RVBAR_ENDERECO 0x0000fffffffff000UL

static void trampolim_cpu1(u64 m1n1, u64 m1n1_fim, u64 stub, u64 stub_fim)
{
    u64 alvo = *(volatile u64 *)STUB_RVBAR_CPU1 & RVBAR_ENDERECO;

    if (!alvo || alvo == m1n1)
        return;                            /* já aponta para o m1n1 */
    if ((alvo + 16 > stub && alvo < stub_fim) || (alvo + 16 > m1n1 && alvo < m1n1_fim))
        return;                            /* cairia em cima de código em uso */

    volatile unsigned int *p = (volatile unsigned int *)alvo;
    p[0] = 0x58000050;                     /* ldr x16, [pc, #8] */
    p[1] = 0xd61f0200;                     /* br  x16           */
    *(volatile u64 *)(alvo + 8) = m1n1;    /* endereço do vetor de reset do m1n1 */
}

u64 stub_main(struct boot_args *ba, u8 *self)
{
    u64 src = (u64)self + STUB_SIZE;
    u64 size = (stub_payload_size + 7) & ~7UL;
    u64 dst = ALIGN16K(ba->top_of_kernel_data > src ? ba->top_of_kernel_data : src);

    fix_firmware_version(ba);

    if (dst != src) {
        /* dst > src sempre; copiando de trás para frente não pisamos no stub nem no
         * próprio dado ainda não copiado (memmove para regiões sobrepostas). */
        volatile u64 *s = (volatile u64 *)src;
        volatile u64 *d = (volatile u64 *)dst;
        for (u64 i = size / 8; i-- > 0;)
            d[i] = s[i];
    }
    ba->top_of_kernel_data = ALIGN16K(dst + size);
    /* depois da cópia: a origem já não é necessária */
    trampolim_cpu1(dst, dst + size, (u64)self, (u64)self + STUB_SIZE);
    return dst + 0x800;
}
