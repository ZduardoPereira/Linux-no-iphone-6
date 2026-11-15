#!/usr/bin/env python3
"""
Protótipo do touchscreen do iPhone 6 (chip "N1", família Apple Z2) — fase 5.
Roda NO iPhone (apk add python3). Passo T2: ligar o chip e conversar com o bootloader.

  python3 n1.py liga      liga alimentação + clock, solta o reset e sonda o bootloader
  python3 n1.py sonda     só a sondagem (chip já ligado)
  python3 n1.py desliga   reset + clock desligado + LDO2 desligado

Sequência e protocolo: re/MULTITOUCH.md de github.com/ITSMEFRTRUST/linux-on-iphone6
(engenharia reversa do iOS 12.5.5); aqui é a nossa conferência no aparelho.
Usa: i2cget/i2cset (i2c-tools), regs (nosso, /dev/mem), gpioset (libgpiod), /dev/spidev.
"""
import ctypes, fcntl, glob, os, subprocess, sys, time

CHESTNUT = ("0", "0x27")          # PMU da tela no i2c0; reg 0x05 bit 0x10 = LDO2 (touch analógico)
TOUCH_CTL = 0x20e01e000           # PMGR: clock de 32,768 kHz do touch
PINO_RESET = 2                    # ativo em nível baixo
PINO_IRQ = 3
RESPOSTAS = {0x18e1: "18e1", 0x1aa1: "1aa1 (ATN_ACK)", 0x1f01: "1f01", 0x4879: "4879",
             0x4969: "4969", 0x4ad1: "4ad1 (escrita OK)", 0x4bc1: "4bc1 (DATA OK)"}

def sh(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip()

def reg_le(end):
    return int(sh("regs", hex(end)).split()[1], 16)

def reg_escreve(end, v):
    sh("regs", "-w", hex(end), hex(v))

# ---------- SPI pelo /dev/spidev ----------
class XferSPI(ctypes.Structure):          # struct spi_ioc_transfer (32 bytes)
    _fields_ = [("tx_buf", ctypes.c_uint64), ("rx_buf", ctypes.c_uint64),
                ("len", ctypes.c_uint32), ("speed_hz", ctypes.c_uint32),
                ("delay_usecs", ctypes.c_uint16), ("bits_per_word", ctypes.c_uint8),
                ("cs_change", ctypes.c_uint8), ("tx_nbits", ctypes.c_uint8),
                ("rx_nbits", ctypes.c_uint8), ("word_delay_usecs", ctypes.c_uint8),
                ("pad", ctypes.c_uint8)]
SPI_IOC_MESSAGE_1 = 0x40206b00            # _IOW('k', 0, char[32])

def spidev():
    devs = glob.glob("/dev/spidev*")
    if devs:
        return devs[0]
    # o touch está no device tree sem driver: liga o spidev a ele
    for d in glob.glob("/sys/bus/spi/devices/spi*"):
        if "n61-touchscreen" in open(d + "/modalias").read():
            open(d + "/driver_override", "w").write("spidev")
            open("/sys/bus/spi/drivers/spidev/bind", "w").write(os.path.basename(d))
            time.sleep(0.2)
            return glob.glob("/dev/spidev*")[0]
    sys.exit("não achei o dispositivo SPI do touch (spi2) — kernel com o patch 0009?")

def xfer(fd, dados, hz=4000000):
    tx = ctypes.create_string_buffer(bytes(dados), len(dados))
    rx = ctypes.create_string_buffer(len(dados))
    t = XferSPI(tx_buf=ctypes.addressof(tx), rx_buf=ctypes.addressof(rx),
                len=len(dados), speed_hz=hz, bits_per_word=8)
    fcntl.ioctl(fd, SPI_IOC_MESSAGE_1, t)
    return rx.raw

SONDA = bytes([0x1a, 0xa1] + [0x18, 0xe1] * 7)

def sonda(fd):
    rx = xfer(fd, SONDA)
    a, b = rx[0] << 8 | rx[1], rx[2] << 8 | rx[3]
    print("  sondagem: rx = %s" % rx.hex(" "))
    ok = a in RESPOSTAS and b in RESPOSTAS
    print("  -> %s / %s  %s" % (RESPOSTAS.get(a, "%04x ?" % a), RESPOSTAS.get(b, "%04x ?" % b),
                               "✅ o bootloader do N1 respondeu" if ok else "❌ resposta inesperada"))
    return ok

def irq():
    return sh("gpioget", "-c", "gpiochip0", "--numeric", str(PINO_IRQ))

def liga():
    fd = os.open(spidev(), os.O_RDWR)
    v = int(sh("i2cget", "-f", "-y", *CHESTNUT, "0x05"), 16)
    print("1. Chestnut 0x05 = %#04x -> %#04x (LDO2 do touch)" % (v, v | 0x17))
    sh("i2cset", "-f", "-y", *CHESTNUT, "0x05", hex(v | 0x17))
    ldo = sh("i2ctransfer", "-f", "-y", "0", "w2@0x74", "0x00", "0x85", "r1@0x74")
    print("2. PMIC 0x0085 = %s (bit 0x02 = LDO15; já vem ligado)" % ldo)
    time.sleep(0.005)
    c = reg_le(TOUCH_CTL)
    reg_escreve(TOUCH_CTL, c & 0x7fffffff)
    time.sleep(0.00001)
    reg_escreve(TOUCH_CTL, (c & 0x7ffffc00) | 0x802dc)
    for _ in range(100):
        if not reg_le(TOUCH_CTL) & (1 << 18):
            break
    print("3. clock 32 kHz: TOUCH_CTL %#010x -> %#010x" % (c, reg_le(TOUCH_CTL)))
    time.sleep(0.015)
    xfer(fd, [0x19, 0xc1]); time.sleep(0.010)
    print("4. REQ_WAKEUP (19 c1) enviado")
    xfer(fd, SONDA)                                     # "garantir S_CLK alto"
    sh("gpioset", "-c", "gpiochip0", "-z", "%d=1" % PINO_RESET)   # solta o reset
    time.sleep(0.030)
    print("5. reset solto (GPIO %d = 1); IRQ (GPIO %d) = %s" % (PINO_RESET, PINO_IRQ, irq()))
    print("6. sondagem do bootloader:")
    sonda(fd)

def desliga():
    subprocess.run(["pkill", "-f", "gpioset"])           # solta a linha; o pino volta
    sh("gpioset", "-c", "gpiochip0", "-z", "%d=0" % PINO_RESET)
    c = reg_le(TOUCH_CTL)
    reg_escreve(TOUCH_CTL, c & ~(1 << 19))
    reg_escreve(TOUCH_CTL, (c & ~(1 << 19)) | (1 << 31))
    v = int(sh("i2cget", "-f", "-y", *CHESTNUT, "0x05"), 16)
    sh("i2cset", "-f", "-y", *CHESTNUT, "0x05", hex(v & ~0x10))
    print("desligado (reset, clock, LDO2)")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "liga"
    if cmd == "liga":
        liga()
    elif cmd == "sonda":
        sonda(os.open(spidev(), os.O_RDWR))
    elif cmd == "desliga":
        desliga()
    else:
        sys.exit(__doc__)
