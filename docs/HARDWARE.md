# Mapa do hardware do iPhone 6 (N61 / A8 T7000)

Levantado em 15/11/2026 com o Linux rodando no aparelho (fase 2), a partir de:
- o **ADT** (Apple Device Tree) — `./scripts/iphone.sh 'base64 /dev/mtd1' 120 | base64 -d > build/adt.bin`
  e `python3 host/adt.py build/adt.bin` (árvore completa em `build/adt.txt`);
- o log do m1n1 — `./scripts/iphone.sh 'tr -d "\0" < /dev/mtd0'`;
- `/sys/bus/platform/devices`, `/sys/class/*` e `dmesg` no iPhone.

Legenda: ✅ funciona · ⚠️ parcial · ❌ sem driver/desligado · 🔒 bloqueado pelo nosso caminho de boot

## SoC (blocos dentro do chip A8)

| Bloco (ADT) | O que é | Linux hoje |
|---|---|---|
| CPU 0 (Typhoon) | núcleo ARMv8 | ✅ |
| CPU 1 | 2º núcleo | 🔒 m1n1: "Starting CPU 1… Failed!" (IORVBAR travado pelo iBoot no boot via checkra1n) |
| `aic` | controlador de interrupções | ✅ |
| `pmgr` | gerência de energia/clocks (95 domínios) | ✅ `apple-pmgr-pwrstate` |
| `clpc` / performance-controller | frequência da CPU | ✅ cpufreq existe (nó `202220000` sem driver próprio) |
| `wdt` | watchdog | ✅ |
| `gpio` | pinos | ✅ `apple-gpio-pinctrl` |
| `uart0` | serial de debug | ✅ `ttySAC0` (precisa de cabo especial) |
| `i2c0` | barramento I2C 0 | ✅ |
| `i2c1`, `i2c2` | barramentos I2C 1 e 2 | ❌ não ativados no device tree |
| `spi1..3` | barramentos SPI | ❌ |
| `dwi` | brilho da tela | ✅ `apple-dwi-bl` (0–2047) |
| `usb-complex` + `otgphyctrl` | USB | ⚠️ só em PIO + patch 0002 (DMA falha neste caminho de boot) |
| `disp0` + `mipi-dsim` | tela | ⚠️ framebuffer simples (sem trocar resolução/vsync) |
| `mcc` | cache de memória | ❌ m1n1: "Unsupported version: mcc,t7000" |
| `ans` (iop) | controlador do armazenamento (NAND) | ❌ (driver em desenvolvimento na Hoolock) |
| `sep` (iop) | Secure Enclave | ❌ (não é necessário) |
| `sio` (iop) | coprocessador de I/O (áudio/SPI) | ❌ |
| `sgx` | GPU PowerVR GX6450 | ❌ sem driver aberto |
| `isp`, `jpeg`, `ave`, `vxd`, `scaler` | câmera, JPEG, vídeo, escalador | ❌ |
| `mca0..4` + `i2s-switch` | áudio digital | ❌ |
| `apcie` | PCIe (onde fica o Wi-Fi) | ❌ m1n1: "pcie: Unsupported compatible" |
| `tempsensor0..4`, `dietempctl0`, `amc`, `sochot0/1` | sensores de temperatura | ❌ — **bom primeiro driver em C** |
| `dart-*` | IOMMUs (uma por bloco) | ❌ |

## Chips externos (ligados nos barramentos)

| Barramento | Chip (ADT) | Função | Driver no Linux oficial? |
|---|---|---|---|
| i2c0 | `pmu,d2186` (Dialog) | PMIC: energia, RTC, botões | ✅ RTC/nvmem funcionando |
| i2c0 | `backlight,lm3534` | driver do LED da tela | (brilho já vem pelo `dwi`) |
| i2c0 | `tristar,cbtl1610` | chave do conector Lightning | ❌ |
| i2c0 | `display-pmu,chestnut` | energia da tela | ❌ |
| i2c1 | `vibrator,drv2604` (TI) | motor de vibração | **sim: `drv260x`** → só falta ativar i2c1 e descrever no DT |
| i2c1 | `charger,sn2400` (TI) | carregador da bateria | ❌ |
| i2c1 | `audio-control,cs35l19` (Cirrus) | amplificador do alto-falante | ~ (`cs35l33` é parecido) |
| i2c2 | `als,ct819` | sensor de luz ambiente | ❌ |
| spi1 | `audio-control,cs42l67` (Cirrus) | codec de áudio (fones/mics) | ❌ |
| spi2 | `multi-touch,n61` | **touchscreen** (Apple Z2) | ~ `apple_z2` (Macs) precisa de adaptação + firmware |
| spi3 | `biosensor,mesa` | Touch ID | ❌ (não faz sentido) |
| uart1 | `bluetooth,n88` (BCM4345) | Bluetooth | ✅ `hci_bcm` existe, depende de energia/GPIO + firmware |
| uart4 + apcie | `wlan-pcie-uart,bcm5345` (BCM4345) | Wi-Fi | ✅ `brcmfmac` existe, depende do PCIe |
| uart5 | `gas-gauge,bq27540` (TI) | medidor de bateria (HDQ) | ~ `bq27xxx` existe, precisa do transporte HDQ |
| uart8 | `apple-oscar` | coprocessador de movimento (M8) | ❌ |
| mipi-dsim | `lcd,pinot` | painel da tela | ❌ |

## Candidatos ao primeiro driver (fase 4)

1. **Vibração (DRV2604 em i2c1)** — o driver já existe no Linux (`drv260x`). O trabalho é
   de *device tree*: ativar `i2c1`, descrever o chip (endereço do ADT) e o GPIO de enable.
   Ótimo para aprender DT sem escrever C. Teste: `fftest`/`evtest` faz o iPhone vibrar.
2. **Sensores de temperatura (`tempsensor0..4`)** — blocos simples mapeados em memória.
   Um driver `hwmon` em C de ~150 linhas. Ótimo primeiro driver em C.
3. **Bateria (bq27540 por HDQ)** — mais difícil: precisa de um transporte novo.
4. **Touchscreen** — o mais importante para o Android, e o mais difícil.
