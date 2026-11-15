#!/usr/bin/env bash
# Compila o m1n1 (bootloader) e o pongoterm (terminal do pongoOS no PC).
set -euo pipefail
source "$(dirname "$0")/../config.sh"

# Patches nossos do m1n1 (m1n1/patches/*.patch), aplicados em ordem e uma vez só
REG="$SRC/m1n1/.iphone6-patches"
touch "$REG"
for p in "$ROOT"/m1n1/patches/*.patch; do
  [ -e "$p" ] || continue
  n="$(basename "$p")"
  if grep -qxF "$n" "$REG"; then
    log "Patch do m1n1 já aplicado: $n"
  else
    log "Aplicando patch do m1n1: $n"
    git -C "$SRC/m1n1" apply "$p"
    echo "$n" >> "$REG"
  fi
done

log "Compilando m1n1"
make -C "$SRC/m1n1" -j"$JOBS" CHAINLOADING=1
cp "$SRC/m1n1/build/m1n1.bin" "$BUILD/m1n1.bin"

log "Compilando pongoterm"
clang -Os "$SRC/pongoterm.c" -DUSE_LIBUSB -lusb-1.0 -o "$TOOLS/pongoterm"

log "Pronto: build/m1n1.bin e tools/pongoterm. Próximo: ./scripts/03-kernel.sh"
