#!/bin/sh
# Teste de durabilidade da flash sem reboot: A salvo com "dormir" limpo, B salvo só
# com flush; depois N cortes de energia simulados (CPU do ANS parada à força) seguidos.
# Uso no iPhone: sh cortes-duplos.sh [cortes]
N=${1:-2}
de=$(cat /sys/module/apple_ans1/parameters/escrita_de)
A=$((de + 1000)); B=$((de + 2000))
umount /mnt/flash 2>/dev/null; dmsetup remove linux 2>/dev/null
espera() { for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do [ -b /dev/ans0 ] && return; sleep 0.5; done; echo "ans0 não voltou"; exit 1; }
carrega() { modprobe apple-ans1; espera; sleep 1; }
corta() { regs -w 0x208040028 0 >/dev/null; sleep 0.2; rmmod apple_ans1; carrega; }
bloco() { dd if=/dev/ans0 bs=4096 skip=$1 count=1 iflag=direct 2>/dev/null | md5sum | cut -c1-12; }
le() { echo "$1: A=$(bloco $A) B=$(bloco $B)"; }
dd if=/dev/urandom of=/tmp/a bs=4096 count=1 2>/dev/null
dd if=/dev/urandom of=/tmp/b bs=4096 count=1 2>/dev/null
echo "esperado: A=$(md5sum < /tmp/a | cut -c1-12) B=$(md5sum < /tmp/b | cut -c1-12)  (zeros=620f0b67a91f)"
dd if=/tmp/a of=/dev/ans0 bs=4096 seek=$A oflag=direct conv=fsync 2>/dev/null
rmmod apple_ans1; carrega                      # A salvo com dormir limpo
le "depois do dormir limpo"
f0=$(dmesg | grep -c "ANS dormindo")
dd if=/tmp/b of=/dev/ans0 bs=4096 seek=$B oflag=direct conv=fsync 2>/dev/null
echo "flushes feitos: $(( $(dmesg | grep -c "ANS dormindo") - f0 ))"
le "B com flush"
i=1; while [ $i -le $N ]; do corta; le "depois de $i corte(s)"; i=$((i + 1)); done
