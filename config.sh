# Configuração compartilhada por todos os scripts em scripts/.
# Este arquivo é lido com "source"; não execute diretamente.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"        # códigos-fonte baixados (fora do git)
BUILD="$ROOT/build"    # tudo que é gerado (fora do git)
TOOLS="$ROOT/tools"    # binários prontos: palera1n, Pongo.bin, pongoterm (fora do git)
# O kernel se recusa a compilar num caminho com espaços ("Área de trabalho"),
# então o código dele fica fora do projeto, num caminho sem espaços.
# Com sudo, $HOME vira /root; usamos sempre a pasta do usuário de verdade.
USER_HOME="$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6)"
KSRC="${KSRC:-$USER_HOME/.cache/linux-no-iphone-6/linux}"
REMOTE_BOOT="${REMOTE_BOOT:-$USER_HOME/.cache/linux-no-iphone-6/remote_boot}"
REMOTE_BOOT_REPO="https://github.com/HoolockLinux/remote_boot.git"

# --- Fontes -----------------------------------------------------------------
M1N1_REPO="https://github.com/HoolockLinux/m1n1.git"
M1N1_BRANCH="idevice"
LINUX_REPO="https://github.com/HoolockLinux/linux.git"
LINUX_BRANCH="hoolock"
DOCS_RAW="https://raw.githubusercontent.com/HoolockLinux/docs/master"
PONGOTERM_URL="https://raw.githubusercontent.com/checkra1n/pongoOS/iOS15/scripts/pongoterm.c"
PALERA1N_URL="https://github.com/palera1n/palera1n/releases/download/v2.4/palera1n-linux-x86_64"
IPSW_URL="https://github.com/blacktop/ipsw/releases/download/v3.1.741/ipsw_3.1.741_linux_x86_64.tar.gz"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/aarch64/alpine-minirootfs-3.23.2-aarch64.tar.gz"

# --- Alvo -------------------------------------------------------------------
# iPhone 6 = placa N61, chip A8 = T7000. (iPhone 6 Plus seria t7000-n56.)
DTB_NAME="t7000-n61.dtb"

# Linha de comando do kernel.
#  - console=ttySAC0: UART física (precisa de cabo serial especial; ignorado se não houver)
#  - console=tty0:    o texto aparece NA TELA do iPhone (o último console= vira /dev/console)
BOOTARGS="console=ttySAC0,115200 console=tty0 loglevel=7"
# Teste: USB em PIO em vez de DMA (patch kernel/patches/0001). Remova para voltar ao DMA.
BOOTARGS="$BOOTARGS dwc2.apple_pio=1"
# Rede via USB: o iPhone fica com .1 e o PC com .2
PHONE_IP="172.16.42.1"
HOST_IP="172.16.42.2"

JOBS="$(nproc)"
mkdir -p "$SRC" "$BUILD" "$TOOLS" "$(dirname "$KSRC")"
case "$KSRC" in *" "*|*:*) echo "ERRO: KSRC não pode ter espaços ou ':' ($KSRC)" >&2; exit 1;; esac

log() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERRO:\033[0m %s\n' "$*" >&2; exit 1; }
