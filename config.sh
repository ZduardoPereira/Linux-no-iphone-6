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
ALPINE_MIRROR="https://dl-cdn.alpinelinux.org/alpine/v3.23"
# Pacotes que vão dentro do initramfs (o iPhone ainda não tem armazenamento)
EXTRA_PKGS="dropbear dropbear-scp openssh-sftp-server i2c-tools evtest lm-sensors libgpiod e2fsprogs device-mapper iw wpa_supplicant wireless-regdb"
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
# Touch (fase 5): o firmware vai numa transferência SPI só (65 KB); o padrão do spidev é 4 KB
BOOTARGS="$BOOTARGS spidev.bufsiz=131072"
# Pânico do kernel: fica parado na tela (para fotografar) em vez de reiniciar na hora
BOOTARGS="$BOOTARGS panic=0"
# Flash interna (fase 7): o disco do iOS tem GPT sem o "MBR de proteção" no setor 0;
# sem "gpt" o Linux não reconhece as partições do /dev/ans0
BOOTARGS="$BOOTARGS gpt"
# Fase 8 (Wi-Fi): o sistema sobe na RAM e o ANS (flash) nem liga, para um travamento
# nos testes do PCIe não pegar o ANS acordado. Tirar quando o Wi-Fi estiver pronto.
BOOTARGS="$BOOTARGS semflash"
# O Wi-Fi conecta sozinho no boot se houver rede salva (./scripts/13-wifi-salvar.sh).
# Para o boot não ligar o Wi-Fi: BOOTARGS="$BOOTARGS semwifi"
# Rede via USB: o iPhone fica com .1 e o PC com .2
PHONE_IP="172.16.42.1"
HOST_IP="172.16.42.2"

JOBS="$(nproc)"
mkdir -p "$SRC" "$BUILD" "$TOOLS" "$(dirname "$KSRC")"
case "$KSRC" in *" "*|*:*) echo "ERRO: KSRC não pode ter espaços ou ':' ($KSRC)" >&2; exit 1;; esac

log() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERRO:\033[0m %s\n' "$*" >&2; exit 1; }

# Wi-Fi: NVRAM do iOS para o módulo deste aparelho, como o brcmfmac mostra no boot
# ("OTP: module=CORO vendor=u version=3.7" -> CORO_V-u__m-3.7). Ver scripts/12-firmware-wifi.sh.
WIFI_NVRAM="CORO_V-u__m-3.7"
