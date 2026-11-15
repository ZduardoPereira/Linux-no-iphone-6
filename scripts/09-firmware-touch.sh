#!/usr/bin/env bash
# Extrai o firmware do touchscreen (chip N1) do iOS 12.5.7 do iPhone 6, direto da Apple.
# O firmware é da Apple: fica em build/firmware/ (fora do git) e não deve ser redistribuído.
#
#   ./scripts/09-firmware-touch.sh      (baixa ~2,9 GB na 1ª vez; precisa do 7zip: sudo apt install 7zip)
set -euo pipefail
source "$(dirname "$0")/../config.sh"
command -v 7z >/dev/null || die "falta o 7-Zip: sudo apt install 7zip"

IOS="$USER_HOME/.cache/linux-no-iphone-6/ios"
DMG="$IOS/16H81__iPhone7,2/038-87263-051.dmg"     # sistema de arquivos do iOS 12.5.7 (16H81)
mkdir -p "$IOS/fw" "$BUILD/firmware"

if [ ! -s "$DMG" ]; then
  log "Baixando o sistema de arquivos do iOS 12.5.7 (~2,9 GB, só desta parte do IPSW)"
  (cd "$IOS" && "$TOOLS/ipsw" download ipsw -d iPhone7,2 --version 12.5.7 \
      --pattern '^038-87263-051\.dmg$' --flat -o . -y)
fi

log "Extraindo usr/share/firmware/multitouch/N61.mtprops (7-Zip lê a imagem direto)"
7z e -y -o"$IOS/fw" "$DMG" "usr/share/firmware/multitouch/N61.mtprops" >/dev/null
# o arquivo vem comprimido pelo sistema de arquivos da Apple (decmpfs tipo 4)
python3 -I "$ROOT/host/decmpfs.py" "$IOS/fw/N61.mtprops" "$IOS/fw/N61-plist.bin"

log "Tirando o firmware do plist e conferindo os checksums"
python3 -I - "$IOS/fw/N61-plist.bin" "$BUILD/firmware/apple-n61-touch.bin" <<'EOF'
import plistlib, struct, sys
fw = plistlib.load(open(sys.argv[1], "rb"))["C1F59,1"]["Constructed Firmware"]
sw32 = lambda v: bytes([(v >> 8) & 255, v & 255, (v >> 24) & 255, (v >> 16) & 255])
n = struct.unpack(">H", fw[4:6])[0] * 4
assert fw[:4] == b"\x18\xe1\x30\x01", "não começa com 18 e1 30 01"
assert fw[10:12] == struct.pack(">H", sum(fw[4:10]) & 0xffff), "checksum do cabeçalho"
assert fw[12 + n:] == sw32(sum(fw[12:12 + n]) & 0xffffffff), "checksum da carga"
open(sys.argv[2], "wb").write(fw)
print("  firmware ok: %d bytes, checksums conferem" % len(fw))
EOF
log "Pronto: build/firmware/apple-n61-touch.bin"
