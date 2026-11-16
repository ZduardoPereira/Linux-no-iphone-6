#!/usr/bin/env python3
"""
apfs_livre.py — lê (só leitura) o mapa de espaço livre do APFS do iOS na flash do
iPhone, pelo SSH, e mostra onde há regiões livres grandes para o Linux.

    python3 host/apfs_livre.py

Segue a "Apple File System Reference":
  superbloco do container (bloco 0 da partição, "NXSB")
  -> área de checkpoints (o superbloco com o maior xid é o atual)
  -> mapa do checkpoint (onde estão os objetos efêmeros)
  -> space manager (blocos livres; endereços dos "chunk info blocks")
  -> chunk info (quantos blocos livres em cada chunk de 128 MB)
Cada objeto tem checksum Fletcher-64, conferido aqui.
"""
import os
import struct
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISPOSITIVO = "/dev/ans0p1"     # a partição APFS (blocos relativos a ela)
BLOCO = 4096


def ler(bloco, n=1):
    """lê n blocos da partição pelo SSH (leitura direta, sem o cache do iPhone)"""
    cmd = ("dd if=%s iflag=direct bs=%d skip=%d count=%d 2>/dev/null"
           % (DISPOSITIVO, BLOCO, bloco, n))
    r = subprocess.run([os.path.join(RAIZ, "scripts", "ssh.sh"), cmd],
                       capture_output=True, check=True)
    if len(r.stdout) != n * BLOCO:
        sys.exit("leitura curta no bloco %d (%d bytes)" % (bloco, len(r.stdout)))
    return r.stdout


def fletcher64(dados):
    """checksum dos objetos APFS: Fletcher sobre palavras de 32 bits, sem os 8 primeiros bytes"""
    s1 = s2 = 0
    for (w,) in struct.iter_unpack("<I", dados[8:]):
        s1 = (s1 + w) % 0xFFFFFFFF
        s2 = (s2 + s1) % 0xFFFFFFFF
    lo = 0xFFFFFFFF - ((s1 + s2) % 0xFFFFFFFF)
    hi = 0xFFFFFFFF - ((s1 + lo) % 0xFFFFFFFF)
    return (hi << 32) | lo


def objeto(dados, nome):
    ck, oid, xid, tipo, sub = struct.unpack_from("<QQQII", dados, 0)
    ok = ck == fletcher64(dados)
    return {"oid": oid, "xid": xid, "tipo": tipo & 0xFFFF, "ok": ok, "nome": nome}


def u32(d, o): return struct.unpack_from("<I", d, o)[0]
def u64(d, o): return struct.unpack_from("<Q", d, o)[0]


def main():
    # 1) superbloco do container no bloco 0
    d = ler(0)
    if d[0x20:0x24] != b"NXSB":
        sys.exit("bloco 0 não é um superbloco APFS (NXSB)")
    bs, total = u32(d, 0x24), u64(d, 0x28)
    desc_blocos, desc_base = u32(d, 0x68), u64(d, 0x70)
    print("container: %d blocos de %d bytes = %.1f GB" % (total, bs, total * bs / 1e9))
    if desc_blocos & 0x80000000:
        sys.exit("área de checkpoints não contígua: não suportado aqui")

    # 2) área de checkpoints: superbloco com maior xid
    area = ler(desc_base, desc_blocos)
    melhor = None
    for i in range(desc_blocos):
        b = area[i * bs:(i + 1) * bs]
        o = objeto(b, "desc")
        if o["tipo"] == 1 and b[0x20:0x24] == b"NXSB" and o["ok"]:
            if not melhor or o["xid"] > melhor[1]["xid"]:
                melhor = (i, o, b)
    if not melhor:
        sys.exit("nenhum superbloco válido na área de checkpoints")
    i, o, sb = melhor
    print("checkpoint atual: bloco %d da área, xid %d" % (desc_base + i, o["xid"]))
    sm_oid = u64(sb, 0x98)
    idx, ln = u32(sb, 0x88), u32(sb, 0x8c)

    # 3) mapas do checkpoint (os blocos antes do superbloco, no mesmo checkpoint)
    sm_end = None
    for k in range(ln - 1):
        b = area[((idx + k) % desc_blocos) * bs:][:bs]
        om = objeto(b, "cpm")
        if om["tipo"] != 0x0C:
            continue
        n = u32(b, 0x24)
        for m in range(n):
            ent = 0x28 + m * 40
            tipo, sub, tam, pad, fs, oid, paddr = struct.unpack_from("<IIIIQQQ", b, ent)
            if oid == sm_oid:
                sm_end, sm_tam = paddr, tam
    if sm_end is None:
        sys.exit("space manager não achado no mapa do checkpoint")

    # 4) space manager
    sm = ler(sm_end, max(1, sm_tam // bs))
    osm = objeto(sm[:sm_tam], "spaceman")
    bpc, cpc = u32(sm, 0x24), u32(sm, 0x28)
    blocos, chunks = u64(sm, 0x30), u64(sm, 0x38)
    cibs, cabs, livres = u32(sm, 0x40), u32(sm, 0x44), u64(sm, 0x48)
    addr_off = u32(sm, 0x50)
    print("space manager: checksum %s; %d blocos, %d livres (%.1f GB livres, %.1f GB usados)"
          % ("ok" if osm["ok"] else "ERRADO", blocos, livres, livres * bs / 1e9,
             (blocos - livres) * bs / 1e9))
    print("  %d chunks de %d blocos (%.0f MB), %d chunk-info-blocks, %d cabs"
          % (chunks, bpc, bpc * bs / 2**20, cibs, cabs))
    if cabs:
        sys.exit("container com CABs (muito grande): não suportado aqui")
    cib_end = [u64(sm, addr_off + 8 * k) for k in range(cibs)]

    # 5) chunk info: quais chunks estão totalmente livres
    livre = []
    for e in cib_end:
        b = ler(e)
        if not objeto(b, "cib")["ok"]:
            sys.exit("checksum errado no chunk info block %d" % e)
        for c in range(u32(b, 0x24)):
            xid, addr, bc, fc, bm = struct.unpack_from("<QQIIQ", b, 0x28 + c * 32)
            livre.append((addr, bc & 0xFFFFF, fc, bm))
    livre.sort()
    cheios = sum(1 for a, bc, fc, bm in livre if fc == bc)
    print("  chunks totalmente livres: %d de %d" % (cheios, len(livre)))

    # maiores sequências de chunks totalmente livres
    seqs, ini, fim = [], None, None
    for a, bc, fc, bm in livre:
        if fc == bc and (ini is None or a == fim):
            ini = a if ini is None else ini
            fim = a + bc
        else:
            if ini is not None:
                seqs.append((fim - ini, ini, fim))
            ini, fim = (a, a + bc) if fc == bc else (None, None)
    if ini is not None:
        seqs.append((fim - ini, ini, fim))
    seqs.sort(reverse=True)
    print("maiores regiões totalmente livres (blocos da partição):")
    for tam, a, b in seqs[:5]:
        print("  blocos %9d .. %9d  (%.1f GB)" % (a, b, tam * bs / 1e9))
    usados = [(a, bc - fc) for a, bc, fc, bm in livre if fc != bc]
    if usados:
        print("chunks com dados: do bloco %d ao %d" % (usados[0][0], usados[-1][0] + bpc))


if __name__ == "__main__":
    main()
