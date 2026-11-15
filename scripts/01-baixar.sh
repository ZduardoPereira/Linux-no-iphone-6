#!/usr/bin/env bash
# Baixa códigos-fonte e binários prontos. Pode rodar de novo para atualizar.
set -euo pipefail
source "$(dirname "$0")/../config.sh"

clone_ou_atualiza() {  # repo branch destino [opções extras do clone]
  local repo=$1 branch=$2 dest=$3; shift 3
  if [ -d "$dest/.git" ]; then
    log "Atualizando $(basename "$dest")"
    git -C "$dest" pull --ff-only
    git -C "$dest" submodule update --init --recursive
  else
    log "Clonando $(basename "$dest") ($branch)"
    git clone --depth 1 -b "$branch" "$@" "$repo" "$dest"
  fi
}

baixa() {  # url destino
  [ -s "$2" ] && { log "Já existe: ${2#$ROOT/}"; return; }
  log "Baixando ${2#$ROOT/}"
  curl -fL --retry 3 -o "$2.part" "$1" && mv "$2.part" "$2"
}

# Bootloader m1n1 (versão da Hoolock para iPhones/iPads antigos)
clone_ou_atualiza "$M1N1_REPO" "$M1N1_BRANCH" "$SRC/m1n1" --recursive --shallow-submodules
# Kernel Linux (versão da Hoolock, com suporte a A7–A11)
clone_ou_atualiza "$LINUX_REPO" "$LINUX_BRANCH" "$KSRC"

# Config de exemplo do kernel (o script 03 converte para páginas de 4K, exigidas pelo A8)
baixa "$DOCS_RAW/config_16k" "$SRC/config_16k"
# pongoOS pronto (o ambiente que recebe o nosso m1n1 pelo USB)
baixa "$DOCS_RAW/binaries/Pongo.bin" "$TOOLS/Pongo.bin"
# Código do pongoterm (o script 02 compila)
baixa "$PONGOTERM_URL" "$SRC/pongoterm.c"
# palera1n: dispara o exploit checkm8 e carrega o pongoOS
baixa "$PALERA1N_URL" "$TOOLS/palera1n" && chmod +x "$TOOLS/palera1n"
# Alpine Linux mínimo para ARM64 (base do nosso initramfs)
baixa "$ALPINE_URL" "$SRC/alpine-minirootfs.tar.gz"

# --- Método iBoot (06-boot-iboot.sh) ---
if [ ! -d "$REMOTE_BOOT/.git" ]; then
  log "Clonando remote_boot"
  git clone --depth 1 "$REMOTE_BOOT_REPO" "$REMOTE_BOOT"
fi
# os submódulos estão como SSH no .gitmodules; forçamos HTTPS
git -C "$REMOTE_BOOT" -c url."https://github.com/".insteadOf="git@github.com:" \
  submodule update --init --recursive --depth 1
make -C "$REMOTE_BOOT/vendor/gaster" libusb_dyn
make -C "$REMOTE_BOOT/vendor/hBootPatcher"
# ipsw (baixa partes do firmware da Apple e monta imagens IMG4)
if [ ! -x "$TOOLS/ipsw" ]; then
  T="$(mktemp -d)"
  curl -fL --retry 3 -o "$T/$(basename "$IPSW_URL")" "$IPSW_URL"
  curl -fL --retry 3 -o "$T/checksums.txt" "$(dirname "$IPSW_URL")/checksums.txt"
  (cd "$T" && grep " $(basename "$IPSW_URL")\$" checksums.txt | sha256sum -c)
  tar -xzf "$T/$(basename "$IPSW_URL")" -C "$T" ipsw && mv "$T/ipsw" "$TOOLS/ipsw"
  rm -rf "$T"
fi

log "Pronto. Próximo passo: ./scripts/02-m1n1.sh"
