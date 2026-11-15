#!/usr/bin/env python3
"""
Protótipo (fase 4.4): lê o medidor de bateria TI bq27540 do iPhone 6 por HDQ,
usando a UART5 (/dev/ttySAC5). Roda NO iPhone (precisa de: apk add python3).

HDQ = protocolo de 1 fio da TI. Cada bit é um pulso baixo: curto = 1, longo = 0.
Truque (nota SLUA408 da TI): uma UART a 57600 baud, 8N2, imita isso — cada quadro
UART (start + 8 bits + 2 stop = 191 us) vira um bit HDQ:
    0xFE -> start + 1 bit em zero  -> pulso de ~35 us  -> "1"
    0xC0 -> start + 6 bits em zero -> pulso de ~120 us -> "0"
O "break" (início de comando) é um 0x00 a 9600 baud (~940 us em zero).
O fio é compartilhado: o RX recebe o eco do que mandamos e, depois, os bits do medidor.

Antes de usar o fio, o carregador SN2400 precisa "entregá-lo": escrever 0x04 no
registrador 0x1d (i2c1, 0x75) e esperar o bit 5; no fim, escrever 0 para devolver.
(Protocolo segundo github.com/ITSMEFRTRUST/linux-on-iphone6 — aqui é a nossa conferência.)

Uso:  python3 hdq.py            mostra os valores principais
      python3 hdq.py 0x2c       lê um registrador (16 bits: 0x2c e 0x2d)
"""
import os, sys, termios, time, select, subprocess

PORTA = "/dev/ttySAC5"
UM, ZERO = 0xFE, 0xC0

def i2c(*args):
    # -f: o endereço 0x75 pertence ao nosso driver sn2400-charger
    return subprocess.run(["i2c" + args[0], "-f", "-y", "1", "0x75"] + list(args[1:]),
                          capture_output=True, text=True, check=True).stdout.strip()

def pede_fio():
    i2c("set", "0x1d", "0x04")
    for _ in range(100):                       # até 1 s
        if int(i2c("get", "0x1d"), 16) & 0x20:
            return
        time.sleep(0.01)
    raise TimeoutError("o carregador não entregou o fio HDQ (0x1d bit 5)")

def devolve_fio():
    i2c("set", "0x1d", "0x00")

class HDQ:
    def __init__(self):
        self.fd = os.open(PORTA, os.O_RDWR | os.O_NOCTTY)
        self.velocidade(termios.B57600)

    def velocidade(self, baud):
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0                              # modo cru
        a[2] = termios.CS8 | termios.CSTOPB | termios.CREAD | termios.CLOCAL  # 8N2
        a[4] = a[5] = baud
        a[6][termios.VMIN], a[6][termios.VTIME] = 0, 0
        termios.tcsetattr(self.fd, termios.TCSADRAIN, a)

    def ler(self, n, limite=0.05):
        buf, fim = b"", time.time() + limite
        while len(buf) < n and time.time() < fim:
            if select.select([self.fd], [], [], 0.005)[0]:
                buf += os.read(self.fd, n - len(buf))
        return buf

    def envia_bits(self, byte):
        quadros = bytes(UM if (byte >> i) & 1 else ZERO for i in range(8))   # LSB primeiro
        os.write(self.fd, quadros)
        eco = self.ler(8)
        return eco

    def le_registrador(self, reg):
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.velocidade(termios.B9600)                       # break
        os.write(self.fd, b"\x00"); self.ler(1)
        self.velocidade(termios.B57600)
        self.envia_bits(reg & 0x7f)                          # bit 7 = 0: leitura
        resp = self.ler(8, 0.02)                             # o medidor manda 8 bits
        if len(resp) != 8:
            raise IOError("registrador 0x%02x: o medidor não respondeu (%d bits, %s)"
                          % (reg, len(resp), resp.hex()))
        # pulso curto (poucos bits em zero no quadro) = 1; pulso longo = 0
        return sum((1 << i) for i, b in enumerate(resp) if b >= 0xF8), resp

    def le16(self, reg):
        lo, _ = self.le_registrador(reg)
        hi, _ = self.le_registrador(reg + 1)
        return lo | (hi << 8)

def s16(v):
    return v - 0x10000 if v & 0x8000 else v

def main():
    h = HDQ()
    pede_fio()
    try:
        if len(sys.argv) > 1 and sys.argv[1] == "dump":       # todos os registradores 0x00..0x3f
            for base in range(0, 0x40, 0x10):
                vals = []
                for r in range(base, base + 0x10):
                    try:
                        vals.append("%02x" % h.le_registrador(r)[0])
                    except IOError:
                        vals.append("--")
                print("%02x: %s" % (base, " ".join(vals)))
            return
        if len(sys.argv) > 1:
            reg = int(sys.argv[1], 0)
            v, bruto = h.le_registrador(reg)
            v2, _ = h.le_registrador(reg + 1)
            print("0x%02x = 0x%02x  0x%02x = 0x%02x  (16 bits: %d)  bruto: %s"
                  % (reg, v, reg + 1, v2, v | v2 << 8, bruto.hex()))
            return
        print("Bateria (bq27540 por HDQ):")
        print("  carga (StateOfCharge)  %d %%" % h.le16(0x2c))
        print("  tensão                 %d mV" % h.le16(0x08))
        print("  corrente média         %d mA" % s16(h.le16(0x14)))
        print("  temperatura            %.1f °C" % (h.le16(0x06) / 10 - 273.15))
        print("  capacidade restante    %d mAh" % h.le16(0x10))
        print("  capacidade total       %d mAh" % h.le16(0x12))
        print("  ciclos                 %d" % h.le16(0x2a))
    finally:
        devolve_fio()

if __name__ == "__main__":
    main()
