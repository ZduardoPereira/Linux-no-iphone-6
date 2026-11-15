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
   (ver `host/99-linux-no-iphone6.rules`).

### Pendências da fase 1
- [ ] Descobrir por que o DMA do USB falha (pongoOS antigo deixa algo configurado?)
- [ ] Enviar o patch do `DTXFSTS` para o kernel oficial (linux-usb)
- [ ] 2º núcleo da CPU (conferir com `nproc`)

## Fase 2: Conhecer o hardware por dentro do Linux
- [ ] Explorar `/sys`, `/proc/device-tree`, `dmesg`: o que o kernel achou?
- [ ] Ler `t7000.dtsi` e `t7000-6.dtsi` no kernel e mapear cada nó para uma peça real
- [ ] Testar botões (`evtest`), brilho (`/sys/class/backlight`), cpufreq
- [ ] Ler o device tree original da Apple (o m1n1 tem acesso a ele) e comparar

**Você aprende:** device tree, sysfs, drivers de plataforma, como o kernel descobre o hardware.

## Fase 3: Userspace de verdade
- [ ] Trocar o shell da tela por algo útil (ex.: mostrar IP, bateria, uptime)
- [ ] SSH (dropbear) pela rede USB
- [ ] Roteamento: o iPhone acessar a internet através do PC (NAT)
- [ ] `apk add` funcionando dentro do iPhone
- [ ] Rootfs maior via rede (NBD/NFS), já que o armazenamento interno ainda não funciona

**Você aprende:** init, rede Linux, NAT, root filesystem por rede.

## Fase 4: Primeiro driver
Escolha **uma** peça e escreva/porte o driver. Ordem sugerida, do mais fácil ao mais difícil:
1. **Motor de vibração** (Taptic, via I2C): bom primeiro driver
2. **Bateria/carregador**: ler nível de bateria
3. **Touchscreen** (Apple Z2, SPI): o Asahi já tem `apple_z2` para Macs; adaptar ao A8
4. **Armazenamento interno**: acompanhar o trabalho em andamento da Hoolock

**Você aprende:** kernel modules, I2C/SPI, interrupções, engenharia reversa, enviar patches.

> Referência para estudo, **não verificada**: o projeto
> [ITSMEFRTRUST/linux-on-iphone6](https://github.com/ITSMEFRTRUST/linux-on-iphone6) diz ter
> touch, Wi-Fi, áudio e câmera no iPhone 6 com 138 patches. Ele se declara "AI-assisted" e
> não foi revisado pela comunidade. Leia os patches para aprender, mas não rode os scripts
> dele (com sudo) sem entender o que fazem.

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
