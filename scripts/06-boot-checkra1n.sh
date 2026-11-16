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

dfu() { lsusb | grep -q '05ac:1227'; }

# DFU só se entra pelos botões (a bootrom decide ao ligar): contagem regressiva no PC,
# parando assim que o iPhone aparece em DFU. Repete se não pegar.
guia_dfu() {
  local t
  while ! dfu; do
    echo
    echo ">>> DFU: aperte ENTER e, ao mesmo tempo, comece a segurar POWER + HOME"
    read -r _ || true
    for t in 8 7 6 5 4 3 2 1; do
      printf '\r    segurando POWER + HOME... %d ' "$t"; sleep 1
    done
    printf '\n>>> SOLTE o POWER e continue segurando o HOME\n'
    for t in $(seq 10 -1 1); do
      dfu && break
      printf '\r    só HOME... %2d ' "$t"; sleep 1
    done
    echo
    if dfu; then
      echo ">>> DFU detectado! Pode soltar o HOME (a tela fica preta, é normal)."
    else
      echo ">>> Não entrou em DFU. Se aparecer a MAÇÃ, repita JÁ: o iOS não pode terminar"
      echo "    de ligar (ele poderia ocupar o espaço da flash que é do Linux)."
    fi
  done
}

if ! lsusb | grep -q '05ac:4141'; then
  systemctl stop usbmuxd 2>/dev/null || true
  ( while true; do modprobe -r apple_mfi_fastcharge 2>/dev/null; sleep 0.2; done ) &
  MFI_LOOP=$!; trap 'kill $MFI_LOOP 2>/dev/null; systemctl start usbmuxd 2>/dev/null || true' EXIT
  # Linux rodando no iPhone: salva a flash antes de cortar a energia
  if lsusb | grep -q '1d6b:0104'; then
    log "Linux rodando no iPhone: salvando a flash (flash-linux desligar)"
    timeout 30 sudo -u "${SUDO_USER:-root}" "$ROOT/scripts/ssh.sh" \
      '/usr/local/sbin/flash-linux desligar' 2>&1 | tail -1 ||
      echo "  (não consegui pelo SSH; use o botão Desligar na tela antes do DFU)"
  fi
  if ! lsusb | grep -q '05ac:1227'; then
    guia_dfu
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
