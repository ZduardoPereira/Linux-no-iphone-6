# Compila o stub como binário "cru" (sem cabeçalho), para rodar em qualquer endereço
# alinhado a 4 KB (só usa endereçamento relativo ao PC: adr/adrp).
CC      := clang --target=aarch64-none-elf
CFLAGS  := -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-stack-protector \
           -mgeneral-regs-only -mstrict-align -Wall -Wextra \
           $(if $(STUB_SEM_EL3),-DSTUB_SEM_EL3)
OUT     ?= .

$(OUT)/pongo-stub.bin: start.S stub.c stub.ld
	$(CC) $(CFLAGS) -c start.S -o $(OUT)/start.o
	$(CC) $(CFLAGS) -c stub.c -o $(OUT)/stub.o
	ld.lld -T stub.ld -static -nostdlib $(OUT)/start.o $(OUT)/stub.o -o $(OUT)/pongo-stub.elf
	llvm-objcopy -O binary $(OUT)/pongo-stub.elf $@
