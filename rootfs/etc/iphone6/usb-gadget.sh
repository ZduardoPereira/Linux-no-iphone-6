# Configura o USB do iPhone como "gadget": o PC passa a ver
#   - uma porta serial  (ACM -> /dev/ttyACM0 no PC, /dev/ttyGS0 no iPhone)
#   - uma placa de rede (NCM -> usb0 no iPhone)
# Tudo via configfs: cada diretório/arquivo abaixo é uma configuração do kernel.
#
# Se o PC não reconhecer o iPhone em high-speed (480 Mbit/s) em 15 s, tenta
# full-speed (12 Mbit/s). O resultado aparece na tela.

G=/sys/kernel/config/usb_gadget/iphone6
IP="$(cat /etc/iphone6/phone_ip)"

mkdir -p "$G" && cd "$G" || { echo "usb-gadget: configfs indisponível"; return 0; }
echo 0x1d6b > idVendor          # Linux Foundation
echo 0x0104 > idProduct         # Multifunction Composite Gadget
mkdir -p strings/0x409
echo "Linux-no-iphone-6" > strings/0x409/manufacturer
echo "iPhone 6 (Linux)"  > strings/0x409/product
echo "n61"               > strings/0x409/serialnumber

mkdir -p configs/c.1/strings/0x409
echo "serial+rede" > configs/c.1/strings/0x409/configuration
mkdir -p functions/acm.usb0 functions/ncm.usb0
# MACs fixos (senão mudam a cada boot e a interface no PC troca de nome)
echo 02:69:70:68:36:01 > functions/ncm.usb0/dev_addr    # lado do iPhone
echo 02:69:70:68:36:02 > functions/ncm.usb0/host_addr   # lado do PC (enx026970683602)
ln -sf "$G/functions/acm.usb0" configs/c.1/
ln -sf "$G/functions/ncm.usb0" configs/c.1/
cd /

UDC="$(ls /sys/class/udc 2>/dev/null | head -n1)"
[ -n "$UDC" ] || { echo "usb-gadget: nenhum controlador USB (UDC) encontrado"; return 0; }

udc_state() { cat "/sys/class/udc/$UDC/state" 2>/dev/null; }

usb_tenta() {  # $1 = high-speed | full-speed
  echo "" > "$G/UDC" 2>/dev/null
  echo "$1" > "$G/max_speed"
  echo "$UDC" > "$G/UDC" || { echo "usb-gadget: falha ao ativar em $UDC"; return 1; }
  echo "usb-gadget: tentando $1..."
  for i in $(seq 15); do
    [ "$(udc_state)" = configured ] && { echo "usb-gadget: CONECTADO ao PC em $1"; return 0; }
    sleep 1
  done
  echo "usb-gadget: $1 falhou (estado: $(udc_state))"
  return 1
}

diagnostico() {
  mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null
  echo "----- dwc2 fifo (depois de configurar) -----"
  cat /sys/kernel/debug/usb/"$UDC"/fifo 2>/dev/null || cat /sys/kernel/debug/"$UDC"/fifo 2>/dev/null
  echo "----- dmesg dwc2 -----"
  dmesg | grep -iE "dwc2|udc|gadget|fifo" | grep -v "new device is" | tail -8
  echo "----------------------"
}

# PIO ou DMA é decidido na linha de comando do kernel (dwc2.apple_pio=1, ver config.sh)
[ "$(cat /sys/module/dwc2/parameters/apple_pio 2>/dev/null)" = Y ] && DMA=PIO || DMA=DMA
echo "usb-gadget: modo de transferência = $DMA"
if usb_tenta high-speed; then
  USB_MODO="$DMA high-speed"
elif usb_tenta full-speed; then
  USB_MODO="$DMA full-speed"
else
  USB_MODO="FALHOU ($DMA)"
fi
echo "usb-gadget: resultado = $USB_MODO"
diagnostico

# Rede
ip link set lo up
for i in 1 2 3 4 5; do [ -e /sys/class/net/usb0 ] && break; sleep 1; done
ip addr add "$IP/24" dev usb0 2>/dev/null && ip link set usb0 up
# Internet pelo PC (fase 3): o PC é o roteador. Só funciona depois de rodar
# ./scripts/08-internet.sh no PC, que também ajusta o DNS.
# metric 900: rota de reserva. Com o Wi-Fi conectado (rota dele: metric ~300), a
# internet sai pelo Wi-Fi; o cabo continua para o SSH (rede 172.16.42.0/24).
ip route add default via "${IP%.*}.2" metric 900 2>/dev/null
