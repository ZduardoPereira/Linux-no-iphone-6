#!/usr/bin/env bash
# Boot pelo método iBoot (HoolockLinux/remote_boot) — recomendado neste PC (AMD).
# Não usa pongoOS: o exploit é feito pelo gaster, e o próprio iBoot da Apple
# (com a verificação de assinatura desligada) carrega o m1n1 como se fosse o kernel do iOS.
#
#   sudo ./scripts/06-boot-iboot.sh prep   1x: baixa iBSS/iBEC/DeviceTree da Apple e aplica patches
#   sudo ./scripts/06-boot-iboot.sh        boot (repetir a cada vez que ligar o iPhone)
#
# Precisa de root (acesso direto ao USB). Antes: iPhone em DFU.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
[ "$(id -u)" = 0 ] || die "rode com sudo: sudo $0 $*"
command -v irecovery >/dev/null || die "falta o irecovery: sudo apt install irecovery"
[ -x "$TOOLS/ipsw" ] || die "falta tools/ipsw — rode ./scripts/01-baixar.sh"
[ -x "$REMOTE_BOOT/remoteboot.sh" ] || die "falta remote_boot — rode ./scripts/01-baixar.sh"
export PATH="$TOOLS:$PATH"

# Serviços do Ubuntu que disputam o iPhone no USB e quebram o timing do exploit
systemctl stop usbmuxd 2>/dev/null || true
( while true; do modprobe -r apple_mfi_fastcharge 2>/dev/null; sleep 0.2; done ) &
MFI_LOOP=$!
trap 'kill $MFI_LOOP 2>/dev/null; systemctl start usbmuxd 2>/dev/null || true' EXIT

cd "$REMOTE_BOOT"
if [ "${1:-}" = "prep" ]; then
  log "Preparando arquivos de boot (1x por modelo; precisa de internet)"
  ./remoteboot.sh prep
else
  [ -s "$BUILD/m1n1-linux.macho" ] || die "falta build/m1n1-linux.macho — rode ./scripts/05-empacotar.sh"
  [ -d cache ] && ls cache/iBSS_* >/dev/null 2>&1 || die "rode antes: sudo $0 prep"
  # remote_boot não lida bem com espaços no caminho ("Área de trabalho")
  mkdir -p /tmp/linux-no-iphone-6
  cp "$BUILD/m1n1-linux.macho" "$BUILD/monitor-stub.macho" /tmp/linux-no-iphone-6/
  log "Dando boot (se travar após 'Sent iBSS/iBEC', desconecte e reconecte o cabo)"
  ./remoteboot.sh boot /tmp/linux-no-iphone-6/m1n1-linux.macho /tmp/linux-no-iphone-6/monitor-stub.macho
  log "Enviado! Em seguida: ./scripts/07-conectar.sh"
fi
