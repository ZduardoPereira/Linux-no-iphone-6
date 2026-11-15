#!/usr/bin/env bash
# Boot pelo checkra1n 0.12.4 + o pongoOS embutido nele (comando "bootr" + stub + m1n1).
# Neste PC (AMD) é o único exploit que funcionou: palera1n e gaster falham depois do checkm8.
#
#   sudo ./scripts/06-boot-checkra1n.sh info   exploit + pergunta ao pongoOS RAM/framebuffer
#                                              (salva em build/pongo-bootargs.txt)
#   sudo ./scripts/06-boot-checkra1n.sh boot   envia DTB, initramfs e kernel e dá boot
#
# Se o iPhone já estiver no pongoOS (05ac:4141), o exploit é pulado.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
[ "$(id -u)" = 0 ] || die "rode com sudo: sudo $0 $*"
MODO="${1:-}"; [ "$MODO" = info ] || [ "$MODO" = boot ] || die "uso: sudo $0 info|boot"

PT=/tmp/linux-no-iphone-6        # pongoterm não aceita espaços no caminho
mkdir -p "$PT"

pongo() {  # envia comandos (um por linha) ao pongoOS
  printf '%s\n' "$@" > "$PT/cmds.txt"
  timeout "${PONGO_TIMEOUT:-20}" "$TOOLS/pongoterm" < "$PT/cmds.txt" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true
}

if ! lsusb | grep -q '05ac:4141'; then
  systemctl stop usbmuxd 2>/dev/null || true
  ( while true; do modprobe -r apple_mfi_fastcharge 2>/dev/null; sleep 0.2; done ) &
  MFI_LOOP=$!; trap 'kill $MFI_LOOP 2>/dev/null; systemctl start usbmuxd 2>/dev/null || true' EXIT
  if ! lsusb | grep -q '05ac:1227'; then
    echo "Coloque o iPhone em DFU (Home+Power 8 s, solte Power, Home mais 5 s)..."
    until lsusb | grep -q '05ac:1227'; do sleep 1; done
  fi
  log "Exploit (checkra1n) + pongoOS"
  timeout 150 "$TOOLS/checkra1n" -c -v -p -E 2>&1 | grep -E "Info|Error|Checkmate|probabilistic" || true
  for i in $(seq 45); do lsusb | grep -q '05ac:4141' && break; sleep 1; done
  lsusb | grep -q '05ac:4141' || die "pongoOS não apareceu — coloque em DFU e tente de novo"
fi
log "pongoOS conectado"

if [ "$MODO" = info ]; then
  pongo "bootargs" | tee "$BUILD/pongo-bootargs.txt"
  chown "${SUDO_USER:-root}:" "$BUILD/pongo-bootargs.txt" 2>/dev/null || true
  log "Salvo em build/pongo-bootargs.txt. O iPhone continua no pongoOS."
  exit 0
fi

# --- boot ---
# m1n1-pongo.bin = stub (boot/pongo-stub) + m1n1 + DTB + kernel + initramfs.
# O "bootr" pula para o início da imagem; o stub ajusta memória/firmware-version e
# entrega para o m1n1, que dá boot no Linux.
IMG="$BUILD/m1n1-pongo.bin"
[ -s "$IMG" ] || die "falta build/m1n1-pongo.bin — rode ./scripts/05-empacotar.sh"
cp "$IMG" "$PT/"
log "Enviando m1n1 + Linux ($(du -h "$IMG" | cut -f1)) e dando boot"
PONGO_TIMEOUT=90 pongo "/send $PT/m1n1-pongo.bin" "bootr"
log "Enviado. Olhe a tela do iPhone; depois: ./scripts/07-conectar.sh"
