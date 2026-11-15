# Roteiro: do primeiro boot ao Android

Cada fase ensina uma camada do sistema. Não pule fases: o Android depende de todas.

## Fase 1: Cadeia de boot ✅ (15/11/2026)
- [x] Instalar dependências e baixar fontes (scripts 00–01)
- [x] Compilar m1n1 e kernel (Hoolock 7.3-rc1, páginas de 4K)
- [x] Boot: checkra1n 0.12.4 → pongoOS antigo → `bootr` → nosso stub → m1n1 → Linux
- [x] Texto do Linux na tela e `/init` próprio
- [x] Shell pela USB serial (`07-conectar.sh`)
- [x] Rede USB funcionando (`ping 172.16.42.1`)

**Você aprende:** BootROM/DFU, exploits de boot, bootloaders, device tree, cmdline do kernel.

### O que foi preciso descobrir (neste PC AMD)
1. **palera1n e gaster** falham logo após o checkm8 (timing do USB da AMD). Só o
   **checkra1n 0.12.4** passa (ele tem uma estratégia "probabilística" de fallback).
2. O pongoOS do checkra1n é de 2021 e não tem `bootm` (boot do m1n1). Solução:
   `boot/pongo-stub/` — 576 bytes que fazem o papel do `bootm` usando o `bootr`:
   restauram `firmware-version` no ADT, movem o m1n1 para cima de `topOfKernelData`
   e pulam para `_start` (+0x800).
3. **USB por DMA não funciona** nesse caminho (causa ainda em aberto) → usamos PIO
   (`dwc2.apple_pio=1`, patch `kernel/patches/0001`).
4. Em PIO, o envio travava por um **bug no driver dwc2**: `DTXFSTS` lido pelo número
   do FIFO em vez do número do endpoint (patch `kernel/patches/0002`). Candidato a
   ser enviado ao kernel oficial.
5. Atrapalham no PC: driver `apple_mfi_fastcharge`, `usbmuxd` e ModemManager
   (ver `host/70-linux-no-iphone6.rules`).

### Pendências da fase 1
- [ ] Descobrir por que o DMA do USB falha (pongoOS antigo deixa algo configurado?)
- [ ] Enviar o patch do `DTXFSTS` para o kernel oficial (linux-usb)
- [ ] 2º núcleo da CPU (conferir com `nproc`)

## Fase 2: Conhecer o hardware por dentro do Linux ✅
- [x] Explorar `/sys`, `/proc/device-tree`, `dmesg`: o que o kernel achou
- [x] Ler o log do m1n1 (`/dev/mtd0`) e o device tree original da Apple (`/dev/mtd1`)
- [x] Leitor do ADT (`host/adt.py`) e mapa completo em [HARDWARE.md](HARDWARE.md)
- [x] Brilho (`/sys/class/backlight`, 0–2047) e cpufreq (300 MHz–1,39 GHz, `schedutil`)
- [ ] Testar os botões lendo `/dev/input/event0`
- [ ] Ler `t7000.dtsi` no kernel e comparar nó a nó com o ADT (`build/adt.txt`)
- [ ] Investigar o 2º núcleo (IORVBAR) e o DMA do USB

**Você aprende:** device tree, sysfs, drivers de plataforma, como o kernel descobre o hardware.

## Fase 3: Userspace de verdade ✅ (o básico)
- [x] Internet no iPhone através do PC (NAT): `./scripts/08-internet.sh`
- [x] `apk add` funcionando dentro do iPhone
- [x] SSH (dropbear) pela rede USB: `./scripts/ssh.sh` (chave do projeto em `build/ssh/`)
- [x] Pacotes ARM64 embutidos no initramfs pelo PC (`host/apk-fetch.py`, lista em `config.sh`)
- [ ] Confirmar no próximo boot: SSH e ferramentas já presentes sem instalar nada
- [ ] Rootfs maior via rede (NBD/NFS), já que o armazenamento interno ainda não funciona
- [ ] Trocar o shell da tela por algo útil (ex.: mostrar IP, uptime, temperatura)

**Você aprende:** init, rede Linux, NAT, gerenciador de pacotes, SSH.

## Fase 4: Primeiro driver ← em andamento
### 4.1 Vibração (TI DRV2604) ✅
- [x] Dados do ADT: `i2c1`, endereço `0x5a`, enable no GPIO 76 (`host/adt.py -n arm-io/i2c1/vib-pwm`)
- [x] Patch de device tree `kernel/patches/0003` (ativa `i2c1`, descreve o chip) + `CONFIG_INPUT_DRV260X_HAPTICS`
- [x] Programa de teste `device/vibrar` (C sem libc, syscalls diretas: openat/ioctl/write)
- [x] Boot: `i2cdetect -y 1` mostra o chip em 0x5a (`UU`); calibração automática vibra no boot
- [x] `vibrar 300` faz o iPhone vibrar ✅ (15/11/2026)

### Depois
1. **Sensores de temperatura** (`tempsensor0..4`): primeiro driver em C (hwmon)
2. **Bateria/carregador**: ler nível de bateria
3. **Touchscreen** (Apple Z2, SPI): o mais importante para o Android
4. **Armazenamento interno**: acompanhar o trabalho em andamento da Hoolock

**Você aprende:** device tree, I2C, GPIO, force-feedback/input, syscalls, depois drivers em C.

> Referência para estudo, **não verificada**: o projeto
> [ITSMEFRTRUST/linux-on-iphone6](https://github.com/ITSMEFRTRUST/linux-on-iphone6) diz ter
> touch, Wi-Fi, áudio e câmera no iPhone 6 com 138 patches. Usamos só as tensões do motor
> de vibração (patch 0002 deles) como referência. Não rode os scripts deles sem entender.

## Fase 5: Interface gráfica
- [ ] Desenhar direto no framebuffer (`/dev/fb0` ou DRM dumb buffer)
- [ ] Interface leve com LVGL ou Weston/Wayland com renderização por software (pixman)
- [ ] Com touch funcionando (fase 4), interface tocável

**Você aprende:** DRM/KMS, framebuffer, compositores, input.

## Fase 6: Android
O Android é um kernel Linux + um userspace próprio (init do Android, Binder, SurfaceFlinger, ART).
Requisitos e o plano para o iPhone 6:

| Requisito do Android | Situação no iPhone 6 | Plano |
|---|---|---|
| Kernel com Binder | ✅ já ativado em `kernel/iphone6.config` | — |
| Tela | ✅ simpledrm | `drm_hwcomposer` ou HWC de framebuffer |
| GPU (OpenGL ES/Vulkan) | ❌ PowerVR sem driver aberto | **SwiftShader** (GPU em software, já faz parte do AOSP) |
| Armazenamento (`/system`, `/data`) | ❌ | Imagens via rede USB (NBD) no início |
| Touch | ❌ | Fase 4. Até lá, controlar pelo PC com `adb` + `scrcpy` |
| RAM | ⚠️ 1 GB | Android Go / AOSP mínimo (Android 10–12 é realista) |
| Páginas 4K | ✅ (A8 usa 4K) | Bom: Android assume 4K por padrão |

Passos:
- [ ] Estudar o [Project Sandcastle](https://projectsandcastle.org/) (Android 10 no iPhone 7), o modelo mais próximo
- [ ] Compilar um AOSP genérico ARM64 (`aosp_arm64`) e entender as partições
- [ ] Criar um "device tree" do AOSP para o iPhone 6 (`device/apple/n61`)
- [ ] Boot do init do Android com `/system` via NBD, `adb` pela USB
- [ ] SurfaceFlinger + SwiftShader na tela → **primeira tela do Android**
- [ ] scrcpy no PC para usar o Android do iPhone

**Atenção:** compilar o AOSP exige ~400 GB de disco e 32 GB+ de RAM. Este PC tem 199 GB
livres e 9,6 GB de RAM, então essa fase vai precisar de mais disco/RAM ou de uma máquina na nuvem.

**Você aprende:** arquitetura do Android, HALs, Binder, init/SELinux, sistema de build do AOSP.
