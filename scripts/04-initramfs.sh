#!/usr/bin/env bash
# Monta o initramfs: Alpine Linux mínimo (ARM64) + o nosso /init (rootfs/).
# O initramfs é o "disco" do Linux no iPhone: vai inteiro na RAM, pelo USB.
set -euo pipefail
source "$(dirname "$0")/../config.sh"

WORK="$BUILD/initramfs-root"
rm -rf "$WORK" && mkdir -p "$WORK"

log "Extraindo Alpine minirootfs"
# --no-same-owner: estamos sem root; o dono vira root:root no cpio (-R 0:0)
tar -xzf "$SRC/alpine-minirootfs.tar.gz" -C "$WORK" --no-same-owner --exclude='./dev/*'

log "Pacotes extras (ARM64): $EXTRA_PKGS"
python3 -I "$ROOT/host/apk-fetch.py" "$WORK" "$SRC/apk-cache" \
  "$ALPINE_MIRROR/main/aarch64" "$ALPINE_MIRROR/community/aarch64" -- $EXTRA_PKGS

log "Chave SSH do projeto (build/ssh/) -> /root/.ssh/authorized_keys"
mkdir -p "$BUILD/ssh"
[ -f "$BUILD/ssh/id_ed25519" ] || ssh-keygen -q -t ed25519 -N "" -C "linux-no-iphone6" -f "$BUILD/ssh/id_ed25519"
install -d -m 700 "$WORK/root/.ssh"
install -m 600 "$BUILD/ssh/id_ed25519.pub" "$WORK/root/.ssh/authorized_keys"

log "Programas nossos para o iPhone (device/*) -> /usr/local/bin"
mkdir -p "$BUILD/device" "$WORK/usr/local/bin"
for d in "$ROOT"/device/*/; do
  [ -f "$d/Makefile" ] || continue          # device/include/ é só cabeçalho
  n="$(basename "$d")"
  (cd "$d" && make -s OUT=../../build/device UAPI="$(dirname "$KSRC")/uapi/include")   # make não aceita espaços
  install -m 755 "$BUILD/device/$n" "$WORK/usr/local/bin/$n"
done

log "Touch: protótipo (n1.py + adt.py) em /root e firmware em /lib/firmware/apple/"
install -m 755 "$ROOT/device/n1.py" "$ROOT/host/adt.py" "$WORK/root/"
if [ -s "$BUILD/firmware/apple-n61-touch.bin" ]; then      # é da Apple: fica só no build/
  install -D -m 644 "$BUILD/firmware/apple-n61-touch.bin" "$WORK/lib/firmware/apple/n61-touch.bin"
else
  echo "aviso: sem firmware do touch (rode ./scripts/09-firmware-touch.sh)"
fi

if [ -s "$BUILD/firmware/apple-ans1-n61.bin" ]; then      # é da Apple: fica só no build/
  install -D -m 644 "$BUILD/firmware/apple-ans1-n61.bin" "$WORK/lib/firmware/apple/ans1-n61.bin"
else
  echo "aviso: sem firmware limpo do ANS (rode ./scripts/11-firmware-ans.sh)"
fi

log "Módulos do kernel (.ko) -> /lib/modules"
if [ -d "$(dirname "$KSRC")/modules/lib/modules" ]; then
  mkdir -p "$WORK/lib/modules"
  cp -a "$(dirname "$KSRC")/modules/lib/modules/." "$WORK/lib/modules/"
fi

log "Copiando arquivos do projeto (rootfs/ por cima do Alpine)"
cp -a "$ROOT/rootfs/." "$WORK/"
chmod +x "$WORK/init"
echo "$PHONE_IP" > "$WORK/etc/iphone6/phone_ip"   # vem do config.sh

log "Gerando build/initramfs.gz"
(cd "$WORK" && find . -print0 | cpio --null -o -H newc -R 0:0 --quiet) | gzip -9 > "$BUILD/initramfs.gz"
log "Pronto ($(du -h "$BUILD/initramfs.gz" | cut -f1)). Próximo: ./scripts/05-empacotar.sh"
