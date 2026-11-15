#!/usr/bin/env bash
# Instala tudo que o PC (Ubuntu 24.04) precisa para compilar e dar boot.
# Rode UMA vez:  ./scripts/00-dependencias.sh   (vai pedir sua senha do sudo)
set -euo pipefail
source "$(dirname "$0")/../config.sh"

log "Instalando pacotes do sistema (apt)"
sudo apt-get update
sudo apt-get install -y \
  build-essential git curl unzip bc flex bison cpio gzip \
  clang lld llvm \
  gcc-aarch64-linux-gnu \
  libssl-dev libelf-dev libncurses-dev \
  device-tree-compiler \
  libusb-1.0-0-dev \
  rustup \
  picocom telnet irecovery xxd

log "Instalando Rust (necessário para o m1n1)"
rustup toolchain install stable --profile minimal
rustup default stable
rustup target add aarch64-unknown-none-softfloat

log "Pronto. Próximo passo: ./scripts/01-baixar.sh"
