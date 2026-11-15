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

# ---------- protocolo do bootloader (HBPP) ----------
# Palavras de 16 bits big-endian. sw32(v) = bytes v[15:8] v[7:0] v[31:24] v[23:16].
import struct
ACK_DATA, ACK_ESCRITA = 0x4bc1, 0x4ad1

def sw32(v):
    return bytes([(v >> 8) & 255, v & 255, (v >> 24) & 255, (v >> 16) & 255])

def ck16(b):
    return struct.pack(">H", sum(b) & 0xffff)

def troca_pares(b):
    b = bytes(b) + bytes(-len(b) % 4)                  # completa até múltiplo de 4
    return bytes(x for i in range(0, len(b), 2) for x in (b[i + 1], b[i]))

def atn_ack(fd):
    rx = xfer(fd, [0x1a, 0xa1])
    return rx[0] << 8 | rx[1]

def pacote_data(endereco, dados):
    dados = bytes(dados) + bytes(-len(dados) % 4)
    cab = struct.pack(">H", len(dados) // 4) + sw32(endereco)
    return b"\x18\xe1\x30\x01" + cab + ck16(cab) + troca_pares(dados) + sw32(sum(dados))

def envia_data(fd, endereco, dados, nome):
    for tentativa in range(5):
        xfer(fd, pacote_data(endereco, dados))
        a = atn_ack(fd)
        print("  %-22s -> %#010x (%4d bytes): ack %04x %s" %
              (nome, endereco, len(dados), a, "✅" if a == ACK_DATA else "❌"))
        if a == ACK_DATA:
            return True
    return False

def mem_escreve(fd, endereco, valor, mascara=0xffffffff):
    corpo = sw32(endereco) + sw32(mascara) + sw32(valor)
    xfer(fd, b"\x1e\x33" + corpo + ck16(corpo))
    a = atn_ack(fd)
    print("  escreve %#010x = %#x: ack %04x %s" % (endereco, valor, a, "✅" if a == ACK_ESCRITA else "❌"))
    return a == ACK_ESCRITA

def mem_le(fd, endereco):
    corpo = sw32(endereco)
    xfer(fd, b"\x1c\x73" + corpo + ck16(corpo))
    rx = xfer(fd, bytes([0x1a, 0xa1, 0x18, 0xe1, 0x18, 0xe1, 0x18, 0xe1]))
    return (rx[2] << 8 | rx[3]) | (rx[4] << 8 | rx[5]) << 16

def calibracao_do_adt():
    """Calibração de fábrica deste aparelho: o iBoot a coloca no ADT (/dev/mtd1)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import adt
    raiz, _ = adt.ler_no(open("/dev/mtd1", "rb").read(), 0)
    no = raiz
    for nome in ("arm-io", "spi2", "multi-touch"):
        no = [f for f in no["filhos"] if f["props"]["name"].rstrip(b"\0") == nome.encode()][0]
    return no["props"]["multi-touch-calibration"], no["props"]["prox-calibration"]

def carrega_calibracao(fd):
    mtcl, pxcl = calibracao_do_adt()
    print("calibração do ADT: MtCl %d bytes, PxCl %d bytes" % (len(mtcl), len(pxcl)))
    ok = envia_data(fd, 0x10009600, pxcl, "PxCl (proximidade)")
    for off in range(0, len(mtcl), 0x3f0):              # em pedaços de até 0x3f0
        ok = ok and envia_data(fd, 0x10009000 + off, mtcl[off:off + 0x3f0], "MtCl (toque) +%#x" % off)
    return ok

def carrega_tudo(fd, firmware):
    if not carrega_calibracao(fd):
        sys.exit("a calibração não foi aceita")
    fw = open(firmware, "rb").read()
    for tentativa in range(5):                          # já vem como pacote DATA pronto
        xfer(fd, fw)
        a = atn_ack(fd)
        print("  firmware (%d bytes): ack %04x %s" % (len(fw), a, "✅" if a == ACK_DATA else "❌"))
        if a == ACK_DATA:
            break
    else:
        sys.exit("o firmware não foi aceito")
    versao = mem_le(fd, 0x10008ffc)
    print("  versão no chip: %#010x" % versao)
    ok = (mem_escreve(fd, 0x10003060, 0x17d3) and mem_escreve(fd, 0x1000305c, 0x2) and
          mem_escreve(fd, 0x10003058, 0x6) and
          mem_escreve(fd, 0x10003000, 2 if versao == 0x434d11a0 else 3) and
          mem_escreve(fd, 0x10003518, 1))
    if not ok:
        sys.exit("sequência de calibração falhou")
    mem_le(fd, 0x10003800)
    xfer(fd, [0x1f, 0x01]); time.sleep(0.065); atn_ack(fd)
    print("  calibrar (1f 01) enviado")
    corpo = sw32(0x10003400) + sw32(1)
    xfer(fd, b"\x1d\x53" + corpo + ck16(corpo))
    time.sleep(0.040)
    print("  EXECUTE @0x10003400 enviado — o firmware deve estar rodando; IRQ = %s" % irq())

# ---------- modo normal (depois do EXECUTE): ler os toques ----------
def le16(b):
    return b[0] | b[1] << 8

def s16(b):
    v = le16(b)
    return v - 0x10000 if v & 0x8000 else v

def le_frame(fd, ctr):
    """Comando 0xEB: pergunta o tamanho e depois lê o relatório. None = nada válido."""
    cmd = bytearray(16)
    cmd[0], cmd[1] = 0xEB, ctr
    cmd[14:16] = struct.pack("<H", sum(cmd[:14]) & 0xffff)
    rx = xfer(fd, cmd)
    if rx[0] & 0xf0 != 0xE0 or le16(rx[14:16]) != sum(rx[:14]) & 0xffff:
        return None
    n = le16(rx[1:3])
    if n == 0:
        return b""
    tx = bytearray(n + 5)
    tx[0], tx[1], tx[2] = 0xEB, ctr, 1
    tx[-2:] = struct.pack("<H", sum(tx[:-2]) & 0xffff)
    rx = xfer(fd, tx)
    if rx[0] not in (0xEB, 0xEA) or sum(rx[0:5]) & 0xff:
        return None
    L = le16(rx[2:4])
    carga = rx[5:5 + L - 2]
    if le16(rx[5 + L - 2:5 + L]) != sum(carga) & 0xffff:
        return None
    return carga

ESTAGIOS = {1: "perto", 2: "pairando", 3: "encostou", 4: "tocando", 5: "soltou", 6: "saindo", 7: "fora"}

def toques(fd, segundos=30):
    print("Toque na tela! (%d s; coordenadas em pixels 750x1334)" % segundos)
    ctr, fim, vistos = 1, time.time() + segundos, set()
    while time.time() < fim:
        if irq() != "0":                       # IRQ ativo em nível baixo
            time.sleep(0.005)
            continue
        p = le_frame(fd, ctr)
        if p is None:
            continue
        ctr = 3 - ctr                          # alterna 1 <-> 2 após frame bom
        if not p:
            continue
        if p[0] != 0x44:
            if p[0] not in vistos:
                print("  relatório %#04x (%d bytes): %s" % (p[0], len(p), p[:24].hex(" ")))
                vistos.add(p[0])
            continue
        n, tam, ini = p[16], p[17], p[2] + le16(p[14:16])
        for i in range(n):
            r = p[ini + i * tam: ini + (i + 1) * tam]
            x, y = s16(r[4:6]), s16(r[6:8])
            print("  dedo %d %-9s x=%4d y=%4d  (bruto %5d,%5d) pressão=%d" %
                  (r[0], ESTAGIOS.get(r[1], r[1]), x * 750 // 5850, (10405 - y) * 1334 // 10405,
                   x, y, le16(r[26:28])))

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
    elif cmd == "calibracao":                 # T4a: só a calibração (cabe em 4096 bytes)
        carrega_calibracao(os.open(spidev(), os.O_RDWR))
    elif cmd == "carrega":                    # T4b: calibração + firmware + EXECUTE
        carrega_tudo(os.open(spidev(), os.O_RDWR),
                     sys.argv[2] if len(sys.argv) > 2 else "/lib/firmware/apple/n61-touch.bin")
    elif cmd == "toques":                     # T4c: lê e mostra os toques
        toques(os.open(spidev(), os.O_RDWR), int(sys.argv[2]) if len(sys.argv) > 2 else 30)
    elif cmd == "tudo":                       # liga + carrega + toques
        liga()
        fd = os.open(spidev(), os.O_RDWR)
        carrega_tudo(fd, "/lib/firmware/apple/n61-touch.bin")
        time.sleep(0.1)
        toques(fd, int(sys.argv[2]) if len(sys.argv) > 2 else 30)
    elif cmd == "desliga":
        desliga()
    else:
        sys.exit(__doc__)
