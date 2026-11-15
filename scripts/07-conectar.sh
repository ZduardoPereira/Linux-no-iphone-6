#!/usr/bin/env bash
# Abre um shell no Linux do iPhone, pela porta serial USB.
#   ./scripts/07-conectar.sh        shell serial (picocom; sair: Ctrl+A e depois Ctrl+X)
#   ./scripts/07-conectar.sh rede   só configura a rede USB do PC (172.16.42.2)
set -euo pipefail
source "$(dirname "$0")/../config.sh"

configura_rede() {
  local iface
  # A placa de rede do iPhone aparece no PC com o driver cdc_ncm
  iface="$(for d in /sys/class/net/*; do
             [ "$(basename "$(readlink -f "$d/device/driver" 2>/dev/null)")" = cdc_ncm ] && basename "$d"
           done | head -n1)"
  [ -n "$iface" ] || { echo "Placa de rede do iPhone não encontrada (ainda)."; return 1; }
  log "Configurando $iface com $HOST_IP"
  sudo ip addr replace "$HOST_IP/24" dev "$iface"
  sudo ip link set "$iface" up
  ping -c 2 -W 1 "$PHONE_IP" && log "Rede OK: o iPhone responde em $PHONE_IP"
}

if [ "${1:-}" = "rede" ]; then configura_rede; exit; fi

log "Aguardando a porta serial do iPhone (/dev/ttyACM*)"
until ls /dev/ttyACM* >/dev/null 2>&1; do sleep 1; done
PORTA="$(ls /dev/ttyACM* | head -n1)"
configura_rede || true
log "Conectando em $PORTA (sair: Ctrl+A, Ctrl+X)"
sudo picocom -q -b 115200 "$PORTA"
