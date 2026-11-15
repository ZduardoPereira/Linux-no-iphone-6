#!/usr/bin/env bash
# Junta tudo num arquivo só, que o pongoOS envia para o iPhone:
#   m1n1.bin | bootargs | device tree | kernel | initramfs
# O m1n1 roda primeiro, lê o que vem "colado" depois dele e dá boot no Linux.
set -euo pipefail
source "$(dirname "$0")/../config.sh"

for f in m1n1.bin "$DTB_NAME" Image.gz initramfs.gz; do
  [ -s "$BUILD/$f" ] || die "falta build/$f — rode os scripts 02, 03 e 04 antes"
done

payload() {
  echo "chosen.bootargs=$BOOTARGS"
  cat "$BUILD/$DTB_NAME" "$BUILD/Image.gz" "$BUILD/initramfs.gz"
}

# Método pongoOS (06-boot.sh): m1n1 "cru"
{ cat "$BUILD/m1n1.bin"; payload; } > "$BUILD/m1n1-linux.bin"
# Método iBoot (06-boot-iboot.sh): m1n1 disfarçado de kernel do iOS (Mach-O)
{ cat "$SRC/m1n1/build/m1n1-idevice.macho"; payload; } > "$BUILD/m1n1-linux.macho"
cp "$SRC/m1n1/build/monitor-stub.macho" "$BUILD/monitor-stub.macho"

# Método checkra1n (06-boot-checkra1n.sh): stub de 16 KB + m1n1 cru, para o "bootr" do
# pongoOS antigo. A versão do iBoot vem de build/pongo-bootargs.txt (modo "info").
mkdir -p "$BUILD/stub"
(cd "$ROOT/boot/pongo-stub" && make -s OUT=../../build/stub)   # make não aceita espaços no caminho
IBOOT="$(grep -o 'Booted by: iBoot-[0-9.]*' "$BUILD/pongo-bootargs.txt" 2>/dev/null | cut -d' ' -f3 || true)"
python3 -I - "$BUILD/stub/pongo-stub.bin" "$BUILD/m1n1-linux.bin" "$BUILD/m1n1-pongo.bin" "$IBOOT" <<'EOF'
import struct, sys
stub, payload, out, iboot = sys.argv[1:5]
s = bytearray(open(stub, "rb").read())
p = open(payload, "rb").read()
assert len(s) <= 0x3000, "stub grande demais"
s += bytes(0x4000 - len(s))
s[8:16] = struct.pack("<Q", len(p))
v = iboot.encode()[:63]
s[16:16 + 64] = v + bytes(64 - len(v))
open(out, "wb").write(bytes(s) + p)
EOF
[ -n "$IBOOT" ] || echo "aviso: versão do iBoot desconhecida (rode: sudo ./scripts/06-boot-checkra1n.sh info)"

log "Pronto: build/m1n1-linux.bin e build/m1n1-linux.macho ($(du -h "$BUILD/m1n1-linux.macho" | cut -f1))"
log "Próximo: sudo ./scripts/06-boot-checkra1n.sh boot (o que funciona neste PC AMD)"
