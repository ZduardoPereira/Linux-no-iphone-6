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
  n="$(basename "$d")"
  (cd "$d" && make -s OUT=../../build/device UAPI="$(dirname "$KSRC")/uapi/include")   # make não aceita espaços
  install -m 755 "$BUILD/device/$n" "$WORK/usr/local/bin/$n"
done

log "Copiando arquivos do projeto (rootfs/ por cima do Alpine)"
cp -a "$ROOT/rootfs/." "$WORK/"
chmod +x "$WORK/init"
echo "$PHONE_IP" > "$WORK/etc/iphone6/phone_ip"   # vem do config.sh

log "Gerando build/initramfs.gz"
(cd "$WORK" && find . -print0 | cpio --null -o -H newc -R 0:0 --quiet) | gzip -9 > "$BUILD/initramfs.gz"
log "Pronto ($(du -h "$BUILD/initramfs.gz" | cut -f1)). Próximo: ./scripts/05-empacotar.sh"
