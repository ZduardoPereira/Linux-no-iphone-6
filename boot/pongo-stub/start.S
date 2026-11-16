/*
 * Stub de 16 KB que vai NA FRENTE do m1n1 quando damos boot pelo pongoOS antigo
 * do checkra1n 0.12.4 (comando "bootr"). Faz o que o "bootm" do pongoOS novo faria.
 *
 * Entrada (pongoOS master, bootr): EL1 SEGURO, MMU desligada, x0 = boot_args do iBoot.
 * Primeiro sobe para EL3 com "smc #0" (o monitor do pongoOS devolve a CPU em EL3, como
 * no "bootm" do pongoOS novo): o m1n1, entrando em EL3, desce para EL1 NÃO-SEGURO, como
 * o iOS. Em EL1 seguro os caches da CPU não são coerentes com o DMA dos dispositivos
 * (que acessam a memória como não-seguros): flash (ANS) e DART do PCIe liam dados
 * velhos. Compilar com STUB_SEM_EL3=1 mantém o comportamento antigo.
 * Layout:  +0x00 b inicio | +0x08 tamanho do payload (u64) | +0x10 versão do iBoot (64 B)
 *          ... código ...  | +0x4000 m1n1 + payload (DTB, kernel, initramfs)
 */
.section .text.start, "ax"
.globl _start
_start:
    b       1f
    .balign 8
.globl stub_payload_size
stub_payload_size:
    .quad   0                   // preenchido pelo scripts/05-empacotar.sh
.globl stub_iboot_version
stub_iboot_version:
    .space  64                  // ex.: "iBoot-4513.270.14"
1:
#ifndef STUB_SEM_EL3
    mrs     x9, CurrentEL
    cmp     x9, #0x4            // estamos em EL1?
    b.ne    2f
    mrs     x9, id_aa64pfr0_el1
    tst     x9, #0xf000         // o chip tem EL3?
    b.eq    2f
    smc     #0                  // monitor do pongoOS: volta na instrução seguinte, em EL3
2:
#endif
    mov     x19, x0             // guarda boot_args
    adr     x20, _start
    add     sp, x20, #0x4000    // pilha no fim dos 16 KB do stub
    mov     x0, x19
    mov     x1, x20
    bl      stub_main           // devolve o endereço de entrada do m1n1
    mov     x9, x0
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    mov     x0, x19             // m1n1 espera x0 = boot_args
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    br      x9
