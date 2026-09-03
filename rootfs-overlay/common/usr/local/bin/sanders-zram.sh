#!/bin/bash
# Script de automacao de zram (Swap comprimido em RAM) para Moto G5 series
# Calcula dinamicamente 50% da Memoria RAM total via awk e inicializa /dev/zram0.

set -euo pipefail

if [ -f /proc/swaps ] && grep -q "/dev/zram0" /proc/swaps; then
    echo "[zram] /dev/zram0 ja esta ativo como swap."
    exit 0
fi

# Modprobe do modulo caso necessario
modprobe zram num_devices=1 2>/dev/null || true

# Calcula 50% da Memoria Total em Megabytes via awk
ZRAM_SIZE_MB=$(awk '/MemTotal:/ { print int(($2 / 1024) * 0.5) }' /proc/meminfo)

if [ -z "$ZRAM_SIZE_MB" ] || [ "$ZRAM_SIZE_MB" -le 0 ]; then
    ZRAM_SIZE_MB=1024
fi

echo "[zram] Configurando /dev/zram0 com ${ZRAM_SIZE_MB}MB (50% RAM)..."

if command -v zramctl >/dev/null 2>&1; then
    zramctl --find --size "${ZRAM_SIZE_MB}M" --algorithm zstd 2>/dev/null || \
    zramctl --find --size "${ZRAM_SIZE_MB}M" --algorithm lz4 2>/dev/null || \
    zramctl --find --size "${ZRAM_SIZE_MB}M" || true
elif [ -b /dev/zram0 ] || [ -d /sys/block/zram0 ]; then
    echo "${ZRAM_SIZE_MB}M" > /sys/block/zram0/disksize 2>/dev/null || \
    echo "$((ZRAM_SIZE_MB * 1024 * 1024))" > /sys/block/zram0/disksize
fi

# Confere explicitamente que /dev/zram0 existe antes de seguir pro
# mkswap/swapon — se toda a cadeia zramctl/sysfs acima falhar
# silenciosamente, mkswap num device inexistente daria um erro obscuro
# em vez de uma mensagem clara do que realmente faltou.
if [ ! -b /dev/zram0 ]; then
    echo "[zram] ERRO: dispositivo /dev/zram0 indisponivel no kernel (zramctl/sysfs falharam)." >&2
    exit 1
fi

mkswap /dev/zram0 >/dev/null
swapon -p 100 /dev/zram0

echo "[zram] /dev/zram0 ativado com sucesso como swap (prioridade 100)."
