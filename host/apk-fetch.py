#!/usr/bin/env python3
"""
Instala pacotes do Alpine (ARM64) dentro de uma pasta de rootfs, rodando no PC x86 e sem root.

Lê o APKINDEX dos repositórios, resolve as dependências, pula o que a rootfs já tem
(lib/apk/db/installed) e extrai cada .apk (um tar.gz) direto na pasta.
Não roda scripts de instalação dos pacotes (não dá para executar ARM64 aqui).

Uso: apk-fetch.py <rootfs> <cache> <repo-url>... -- <pacote>...
"""
import gzip, io, os, sys, tarfile, urllib.request

def baixar(url, destino):
    if not os.path.exists(destino):
        tmp = destino + ".part"
        with urllib.request.urlopen(url, timeout=60) as r, open(tmp, "wb") as f:
            f.write(r.read())
        os.replace(tmp, destino)
    return destino

def ler_indice(dados):
    """APKINDEX: blocos separados por linha vazia; campos 'X:valor'."""
    pacotes = []
    for bloco in dados.decode().split("\n\n"):
        campos = {}
        for linha in bloco.splitlines():
            if len(linha) > 2 and linha[1] == ":":
                campos[linha[0]] = linha[2:]
        if "P" in campos:
            pacotes.append(campos)
    return pacotes

def nome_dep(d):
    for sep in ("<", ">", "=", "~"):
        d = d.split(sep)[0]
    return d

def main():
    args = sys.argv[1:]
    sep = args.index("--")
    raiz, cache, repos, pedidos = args[0], args[1], args[2:sep], args[sep + 1:]
    os.makedirs(cache, exist_ok=True)

    # índice: nome -> (repo, campos) ; provides (so:libz.so.1, cmd:..., etc.) -> nome
    por_nome, provides = {}, {}
    for i, repo in enumerate(repos):
        f = baixar(repo + "/APKINDEX.tar.gz", os.path.join(cache, "APKINDEX.%d.tar.gz" % i))
        with tarfile.open(f) as t:
            dados = t.extractfile("APKINDEX").read()
        for p in ler_indice(dados):
            por_nome.setdefault(p["P"], (repo, p))
            for prov in p.get("p", "").split():
                provides.setdefault(nome_dep(prov), p["P"])

    instalados = set()
    db = os.path.join(raiz, "lib/apk/db/installed")
    if os.path.exists(db):
        for p in ler_indice(open(db, "rb").read()):
            instalados.add(p["P"])
            for prov in p.get("p", "").split():
                instalados.add(nome_dep(prov))

    fila, escolhidos = list(pedidos), []
    while fila:
        d = nome_dep(fila.pop(0))
        if d.startswith("!") or d in instalados:
            continue
        nome = d if d in por_nome else provides.get(d)
        if not nome:
            sys.exit("apk-fetch: não achei '%s' nos repositórios" % d)
        if nome in instalados:
            continue
        instalados.add(nome)
        escolhidos.append(nome)
        fila += por_nome[nome][1].get("D", "").split()

    for nome in escolhidos:
        repo, p = por_nome[nome]
        arq = "%s-%s.apk" % (p["P"], p["V"])
        f = baixar(repo + "/" + arq, os.path.join(cache, arq))
        # um .apk são 3 gzip concatenados (assinatura, controle, dados). gzip.decompress
        # junta todos; ignore_zeros faz o tarfile seguir depois do fim de cada tar.
        dados = gzip.decompress(open(f, "rb").read())
        with tarfile.open(fileobj=io.BytesIO(dados), mode="r:", ignore_zeros=True) as t:
            for m in t:
                if m.name.startswith(".") or m.isdev():
                    continue
                t.extract(m, raiz, set_attrs=m.isfile() or m.isdir(), filter="tar")
        print("  +", arq)

if __name__ == "__main__":
    main()
