#!/usr/bin/env bash
# Monta o compilador cruzado para programas C normais (com libc) no iPhone:
# sysroot do Alpine ARM64 (musl + compiler-rt) usado pelo host/iphone-cc.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
SR="$USER_HOME/.cache/linux-no-iphone-6/sysroot"
mkdir -p "$SR"
log "Sysroot ARM64 (musl-dev + compiler-rt) em $SR"
python3 -I "$ROOT/host/apk-fetch.py" "$SR" "$SRC/apk-cache" \
  "$ALPINE_MIRROR/main/aarch64" "$ALPINE_MIRROR/community/aarch64" -- musl-dev compiler-rt
log "Pronto. Teste: host/iphone-cc programa.c -o programa"
