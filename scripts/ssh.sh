#!/usr/bin/env bash
# Entra no iPhone por SSH (rede USB), com a chave do projeto em build/ssh/.
#   ./scripts/ssh.sh                 shell interativo
#   ./scripts/ssh.sh 'uname -a'      roda um comando
# Copiar arquivos:  scp -i build/ssh/id_ed25519 -o UserKnownHostsFile=build/ssh/known_hosts arquivo root@172.16.42.1:/tmp/
set -euo pipefail
source "$(dirname "$0")/../config.sh"
[ -f "$BUILD/ssh/id_ed25519" ] || die "falta a chave: rode ./scripts/04-initramfs.sh (ela cria build/ssh/)"
# A chave do servidor muda a cada boot (o iPhone não guarda nada), então não a fixamos.
exec ssh -i "$BUILD/ssh/id_ed25519" \
  -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=ERROR \
  root@"$PHONE_IP" "$@"
