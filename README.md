# Linux no iPhone 6

Projeto de estudo: dar boot em Linux de verdade no **iPhone 6** (chip **A8 / T7000**,
placa **N61**), entender cada etapa e, no longo prazo, chegar a um **Android** nele.

O iOS **não é apagado nem modificado**. O Linux roda inteiro na RAM, enviado pelo USB
a cada boot. Para voltar ao iOS, basta reiniciar o iPhone.

## Como funciona a cadeia de boot

```
 PC (Ubuntu)                                    iPhone 6
 ───────────                                    ────────
 palera1n  ── exploit checkm8 (USB/DFU) ──────▶ BootROM: falha de hardware, não tem correção
           ── envia Pongo.bin ────────────────▶ pongoOS: mini-sistema que recebe arquivos via USB
 pongoterm ── /send m1n1-linux.bin; bootm ────▶ m1n1:    prepara o hardware e o device tree
                                                  └──▶ kernel Linux (Image.gz + t7000-n61.dtb)
                                                         └──▶ initramfs (Alpine + nosso /init)
 picocom   ◀──────── USB serial (/dev/ttyACM0) ─────────── shell
 rede      ◀──────── USB rede 172.16.42.1 ◀──▶ 172.16.42.2
```

- **checkm8**: falha na BootROM (código gravado no chip). Funciona em qualquer versão do
  iOS, mas não sobrevive ao desligar, então o boot é *tethered* (precisa do PC toda vez).
- **pongoOS**: ambiente de pré-boot do time do checkra1n.
- **m1n1**: bootloader do Asahi Linux (Linux nos Macs M1), adaptado pela
  [Hoolock Linux](https://github.com/HoolockLinux) para chips A7–A11.
- **Kernel**: Linux da Hoolock (branch `hoolock`). O suporte básico ao A8 já entrou no
  kernel oficial a partir do 6.13.

## O que funciona hoje (A8, segundo a Hoolock)

| Funciona | Não funciona (ainda) |
|---|---|
| CPU (2 núcleos), cpufreq, RAM 1 GB | Touchscreen |
| Tela (framebuffer) + brilho | GPU (PowerVR): sem driver aberto |
| Botões | Armazenamento interno (em desenvolvimento) |
| USB modo dispositivo (serial + rede), em PIO* | Wi-Fi, Bluetooth, áudio, câmera, modem |
| I2C, GPIO, watchdog, RTC | |

Fonte: [HoolockLinux/docs, features/A8.md](https://github.com/HoolockLinux/docs/blob/master/features/A8.md).
\* Neste projeto, via boot pelo checkra1n: USB só funciona em PIO e com o patch
`kernel/patches/0002` (ver docs/ROTEIRO.md).

## Uso

```bash
./scripts/00-dependencias.sh   # 1x: instala pacotes (pede senha do sudo)
./scripts/01-baixar.sh         # baixa kernel, m1n1, pongoOS, palera1n, Alpine
./scripts/02-m1n1.sh           # compila m1n1 + pongoterm
./scripts/03-kernel.sh         # compila o kernel (use "menuconfig" para explorar)
./scripts/04-initramfs.sh      # monta o initramfs (rootfs/ + Alpine)
./scripts/05-empacotar.sh      # gera build/m1n1-linux.bin
sudo ./scripts/06-boot-checkra1n.sh boot  # iPhone em DFU → Linux (funciona neste PC AMD)
./scripts/07-conectar.sh       # rede USB + shell no iPhone pela serial
```

Métodos alternativos de boot (não funcionaram neste PC AMD, mas são os oficiais da Hoolock):
`./scripts/06-boot.sh` (palera1n + pongoOS novo) e `sudo ./scripts/06-boot-iboot.sh` (gaster + iBoot).

> **PCs com processador AMD:** palera1n e gaster falham logo depois do exploit; o
> checkra1n 0.12.4 funciona. O driver `apple_mfi_fastcharge` e o `usbmuxd` também
> atrapalham; os scripts de boot os desativam enquanto rodam. Para o ModemManager não
> mandar "AT" no shell: `sudo cp host/99-linux-no-iphone6.rules /etc/udev/rules.d/`.
> Detalhes do que foi descoberto: [docs/ROTEIRO.md](docs/ROTEIRO.md), fase 1.

Primeiro teste recomendado: `./scripts/06-boot.sh m1n1` (só o m1n1, sem Linux). Se a tela
do iPhone mostrar o m1n1, a cadeia checkm8 → pongoOS → m1n1 está funcionando.

## Estrutura

```
config.sh             variáveis comuns (URLs, IPs, bootargs)
scripts/              passo a passo numerado
kernel/iphone6.config nossas opções de kernel (somadas ao config da Hoolock)
rootfs/               arquivos copiados para o initramfs (o /init é nosso)
docs/ROTEIRO.md       fases do projeto, até o Android
src/ build/ tools/    gerados/baixados (fora do git)
```

## Créditos

Hoolock Linux (Nick Chan e outros), Asahi Linux (m1n1), checkra1n (pongoOS, checkm8),
palera1n, Konrad Dybcio (primeiro port linux-apple), Corellium (Project Sandcastle).
