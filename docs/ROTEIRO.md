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
5. Outro bug do dwc2 em PIO: um SETUP que chega com o ep0 fora do estado SETUP era
   copiado para o buffer do pedido anterior **sem limite de tamanho** (corrupção de
   memória → boot travando em "new address 1", e o "cdc_ncm bind() failure" no PC).
   Patch `kernel/patches/0014`: SETUP novo aborta a transferência em andamento (como
   manda o USB 2.0) e a cópia nunca passa do buffer.
6. Atrapalham no PC: driver `apple_mfi_fastcharge`, `usbmuxd` e ModemManager
   (ver `host/70-linux-no-iphone6.rules`).

### Pendências da fase 1
- [ ] Descobrir por que o DMA do USB falha (pongoOS antigo deixa algo configurado?)
- [ ] Enviar o patch do `DTXFSTS` para o kernel oficial (linux-usb)
- [x] **2º núcleo funcionando** (`nproc` = 2, 2 núcleos a 100% sem erros). A causa, achada
      com "pegadas" que a CPU 1 deixa na RAM (`m1n1/patches/0002`): a CPU 0 roda em EL1
      **seguro** (como o iBoot/pongoOS antigo deixam) e o m1n1 descia a CPU 1 de EL3 como
      **não-segura**; os caches tratam os dois modos como endereços diferentes, a CPU 1 leu
      `target_cpu` velho e marcou o flag na entrada errada da `spin_table`. Correção:
      `m1n1/patches/0003` (secundário mantém NS=0). Hipóteses descartadas no caminho:
      RVBAR travado, "recfg" restaurando o RVBAR (o trampolim do stub fica, inofensivo)

## Fase 2: Conhecer o hardware por dentro do Linux ✅
- [x] Explorar `/sys`, `/proc/device-tree`, `dmesg`: o que o kernel achou
- [x] Ler o log do m1n1 (`/dev/mtd0`) e o device tree original da Apple (`/dev/mtd1`)
- [x] Leitor do ADT (`host/adt.py`) e mapa completo em [HARDWARE.md](HARDWARE.md)
- [x] Brilho (`/sys/class/backlight`, 0–2047) e cpufreq (300 MHz–1,39 GHz, `schedutil`)
- [ ] Testar os botões lendo `/dev/input/event0`
- [ ] Ler `t7000.dtsi` no kernel e comparar nó a nó com o ADT (`build/adt.txt`)
- [x] Investigar o 2º núcleo ✅
- [ ] Investigar o DMA do USB

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

### 4.2 Sensores de temperatura — primeiro driver do zero (em andamento)
- [x] Mapa dos registradores pelo ADT (5 sensores em 2 blocos: PMGR e TCC)
- [x] Ferramenta `device/regs` (lê registradores físicos via `/dev/mem` + `mmap`)
- [x] Engenharia reversa: `readBK` bits 27..16 = temperatura (hipótese: décimos de °C),
      bit 31 = válido; bloco TCC só aceita acesso de 32 bits; experimento de aquecimento
- [x] Driver `drivers/hwmon/apple-t7000-temp.c` + device tree (`kernel/patches/0004`)
- [x] Boot: `sensors` mostra os sensores (5 no sysfs; nome único por bloco corrigido p/ o próximo boot)
- [ ] Confirmar a escala: iPhone frio (desligado ~15 min) deve ler perto da temperatura ambiente

### 4.3 Chips do i2c1 ✅
- [x] `0x58` = o mesmo DRV2604 respondendo num 2º endereço (registradores idênticos ao vivo)
- [x] `0x75` = carregador SN2400: eventos, status, limite de entrada e ADC entendidos ([SN2400.md](SN2400.md))
- [x] Achado: reconectar o cabo derruba o limite para 90 mA; `i2cset -y 1 0x75 0x09 0x24` volta a 450 mA
- [x] Driver `drivers/power/supply/sn2400_charger.c` (`kernel/patches/0005`): vigia a cada 2 s, reprograma o limite, fontes `sn2400-usb` e `sn2400-battery`
- [x] Boot: fontes em `/sys/class/power_supply/`; tirar e pôr o cabo → driver reprogramou 450 mA sozinho ✅

### 4.4 Percentual da bateria (TI bq27540 por HDQ) ✅
- [x] ADT: medidor em `uart5` (0x20a0d4000, IRQ 163), fio HDQ = pino 57 (TX); acesso pedido ao SN2400 (`0x1d`)
- [x] Protocolo (segundo ITSMEFRTRUST, a confirmar): 57600 8N2, `0xFE`=1 / `0xC0`=0, eco no RX, break = `0x00` a 9600
- [x] Device tree: UART5 ativa + pino 57 na função UART (`kernel/patches/0006`) → `/dev/ttySAC5`
- [x] Protótipo em Python (`device/hdq.py`): **95 %**, 4345 mV, 29 °C, 1732/1810 mAh, 2 ciclos ([BQ27540.md](BQ27540.md))
- [ ] Corrente da bateria: `0x14` fica em 997 fixo (firmware da Apple?)
- [x] Driver `drivers/power/supply/apple_hdq_battery.c` (`kernel/patches/0007`): UART5 em polling 8N2 + núcleo `bq27xxx`
- [x] Boot: `/sys/class/power_supply/bq27540-battery/` → **95 %**, 4,346 V, 29 °C, 1732/1810 mAh, 2 ciclos ✅
- [x] Unificar numa bateria só (`kernel/patches/0008`): `bq27xxx` sem corrente usa o carregador (`power-supplies`) para o status
- [ ] Boot: só `bq27540-battery`, status "Charging" com o cabo

### Depois
2. **Bateria/carregador**: ler nível de bateria
3. **Touchscreen** (Apple Z2, SPI): o mais importante para o Android
4. **Armazenamento interno**: acompanhar o trabalho em andamento da Hoolock

**Você aprende:** device tree, I2C, GPIO, force-feedback/input, syscalls, depois drivers em C.

> Referência para estudo, **não verificada**: o projeto
> [ITSMEFRTRUST/linux-on-iphone6](https://github.com/ITSMEFRTRUST/linux-on-iphone6) diz ter
> touch, Wi-Fi, áudio e câmera no iPhone 6 com 138 patches. Usamos só as tensões do motor
> de vibração (patch 0002 deles) como referência. Não rode os scripts deles sem entender.

## Fase 5: Touchscreen e interface gráfica ✅ (touch no kernel + tela inicial LVGL, 15/11/2026)
O chip de toque é o "N1" (família Apple Z2), no SPI2. Mapa do protocolo: re/MULTITOUCH.md de
github.com/ITSMEFRTRUST/linux-on-iphone6 (engenharia reversa do iOS); conferido aqui no aparelho.

- [x] T1: SPI2 — variante S5L (A7-A11) no `spi-apple`, em polling (`kernel/patches/0009`)
- [x] T2: ligar o chip (Chestnut LDO2, clock 32 kHz em `0x20e01e000`, reset no GPIO 2) e
      sondar o bootloader: `python3 device/n1.py liga` → **respondeu `1f01`/`4879`** ✅
- [x] T3: firmware extraído (`scripts/09-firmware-touch.sh`): 65320 bytes, versão 0x0412, checksums ✅
- [x] T4a: calibração do ADT (`/dev/mtd1`) aceita pelo chip: PxCl + MtCl, ack `4bc1` ✅
- [x] T4b: firmware (ack `4bc1`) + sequência de calibração (5× ack `4ad1`) + EXECUTE; versão no chip `0x434d15c1` ✅
- [x] T4c: **toques lidos!** relatório `0x44`, trajetória lisa do dedo em pixels 750×1334 ✅ (`python3 /root/n1.py tudo 30`)
- [x] Orientação conferida nos 4 cantos; multitoque (2 dedos) ✅; X um pouco negativo na borda esquerda
- [x] T5: driver de kernel `drivers/input/touchscreen/apple_n1.c` (`kernel/patches/0010`): carrega o
      firmware no boot em 0,25 s; `evtest` mostra multitoque padrão (slots, tracking IDs) ✅
- [x] T6: m1n1 copia `multi-touch-calibration` e `prox-calibration` do ADT para o Linux (`m1n1/patches/0004`)
- [x] Primeiro app: `device/desenho` (C sem libc, 6 KB): pinta onde o dedo passa, paleta de cores,
      Home limpa, Volume muda a espessura, Power sai (`/dev/fb0` + `/dev/input` + `ppoll`)
- [x] Interface de verdade: **LVGL** (escolhido em vez de Android/Waydroid: 1 GB de RAM, sem GPU) ✅
  - L1: compilador cruzado `host/iphone-cc` (clang + sysroot Alpine/musl, estático; `scripts/10-toolchain.sh`)
  - L2: LVGL v9.6 em `src/lvgl`, drivers prontos `lv_linux_fbdev` (tela) e `lv_evdev` (toque);
    fonte Montserrat completa (acentos) embutida via `tiny_ttf`; tema escuro
  - L3: `device/inicio`: hora, data, bateria, temperatura, memória, carregador, brilho, vibrar,
    abre o Desenho. Power apaga a tela, Volume muda o brilho. Parado: 0% de CPU, ~5 MB de RAM
  - L4: sobe sozinha no boot (`rootfs/init`); `touch /tmp/sem-inicio; killall inicio` volta ao texto ✅

## Fase 7: Armazenamento interno (flash de 128 GB) ← leitura, escrita, ext4 e durabilidade ✅
No A8 a flash é do **ANS**, um coprocessador ARM com firmware próprio (carregado pelo iBoot
na RAM). Não é NVMe: o driver NVMe da Apple é do A11 em diante. Protocolo descoberto pela
HoolockLinux (m1n1 `src/ans1.c`, `proxyclient/m1n1/fw/ans1.py`).

- [x] Prova de vida no m1n1 (`m1n1/patches/0005`): GPT do iOS lida da flash ✅
- [x] Mailbox AKF no Linux (`kernel/patches/0011`): máscara de IRQ achada no reg 0x04 ✅
- [x] RTKit v10 (`kernel/patches/0013`): endpoint de armazenamento aberto junto com o mapa ✅
- [x] Driver `apple-ans1` (`kernel/patches/0012`): `/dev/ans0`, 128 GB, só leitura ✅
- [x] Partições: `gpt` na linha de comando → `ans0p1` (container APFS de 125 GB) ✅
- [x] **Coerência de memória** resolvida ✅. O A8 tem um *memcache* no lado da memória que
      guarda cópias dos acessos **Normal** da CPU (com ou sem cache!) e não enxerga as
      escritas do ANS; a manutenção de cache do ARM não o alcança. Acessos **Device** passam
      por fora dele. O driver usa buffers Device para comandos e dados (varridos uma vez na
      carga) e copia para as páginas do Linux. Medido: checksum idêntico em todos os modos
      de leitura; 116 MB/s pelo cache do Linux, 92 MB/s diretos (1 MB), 14,5 MB/s (4 KB)
- Caminhos testados que NÃO resolveram (para não repetir): `dc civac` com
  `HID4.DisDcMVAOps` desligado (o m1n1/iOS desligam esse bit para limpar cache; ele vem
  ligado e anula a manutenção de cache do Linux, mas não é a causa aqui); tabela de
  política do memcache por "data set ID" (MCC 0x2000N_X000 = 0x111); `HID8` data set ID
  da CPU 15→0. Ferramentas de teste em `docs/ans1-testes/`
- [ ] USB por DMA: provavelmente o mesmo memcache (o dwc2 escreve buffers Normal)
- [x] O boot depende do iOS na flash: o iBoot carrega dele o kernel, TrustCache, firmware do
      SEP e DeviceTree (ADT `/chosen/memory-map`) antes do pongo. Então **não apagamos o iOS**
- [x] Espaço livre do APFS lido com `host/apfs_livre.py` (só leitura, checksums ok): iOS usa
      6,6 GB; maior região livre 107 GB (blocos 4.128.768–30.244.864 da partição)
- [x] **Escrita** ✅ só numa janela de 96 GB dentro do livre (blocos de disco 4.194.310–
      29.360.134); o driver recusa qualquer escrita fora dela. 64 MB a 92,7 MB/s, lidos de
      volta idênticos; persistem ao recarregar o driver
- [x] **Durabilidade** ✅ (depois de muita investigação):
  - o iBoot carrega o firmware do ANS (embutido nele, criptografado; chave pública) e o
    **desliga** antes do pongo; dar a partida na cópia já usada fazia o firmware se
    recuperar a cada sessão e, num desligamento sem aviso, **voltar no tempo**
  - `scripts/11-firmware-ans.sh` extrai o **firmware limpo** do iBoot do IPSW; o driver o
    recarrega antes de cada partida, preservando a tabela de parâmetros que o iBoot preenche
    (SOC_, ANCC = clock da flash, RHB0/RHS0…)
  - opcodes achados desmontando o iBoot e o firmware: **0x1b** abre a E/S (montagem do FTL),
    **0x19** libera a escrita (o firmware parte trancado), **0x14** é desligamento final (não usar)
  - corte simulado (CPU do ANS parada + domínio desligado) **nunca** perdeu nada, nem 1 GB
    sem flush; o corte **de verdade** (DFU) com o ANS acordado fazia o boot seguinte voltar
    para um estado antigo (o chip NAND tem controlador próprio e perde estado volátil)
  - solução, como no iOS: **cochilo** — 2 s sem pedidos (`cochilo_ms`), o driver põe o ANS
    para dormir de forma limpa e para a CPU dele; o próximo pedido religa (~0,95 s, com o
    firmware limpo). Flush = dormir
  - **confirmado com reboot de verdade (16/11/2026)**: ext4 montado, `sync`, DFU sem desligar
    nada → os 3 arquivos voltaram íntegros (o ext4 só repassa o journal)
  - cuidado: cortar a CPU do ANS durante a partida dele trava o chip NAND até um reboot
- [x] `flash-linux formatar` / `montar` / `desligar`: ext4 de 94 GB em `/mnt/flash`
- [x] `sync` rápido: 0,3–0,8 s num arquivo pequeno (era ~3 s). Medido: dormir depois de
      escrever custa ~1,3 s (salvar o contexto; o 0x14 custa o mesmo), religar 0,36 s, e o
      fsync do ext4 manda 2–3 flushes. O flush agora só antecipa o cochilo (0,1 s depois do
      último pedido): o ANS dorme uma vez e os dados ficam seguros ~1,5 s depois do sync
      (antes o sync terminava com o ANS acordado). `flush_espera=1` volta ao modo antigo
- [x] O `/init` monta a flash em `/mnt/flash` sozinho no boot, em segundo plano (não formata
      nada; `semflash` na linha de comando do kernel pula). Tela inicial: cartão "Flash
      interna" (espaço livre) e botão **Desligar** (salva a flash; "Voltar" remonta)
- [x] **Sistema na flash** (16/11/2026): o `/init` do initramfs monta a flash, instala o
      sistema em `/mnt/flash/sistema` no 1º boot (`flash-linux instalar`), recopia a cada
      boot a camada do projeto (módulos, firmwares, `/usr/local`, scripts:
      `flash-linux atualizar`) e faz `switch_root`. `apk add` e arquivos ficam salvos; a RAM
      do initramfs é liberada (24 MB em uso em vez de ~80). Sem flash, ou com `ram` na linha
      de comando do kernel, tudo continua na RAM. `desligar` trava a flash (somente leitura)
- [x] Perdas em série resolvidas: depois que o iOS ligou uma vez sem ninguém ver (cortado no
      meio), o iBoot passou a gravar muito na NVRAM a cada boot e atropelava os nossos dados.
      Ligar o iOS e desligar **pelo menu** resolveu. Regra: se o iOS ligar sem querer, ligá-lo
      e desligá-lo pelo menu antes de confiar na flash, e reconferir com `host/apfs_livre.py`.
      Saúde: no log do ANS, `Location [5,N] for element NVRAM` deve andar ~1 por boot
- [x] **Nunca** mandar o opcode 0x14 (desligamento NVMe) com uso depois: o boot seguinte
      recusou o log ("DM sequence in CLog didn't match") e a janela se perdeu

## Fase 8: Wi-Fi ← conectado à internet ✅ (16/11/2026)
O Wi-Fi é um **Broadcom BCM4345 B1** ligado por **PCIe** (`apcie,t7000`) atrás de uma
IOMMU da Apple (DART `dart-apcie1`). Base: os patches GPL-2.0 de
[ITSMEFRTRUST/linux-on-iphone6](https://github.com/ITSMEFRTRUST/linux-on-iphone6),
estudados e adaptados (`kernel/patches/0015`–`0023`, com autoria original).

- [x] GPIO do PMIC (`0015`): `WL_REG_ON` = GPIO 10 do PMU liga o chip (o GPIO 11 fica
      reservado: mexer nele travou o I2C do PMU no aparelho de referência)
- [x] Controlador PCIe do A8 (`0017`/`0018`), testado etapa por etapa (`t7000_stage` 1→6):
      PHY, porta 1, link treinado, chip `14e4:43ab` enumerado
- [x] DART do apcie (`0022`/`0023`: formato de descritor S5L8960X; janela de DMA
      0x80000000–0xbfffefff no device tree, `0025`)
- [x] `brcmfmac` com o BCM4345 B1 (`0020`/`0021`); a OTP do chip diz o módulo
      (`0024`): neste aparelho **USI** (`module=CORO vendor=u version=3.7`)
- [x] Firmware tirado do iOS 12.5.7 (`scripts/12-firmware-wifi.sh`: baixa só o sistema de
      arquivos do IPSW, lê o APFS com o 7-Zip, escolhe a NVRAM pelo `WIFI_NVRAM` do
      `config.sh` e põe o MAC do ADT)
- [x] **A descoberta que destravou tudo: o Linux rodava em modo *Secure*.** O pongoOS
      antigo (`bootr`) entrega a CPU em EL1 *Secure*; o iOS e os dispositivos usam
      *Non-secure*, e os caches tratam os dois modos como endereços diferentes. A DART
      lia tabelas sem as entradas recém-gravadas ("NO PTE"). Correção: o stub faz
      `smc #0` (o monitor de EL3 do próprio pongoOS devolve a CPU em EL3, como no
      `bootm` do pongoOS novo) e o m1n1 desce para EL1 *Non-secure*; o 2º núcleo segue a
      CPU de boot (`m1n1/patches/0007`). `STUB_SEM_EL3=1` no stub volta ao modo antigo
- [x] `iw` + `wpa_supplicant` + `wireless-regdb`; comando `wifi` (ligar, redes,
      conectar com senha digitada sem aparecer, estado, desligar)
- [x] Conectar a uma rede (`wifi conectar "Nome"`): internet e DNS pela `wlan0` (sinal
      −37 dBm, 65 Mbit/s); o `udhcpc` fica em segundo plano renovando o endereço
- [ ] Ligar o Wi-Fi sozinho no boot e mostrar na tela inicial
- [ ] Rever, agora em *Non-secure*: o DMA do USB (sem `apple_pio`) e os contornos do
      memcache no driver da flash (buffers Device)
- [ ] Tirar o `semflash` do `config.sh` (os testes do PCIe rodaram com a flash desligada)

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
