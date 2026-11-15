#!/usr/bin/env python3
"""
Leitor do ADT (Apple Device Tree) — o "mapa do hardware" que o iBoot entrega ao iOS.

Formato (tudo little-endian, alinhado a 4 bytes):
  nó          = u32 n_propriedades, u32 n_filhos, propriedades..., filhos...
  propriedade = char nome[32], u32 tamanho (bit 31 = "placeholder"), dados (padding até 4)

Uso:
  python3 host/adt.py build/adt.bin                 # árvore com compatible/reg/interrupções
  python3 host/adt.py build/adt.bin -a              # todas as propriedades
  python3 host/adt.py build/adt.bin -n arm-io/i2c0  # só um nó (e filhos), com tudo
Para pegar o ADT do iPhone (rodando Linux):
  ./scripts/iphone.sh 'base64 /dev/mtd1' 120 | base64 -d > build/adt.bin
"""
import struct, sys, string

def ler_no(buf, pos):
    nprops, nfilhos = struct.unpack_from("<II", buf, pos)
    pos += 8
    props = {}
    for _ in range(nprops):
        nome = buf[pos:pos + 32].split(b"\0")[0].decode(errors="replace")
        tam = struct.unpack_from("<I", buf, pos + 32)[0] & 0x7fffffff
        pos += 36
        props[nome] = buf[pos:pos + tam]
        pos += (tam + 3) & ~3
    filhos = []
    for _ in range(nfilhos):
        filho, pos = ler_no(buf, pos)
        filhos.append(filho)
    return {"props": props, "filhos": filhos}, pos

def texto(v):
    """Mostra strings como texto e o resto como u32/hex."""
    imprimivel = set(string.printable.encode()) - set(b"\x0b\x0c")
    partes = v.rstrip(b"\0").split(b"\0")
    # Um u32 pequeno (ex.: 0x24) tem bytes "$\0\0\0" e parece string de 1 letra:
    # só tratamos como texto se tiver pelo menos 2 caracteres.
    if v and v.endswith(b"\0") and len(v.rstrip(b"\0")) >= 2 and \
            all(p and all(c in imprimivel for c in p) for p in partes):
        return ", ".join(p.decode() for p in partes)
    if len(v) % 4 == 0 and 0 < len(v) <= 64:
        return " ".join("%#x" % x for x in struct.unpack("<%dI" % (len(v) // 4), v))
    return v[:32].hex() + ("…(%d bytes)" % len(v) if len(v) > 32 else "")

PRINCIPAIS = ("compatible", "reg", "interrupts", "device_type", "AAPL,phandle")

def mostrar(no, caminho="", nivel=0, tudo=False, alvo=None):
    nome = no["props"].get("name", b"?").rstrip(b"\0").decode()
    atual = (caminho + "/" + nome) if nivel else ""
    dentro = alvo is None or atual.strip("/").startswith(alvo.strip("/"))
    if dentro:
        print("  " * nivel + (nome if nivel else "/"))
        for k, v in no["props"].items():
            if k == "name":
                continue
            if tudo or alvo or k in PRINCIPAIS:
                print("  " * nivel + "    %-24s %s" % (k, texto(v)))
    for f in no["filhos"]:
        mostrar(f, atual, nivel + 1, tudo, alvo)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    buf = open(sys.argv[1], "rb").read()
    raiz, _ = ler_no(buf, 0)
    alvo = sys.argv[sys.argv.index("-n") + 1] if "-n" in sys.argv else None
    mostrar(raiz, tudo="-a" in sys.argv, alvo=alvo)
