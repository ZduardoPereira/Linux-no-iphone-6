#!/usr/bin/env bash
# Firmware do Wi-Fi (Broadcom BCM4345 B1, módulo "tempranillo"), tirado do iOS 12.5.7.
#
# O iOS guarda em /usr/share/firmware/wifi/C-4345__s-B1/:
#   tempranillo.trx   firmware que roda no chip (imagem crua da RAM dele)
#   tempranillo.clmb  tabela de regulamentação (canais/potência por país)
#   tempranillo.txcb  limites de potência de transmissão
#   P-tempranillo_M-<módulo>_V-<fabricante>__m-<versão>.txt   NVRAM (parâmetros
#                     de rádio da placa), uma por fabricante/revisão do módulo
# Qual NVRAM usar está gravado na OTP do chip; o brcmfmac do projeto mostra no boot:
#   brcmfmac 0000:01:00.0: OTP: module=CORO vendor=u version=3.7
# (WIFI_NVRAM no config.sh). O endereço MAC vem do ADT deste aparelho.
#
# O sistema de arquivos do iOS fica dentro do IPSW (2,9 GB, sem compressão no ZIP):
# baixamos só esse pedaço, por intervalos HTTP, retomando se cair. Os arquivos são da
# Apple/Broadcom: ficam só em build/ (fora do git).
set -euo pipefail
source "$(dirname "$0")/../config.sh"

DIR="$USER_HOME/.cache/linux-no-iphone-6/ios"
FS="$DIR/rootfs"; WIFI="$DIR/wifi"
SAIDA="$BUILD/firmware/wifi"
ORIGEM="$WIFI/usr/share/firmware/wifi/C-4345__s-B1"
NOME="brcmfmac4345b1-pcie.apple,tempranillo"
mkdir -p "$FS" "$WIFI" "$SAIDA"

if [ ! -s "$ORIGEM/tempranillo.trx" ]; then
  URL="$("$TOOLS/ipsw" download ipsw --device iPhone7,2 --build 16H81 --urls 2>/dev/null |
         grep -o 'https://[^ ]*\.ipsw' | head -1)"
  [ -n "$URL" ] || die "não achei a URL do IPSW 16H81"
  MANIFESTO="$BUILD/ipsw-manifest/16H81__iPhone7,2/BuildManifest.plist"
  [ -s "$MANIFESTO" ] || die "falta $MANIFESTO (rode ./scripts/11-firmware-ans.sh)"
  DMG="$(python3 -I -c 'import plistlib,sys
m=plistlib.load(open(sys.argv[1],"rb"))
print(m["BuildIdentities"][0]["Manifest"]["OS"]["Info"]["Path"])' "$MANIFESTO")"

  if [ ! -s "$FS/rootfs-16H81.dmg" ]; then
    log "Localizando $DMG dentro do IPSW"
    read -r OFF TAM < <(python3 -I - "$URL" "$DMG" <<'EOF'
# deslocamento e tamanho dos dados de um arquivo guardado SEM compressão no ZIP remoto
import io, socket, struct, sys, urllib.request, zipfile
_o = socket.getaddrinfo        # a Apple derruba conexões IPv6 longas: só IPv4
socket.getaddrinfo = lambda *a, **k: [x for x in _o(*a, **k) if x[0] == socket.AF_INET]
URL, NOME = sys.argv[1:3]
def faixa(a, b):
    r = urllib.request.Request(URL, headers={"Range": "bytes=%d-%d" % (a, b)})
    return urllib.request.urlopen(r, timeout=60).read()
class Remoto(io.RawIOBase):
    def __init__(s):
        r = urllib.request.Request(URL, method="HEAD")
        s.tam = int(urllib.request.urlopen(r, timeout=60).headers["Content-Length"]); s.pos = 0
    def seekable(s): return True
    def readable(s): return True
    def tell(s): return s.pos
    def seek(s, o, w=0):
        s.pos = o if w == 0 else s.pos + o if w == 1 else s.tam + o
        return s.pos
    def readinto(s, b):
        n = min(len(b), s.tam - s.pos)
        if n <= 0: return 0
        d = faixa(s.pos, s.pos + n - 1); b[:len(d)] = d; s.pos += len(d); return len(d)
i = zipfile.ZipFile(io.BufferedReader(Remoto(), 1 << 20)).getinfo(NOME)
if i.compress_type != 0: sys.exit("o DMG está comprimido no ZIP: não suportado")
h = faixa(i.header_offset, i.header_offset + 29)
print(i.header_offset + 30 + sum(struct.unpack("<HH", h[26:30])), i.file_size)
EOF
    )
    log "Baixando o sistema de arquivos do iOS ($((TAM / 1000000)) MB; retoma se cair)"
    P="$FS/rootfs-16H81.dmg.parcial"; touch "$P"
    for t in $(seq 1 50); do
      JA=$(stat -c %s "$P"); [ "$JA" -ge "$TAM" ] && break
      curl -4 -s --fail -r "$((OFF + JA))-$((OFF + TAM - 1))" "$URL" >> "$P" || sleep 3
      echo "  $(( $(stat -c %s "$P") * 100 / TAM ))%"
    done
    [ "$(stat -c %s "$P")" -eq "$TAM" ] || die "download incompleto (rode de novo para continuar)"
    mv "$P" "$FS/rootfs-16H81.dmg"
  fi

  log "Extraindo a partição APFS do DMG e a pasta do firmware (7-Zip lê APFS)"
  "$TOOLS/ipsw" disk dmg "$FS/rootfs-16H81.dmg" -r 'Apple_APFS' -o "$FS/rootfs-16H81.apfs" >/dev/null
  7z x -y -o"$WIFI" "$FS/rootfs-16H81.apfs" 'usr/share/firmware/wifi/*' -r >/dev/null
  rm -f "$FS/rootfs-16H81.apfs"          # 5,4 GB; refaz-se em 1 min a partir do DMG
fi

NVRAM="$ORIGEM/P-tempranillo_M-${WIFI_NVRAM}.txt"
[ -s "$NVRAM" ] || die "não existe $(basename "$NVRAM") (veja WIFI_NVRAM no config.sh)"

# endereço MAC deste aparelho (ADT /arm-io/uart4/wlan local-mac-address)
MAC="$(python3 -I "$ROOT/host/adt.py" "$BUILD/adt.bin" -n arm-io/uart4/wlan |
       awk '$1 == "local-mac-address" {print $2}' | sed 's/../&:/g; s/:$//')"
[[ "$MAC" =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]] || die "MAC do Wi-Fi não encontrado no ADT"

log "Gerando build/firmware/wifi/$NOME.* (NVRAM $WIFI_NVRAM)"
install -m 644 "$ORIGEM/tempranillo.trx"  "$SAIDA/$NOME.bin"
install -m 644 "$ORIGEM/tempranillo.clmb" "$SAIDA/$NOME.clm_blob"
install -m 644 "$ORIGEM/tempranillo.txcb" "$SAIDA/$NOME.txcap_blob"
{ cat "$NVRAM"; echo "macaddr=$MAC"; } > "$SAIDA/$NOME.txt"
log "Pronto (vão para /lib/firmware/brcm/ no 04-initramfs)"
