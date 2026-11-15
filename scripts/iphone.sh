#!/usr/bin/env bash
# Roda um comando no iPhone pela serial USB e mostra a saída.
#   ./scripts/iphone.sh 'uname -a'
#   ./scripts/iphone.sh 'cat /proc/cpuinfo' 10      (segundo argumento: tempo limite em s)
# Sem sudo precisa da regra host/70-linux-no-iphone6.rules instalada.
# Não use junto com o picocom aberto (os dois disputam a porta).
set -euo pipefail
PORTA="${PORTA:-$(ls /dev/ttyACM* 2>/dev/null | head -n1)}"
[ -n "$PORTA" ] || { echo "iPhone não encontrado (/dev/ttyACM*). Ele está rodando Linux?" >&2; exit 1; }
[ $# -ge 1 ] || { echo "uso: $0 'comando' [timeout]" >&2; exit 1; }

exec python3 -I - "$PORTA" "$1" "${2:-15}" <<'EOF'
import os, sys, termios, time, select, re
porta, cmd, timeout = sys.argv[1], sys.argv[2], float(sys.argv[3])
fd = os.open(porta, os.O_RDWR | os.O_NOCTTY)
attr = termios.tcgetattr(fd)
attr[0] = attr[1] = attr[3] = 0                    # modo "cru" no lado do PC
attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
termios.tcsetattr(fd, termios.TCSANOW, attr)

DSR = b"\x1b[6n"   # o ash do busybox pergunta "onde está o cursor?" antes de cada prompt

def ler(fim_marca, limite):
    buf, fim = b"", time.time() + limite
    while time.time() < fim:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        pedaco = os.read(fd, 4096)
        if DSR in pedaco:
            os.write(fd, b"\x1b[1;1R")              # responde como um terminal faria
            pedaco = pedaco.replace(DSR, b"")
        buf += pedaco
        if fim_marca and re.search(fim_marca, buf):
            break
    return buf

ler(None, 0.3)                                     # descarta sobras de comandos anteriores
# As marcas são escritas com "" no meio: no eco do comando aparecem quebradas,
# e só na SAÍDA de verdade aparecem inteiras.
os.write(fd, ('echo __I""NI__; %s; echo __F""IM__$?\r' % cmd).encode())
saida = ler(rb"__FIM__\d+", timeout).decode(errors="replace").replace("\r", "")
os.close(fd)

m = re.search(r"__INI__\n(.*?)__FIM__(\d+)", saida, re.S)
if not m:
    sys.stdout.write(saida)
    sys.exit("\n[tempo esgotado depois de %gs]" % timeout)
sys.stdout.write(m.group(1))
sys.exit(int(m.group(2)))
EOF
