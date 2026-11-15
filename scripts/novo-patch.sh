#!/usr/bin/env bash
# Gera um patch novo em kernel/patches/ com as mudanças ATUAIS do kernel nos arquivos
# listados, relativas a "kernel original + patches que já existem". Assim cada patch
# contém só o que é dele, mesmo quando vários mexem no mesmo arquivo.
#
#   ./scripts/novo-patch.sh 0005-nome "Título do patch" caminho/arq1.c caminho/arq2.dts ...
#
# Os caminhos são relativos à raiz do kernel ($KSRC). Arquivos novos também funcionam.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
[ $# -ge 3 ] || die "uso: $0 NNNN-nome \"título\" arquivo..."
NOME="$1"; TITULO="$2"; shift 2
SAIDA="$ROOT/kernel/patches/$NOME.patch"
[ -e "$SAIDA" ] && die "já existe: kernel/patches/$NOME.patch"

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a" "$T/b"

# 1) "antes" = arquivos originais (HEAD do kernel) + todos os patches existentes
ANTERIORES=("$ROOT"/kernel/patches/*.patch)
mapfile -t TOCADOS < <(grep -h '^+++ b/' "${ANTERIORES[@]}" | sed 's|^+++ b/||' | sort -u)
for f in "${TOCADOS[@]}" "$@"; do
  if git -C "$KSRC" cat-file -e "HEAD:$f" 2>/dev/null; then
    mkdir -p "$T/a/$(dirname "$f")"
    git -C "$KSRC" show "HEAD:$f" > "$T/a/$f"
  fi
done
for p in "${ANTERIORES[@]}"; do (cd "$T/a" && git apply "$p"); done

# 2) "depois" = como os arquivos estão agora no kernel (copiados para b/)
for f in "$@"; do
  mkdir -p "$T/b/$(dirname "$f")"
  cp "$KSRC/$f" "$T/b/$f"
done
# 3) diff a/ -> b/ ; com --no-prefix os caminhos já saem como "a/arq" e "b/arq"
{
  printf '%s\n\n' "$TITULO"
  for f in "$@"; do
    antes="a/$f"; [ -e "$T/a/$f" ] || antes=/dev/null       # arquivo novo
    (cd "$T" && git diff --no-index --no-prefix -- "$antes" "b/$f" || true)
  done
} > "$SAIDA"

grep -q '^@@' "$SAIDA" || { rm -f "$SAIDA"; die "nenhuma mudança encontrada nesses arquivos"; }
# confere: o patch tem que "desaplicar" limpo sobre o estado atual
git -C "$KSRC" apply --check -R "$SAIDA" || die "patch gerado não confere com o kernel atual"
echo "$NOME.patch" >> "$KSRC/.iphone6-patches"
log "Criado kernel/patches/$NOME.patch ($(grep -c '^diff ' "$SAIDA") arquivo(s))"
