#!/usr/bin/env bash
# Firmware limpo do ANS (o coprocessador da flash), tirado do iBoot do iOS 12.5.7.
#
# O iBoot do A8 traz o firmware do ANS embutido e o carrega na RAM a cada boot. O
# Linux recebe essa cópia JÁ USADA (o __DATA mudou durante o uso pelo iBoot), e dar
# a partida nela faz o firmware entrar em recuperação e abrir uma área nova no log
# de contexto; se a sessão termina sem o "dormir", o boot seguinte volta no tempo.
# Com a imagem original, o driver recarrega o firmware antes de cada partida, como
# o iBoot faz.
#
# O iBoot no IPSW é criptografado; as chaves desta versão são públicas
# (The Apple Wiki). O arquivo gerado é da Apple: fica só em build/ (fora do git).
set -euo pipefail
source "$(dirname "$0")/../config.sh"

DIR="$USER_HOME/.cache/linux-no-iphone-6/ios"
IM4P="$DIR/16H81__iPhone7,2/Firmware/all_flash/iBoot.n61.RELEASE.im4p"
mkdir -p "$DIR" "$BUILD/firmware"

if [ ! -s "$IM4P" ]; then
  log "Baixando só o iBoot do IPSW do iOS 12.5.7 (16H81, ~700 KB)"
  (cd "$DIR" && "$TOOLS/ipsw" download ipsw --device iPhone7,2 --build 16H81 \
      --pattern 'iBoot.n61.RELEASE.im4p' -y)
fi

log "Descriptografando o iBoot (chave publicada no The Apple Wiki)"
"$TOOLS/ipsw" img4 im4p extract --lookup --lookup-device iPhone7,2 --lookup-build 16H81 \
    --output "$DIR/iboot.n61.dec" "$IM4P" >/dev/null

log "Extraindo o firmware do ANS de dentro do iBoot"
python3 -I - "$DIR/iboot.n61.dec" "$BUILD/firmware/apple-ans1-n61.bin" <<'EOF'
import struct, sys
ib = open(sys.argv[1], 'rb').read()
if b'iBoot-4513.270.14' not in ib:
    sys.exit("não parece o iBoot 4513.270.14 descriptografado")
# o firmware começa na tabela de vetores ARM (b +0x18; b .; ...) e vai até o fim do iBoot
vet = struct.pack('<II', 0xea000006, 0xeafffffe)
ini = ib.find(vet, 0x40000)
fw = ib[ini:]
if b'AppleStorageProcessorANS1-710.260.6' not in fw:
    sys.exit("firmware do ANS não encontrado")
# o iBoot guarda o tamanho do que carrega: tem que bater
if struct.pack('<I', len(fw)) not in ib[:ini]:
    sys.exit("tamanho do firmware (%#x) não confere com o iBoot" % len(fw))
open(sys.argv[2], 'wb').write(fw)
print("firmware do ANS: %#x bytes (__TEXT 0x4a000 + __DATA inicial %#x), offset %#x no iBoot"
      % (len(fw), len(fw) - 0x4a000, ini))
EOF
log "Pronto: build/firmware/apple-ans1-n61.bin (vai para /lib/firmware/apple/ no 04-initramfs)"
