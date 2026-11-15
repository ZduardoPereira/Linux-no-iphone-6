#!/usr/bin/env python3
"""
Descomprime um arquivo da Apple comprimido por "decmpfs" tipo 4 (zlib no resource fork),
como os que o 7-Zip extrai de uma imagem do iOS:
    arquivo                          -> 0 bytes
    arquivo:com.apple.decmpfs        -> cabeçalho de 16 bytes
    arquivo:com.apple.ResourceFork   -> os dados comprimidos (recurso 'cmpf')

Formato:
  decmpfs  : 'fpmc' | tipo (u32 LE) | tamanho original (u64 LE)
  fork     : offset dos dados (u32 BE) | offset do mapa | tam. dados | tam. mapa
  recurso  : tamanho (u32 BE) e, dentro dele: n blocos (u32 LE),
             n x (offset u32 LE, tamanho u32 LE) relativos ao início da tabela;
             cada bloco é zlib, ou cru se o 1º byte for 0xFF. Blocos de 64 KiB.

Uso: decmpfs.py ARQUIVO SAIDA
"""
import struct, sys, zlib

def descomprime(base):
    cab = open(base + ":com.apple.decmpfs", "rb").read()
    magia, tipo, tamanho = struct.unpack("<4sIQ", cab[:16])
    if magia != b"fpmc":
        sys.exit("cabeçalho decmpfs inválido: %r" % magia)
    if tipo != 4:
        sys.exit("só sei o tipo 4 (zlib no resource fork); este é %d" % tipo)
    fork = open(base + ":com.apple.ResourceFork", "rb").read()
    dados_off = struct.unpack(">I", fork[0:4])[0]
    rec_tam = struct.unpack(">I", fork[dados_off:dados_off + 4])[0]
    rec = fork[dados_off + 4:dados_off + 4 + rec_tam]
    n = struct.unpack("<I", rec[:4])[0]
    saida = bytearray()
    for i in range(n):
        off, tam = struct.unpack("<II", rec[4 + 8 * i:12 + 8 * i])
        bloco = rec[off:off + tam]
        saida += bloco[1:] if bloco[:1] == b"\xff" else zlib.decompress(bloco)
    if len(saida) != tamanho:
        sys.exit("tamanho errado: %d (esperado %d)" % (len(saida), tamanho))
    return bytes(saida)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = descomprime(sys.argv[1])
    open(sys.argv[2], "wb").write(d)
    print("%s: %d bytes" % (sys.argv[2], len(d)))
