#!/usr/bin/env bash
# Dá boot no Linux no iPhone. Precisa repetir a cada vez que ligar o iPhone (boot tethered).
#   ./scripts/06-boot.sh         boot completo (m1n1 + Linux)
#   ./scripts/06-boot.sh m1n1    só o m1n1 (teste: mostra o logo/infos do m1n1 na tela)
# Antes: iPhone conectado no USB e em modo DFU (tela preta).
set -euo pipefail
source "$(dirname "$0")/../config.sh"

IMG="$BUILD/m1n1-linux.bin"
[ "${1:-}" = "m1n1" ] && IMG="$BUILD/m1n1.bin"
[ -s "$IMG" ] || die "falta ${IMG#$ROOT/} — rode ./scripts/05-empacotar.sh"
for t in palera1n pongoterm Pongo.bin; do [ -e "$TOOLS/$t" ] || die "falta tools/$t"; done

if ! lsusb | grep -q '05ac:1227'; then
  echo "iPhone não está em DFU. Para entrar:"
  echo "  1) segure Home + Power por 8 s"
  echo "  2) solte Power e continue segurando Home por mais 5 s"
  echo "  3) a tela fica PRETA (se aparecer logo/cabo, tente de novo)"
  echo "Aguardando DFU..."
  until lsusb | grep -q '05ac:1227'; do sleep 1; done
fi

# O usbmuxd (serviço do Ubuntu para iPhones) "pega" o aparelho quando ele reconecta
# depois do exploit e atrapalha. Paramos durante o boot e religamos no final.
sudo systemctl stop usbmuxd.service usbmuxd.socket 2>/dev/null || sudo systemctl stop usbmuxd || true
# O driver apple_mfi_fastcharge também se conecta ao iPhone e quebra o timing do checkm8.
# O Ubuntu o recarrega a cada reconexão, então ficamos descarregando enquanto o boot roda.
sudo sh -c 'while true; do modprobe -r apple_mfi_fastcharge 2>/dev/null; sleep 0.2; done' &
MFI_LOOP=$!
trap 'sudo kill $MFI_LOOP 2>/dev/null; sudo systemctl start usbmuxd >/dev/null 2>&1 || true' EXIT

log "Etapa 1/2: checkm8 + pongoOS (palera1n)"
echo "    Se parar em 'Booting pongoOS...' mas o iPhone mostrar o pongoOS, aperte Ctrl+C."
echo "    Se der 'Timed out waiting for download mode': coloque em DFU de novo e repita."
sudo PALERA1N_BYPASS_PASSCODE_CHECK=1 timeout 150 "$TOOLS/palera1n" -l -p -k "$TOOLS/Pongo.bin" || true

log "Etapa 2/2: enviando ${IMG#$ROOT/} para o pongoOS"
echo "    Quando o iPhone começar a mostrar o m1n1/texto do Linux, pode apertar Ctrl+C."
printf '/send %s\nbootm\n' "$IMG" | sudo "$TOOLS/pongoterm"
