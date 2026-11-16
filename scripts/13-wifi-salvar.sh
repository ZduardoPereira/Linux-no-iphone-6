#!/usr/bin/env bash
# Guarda no PC as redes Wi-Fi salvas no iPhone, para ele conectar sozinho a cada boot.
#   ./scripts/13-wifi-salvar.sh      (com o iPhone no Linux, depois de um "wifi conectar")
#
# Com o sistema na RAM, o que muda no iPhone some ao reiniciar, inclusive as redes do
# "wifi conectar". Este script copia o arquivo delas (nome + chave derivada da senha; a
# senha em texto não fica nele) para build/wifi/, fora do git, e o 04-initramfs.sh o
# põe de volta no iPhone a cada boot. Com o sistema na flash não precisa: as redes
# ficam salvas nela.
# Para o iPhone esquecer todas no próximo boot: rm build/wifi/wpa_supplicant.conf
set -euo pipefail
source "$(dirname "$0")/../config.sh"

DEST="$BUILD/wifi/wpa_supplicant.conf"
mkdir -p "$BUILD/wifi" && chmod 700 "$BUILD/wifi"
umask 077

# entre marcas: qualquer outra coisa que o login no iPhone imprima não entra no arquivo
"$ROOT/scripts/ssh.sh" 'echo @@inicio; cat /etc/wpa_supplicant/wpa_supplicant.conf; echo @@fim' |
  sed -n '/^@@inicio$/,/^@@fim$/{//!p}' > "$DEST.novo" ||
  { rm -f "$DEST.novo"; die "não consegui ler as redes do iPhone (ele está no Linux? ./scripts/ssh.sh funciona?)"; }

N="$(grep -c '^network={' "$DEST.novo" || true)"
if [ "$N" -eq 0 ]; then
  rm -f "$DEST.novo"; die "o iPhone não tem rede salva (nele: wifi conectar \"Nome da rede\")"
fi
if grep -q '#psk=' "$DEST.novo"; then
  rm -f "$DEST.novo"; die "o arquivo do iPhone tem uma senha em texto (#psk=): não copiei"
fi
mv "$DEST.novo" "$DEST"
log "$N rede(s) Wi-Fi guardada(s) em build/wifi/ (fora do git)"
log "Próximo: ./scripts/04-initramfs.sh && ./scripts/05-empacotar.sh (no boot seguinte o Wi-Fi conecta sozinho)"
