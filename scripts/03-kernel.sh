#!/usr/bin/env bash
# Compila o kernel Linux e o device tree do iPhone 6.
#   ./scripts/03-kernel.sh            compila
#   ./scripts/03-kernel.sh menuconfig abre o menu de opções antes (para aprender/explorar)
set -euo pipefail
source "$(dirname "$0")/../config.sh"

K="$KSRC"
[ -d "$K" ] || die "kernel não encontrado em $K — rode ./scripts/01-baixar.sh"
MK=(make -C "$K" -j"$JOBS" LLVM=1 ARCH=arm64)

if [ ! -f "$K/.config" ]; then
  log "Criando .config a partir do exemplo da Hoolock"
  cp "$SRC/config_16k" "$K/.config"
  # O A8 só aceita páginas de memória de 4K (A9 em diante usa 16K).
  "$K/scripts/config" --file "$K/.config" \
    -d ARM64_16K_PAGES -e ARM64_4K_PAGES \
    --set-str LOCALVERSION "-iphone6"
fi
# Patches nossos (kernel/patches/*.patch), aplicados em ordem e uma vez só.
# O registro do que já foi aplicado fica em $K/.iphone6-patches (nome do patch).
# Se você alterar um patch já aplicado, recomece com: git -C "$KSRC" checkout . && rm "$KSRC/.iphone6-patches"
REG="$K/.iphone6-patches"
touch "$REG"
for p in "$ROOT"/kernel/patches/*.patch; do
  [ -e "$p" ] || continue
  n="$(basename "$p")"
  if grep -qxF "$n" "$REG"; then
    log "Patch já aplicado: $n"
  else
    log "Aplicando patch: $n"
    git -C "$K" apply "$p"
    echo "$n" >> "$REG"
  fi
done
# Ajustes nossos ficam em kernel/iphone6.config (versionado no git); aplicados sempre,
# para que mudanças nesse arquivo valham na próxima compilação.
cp "$ROOT/kernel/iphone6.config" "$K/.iphone6.config"
(cd "$K" && ARCH=arm64 LLVM=1 scripts/kconfig/merge_config.sh -m -Q .config .iphone6.config >/dev/null)
"${MK[@]}" olddefconfig

[ "${1:-}" = "menuconfig" ] && "${MK[@]}" menuconfig

log "Compilando kernel (a primeira vez demora; ~10-20 min)"
"${MK[@]}" Image.gz "apple/$DTB_NAME"

cp "$K/arch/arm64/boot/Image.gz"              "$BUILD/Image.gz"
cp "$K/arch/arm64/boot/dts/apple/$DTB_NAME"   "$BUILD/$DTB_NAME"
cp "$K/.config"                               "$BUILD/kernel.config"
# Cabeçalhos do kernel para programas do iPhone (device/*), sem espaços no caminho
"${MK[@]}" -s headers_install INSTALL_HDR_PATH="$(dirname "$KSRC")/uapi" >/dev/null
log "Pronto: build/Image.gz e build/$DTB_NAME. Próximo: ./scripts/04-initramfs.sh"
