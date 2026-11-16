#!/usr/bin/env bash
# Dá internet ao iPhone através do PC (o PC vira roteador com NAT).
#   ./scripts/08-internet.sh          liga   (pede a senha do sudo)
#   ./scripts/08-internet.sh parar    desliga (remove as regras)
# Nada fica permanente: as regras somem quando o PC reinicia.
# Rode SEM o picocom aberto (o script usa a serial para configurar o iPhone).
set -euo pipefail
source "$(dirname "$0")/../config.sh"

# (o "|| true" importa: com set -e, um teste falso no fim do laço encerraria o script em silêncio)
USB_IF="$(for d in /sys/class/net/*; do
  [ "$(basename "$(readlink -f "$d/device/driver" 2>/dev/null)")" = cdc_ncm ] && basename "$d" || true
done | head -n1)"
WAN_IF="$(ip route show default | awk '{print $5; exit}')"
[ -n "$USB_IF" ] || die "rede USB do iPhone não encontrada — ele está rodando Linux e conectado?"
[ -n "$WAN_IF" ] || die "o PC não tem rota padrão (sem internet?)"
NET="${PHONE_IP%.*}.0/24"

regras() {  # $1 = -I (inserir) ou -D (apagar)
  sudo iptables "$1" FORWARD -i "$USB_IF" -o "$WAN_IF" -j ACCEPT
  sudo iptables "$1" FORWARD -i "$WAN_IF" -o "$USB_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
  sudo iptables -t nat "$1" POSTROUTING -s "$NET" -o "$WAN_IF" -j MASQUERADE
}

if [ "${1:-}" = parar ]; then
  regras -D 2>/dev/null || true
  log "Regras removidas (ip_forward continua como estava; reinicie o PC para zerar tudo)"
  exit 0
fi

log "PC: $USB_IF (iPhone) -> $WAN_IF (internet)"
sudo ip addr replace "$HOST_IP/24" dev "$USB_IF"
sudo ip link set "$USB_IF" up
sudo sysctl -q -w net.ipv4.ip_forward=1
regras -D 2>/dev/null || true      # evita duplicar se rodar duas vezes
regras -I

# DNS: o mesmo servidor que o PC usa na interface de internet
DNS="$(resolvectl dns "$WAN_IF" 2>/dev/null | awk '{for(i=4;i<=NF;i++) if ($i ~ /^[0-9.]+$/) {print $i; exit}}')"
DNS="${DNS:-1.1.1.1}"

log "iPhone: rota padrão via $HOST_IP, DNS $DNS"
# metric 900, a mesma do usb-gadget.sh: com o Wi-Fi conectado, ele continua na frente
"$ROOT/scripts/iphone.sh" "ip route replace default via $HOST_IP metric 900 && echo 'nameserver $DNS' > /etc/resolv.conf && ping -c 2 -W 3 $DNS >/dev/null && echo 'internet OK (ping $DNS)'" 20
"$ROOT/scripts/iphone.sh" "wget -q -O /dev/null http://dl-cdn.alpinelinux.org/alpine/ && echo 'DNS + HTTP OK (alpinelinux.org)'" 30 \
  || echo "aviso: ping funcionou mas HTTP/DNS não — veja o firewall/DNS da sua rede"
log "Pronto. No iPhone: apk update && apk add i2c-tools evtest dropbear"
