#!/bin/bash
# Script de gerenciamento de CPU Frequency Governor para Moto G5 series
# Uso: sanders-cpufreq.sh [schedutil|powersave|performance|ondemand]

set -euo pipefail

TARGET_GOV="${1:-schedutil}"

# Processa via awk a lista de CPUs com cpufreq ativo
CPUS=$(find /sys/devices/system/cpu/ -maxdepth 2 -name "scaling_governor" 2>/dev/null | awk -F'/' '{print $6}')

if [ -z "$CPUS" ]; then
    # Nao e erro: msm8953 mainline ainda nao expoe cpufreq (falta driver
    # de clock-controller de CPU). Sai limpo em vez de falhar o boot.
    echo "[cpufreq] Nenhuma interface cpufreq encontrada em sysfs, pulando." >&2
    exit 0
fi

echo "[cpufreq] Aplicando governor '$TARGET_GOV'..."

for cpu in $CPUS; do
    SYS_PATH="/sys/devices/system/cpu/$cpu/cpufreq"
    
    if [ ! -f "$SYS_PATH/scaling_available_governors" ]; then
        continue
    fi

    # Valida usando awk se o governor desejado está disponível para esta CPU
    AVAILABLE=$(cat "$SYS_PATH/scaling_available_governors")
    IS_VALID=$(echo "$AVAILABLE" | awk -v target="$TARGET_GOV" '{
        for (i=1; i<=NF; i++) {
            if ($i == target) { print "1"; exit }
        }
        print "0"
    }')

    if [ "$IS_VALID" -eq 1 ]; then
        echo "$TARGET_GOV" > "$SYS_PATH/scaling_governor"
        CUR_GOV=$(cat "$SYS_PATH/scaling_governor")
        CUR_FREQ=$(cat "$SYS_PATH/scaling_cur_freq" 2>/dev/null || echo "N/A")
        echo "[cpufreq] $cpu: governor=$CUR_GOV freq=${CUR_FREQ}kHz"
    else
        echo "[cpufreq] WARN: governor '$TARGET_GOV' não suportado em $cpu (disponíveis: $AVAILABLE)" >&2
    fi
done
