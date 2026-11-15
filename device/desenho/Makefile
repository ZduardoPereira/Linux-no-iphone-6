# Compila "desenho" para o iPhone (ARM64), estático e sem libc.
# UAPI = cabeçalhos do kernel (make headers_install), gerados pelo scripts/04-initramfs.sh
UAPI ?= $(HOME)/.cache/linux-no-iphone-6/uapi/include
OUT  ?= .

$(OUT)/desenho: desenho.c
	clang --target=aarch64-linux-gnu -O2 -static -nostdlib -ffreestanding \
	      -fno-stack-protector -fno-pie -fuse-ld=lld -Wall -Wextra \
	      -I../include -I$(UAPI) desenho.c -o $@
	llvm-strip $@
