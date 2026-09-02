#!/bin/bash
# sanders-battery-guard.sh — Monitora e limita a carga da bateria em 80%
# Evita a degradacao e estufamento da bateria em operacao 24/7 na tomada.

set -euo pipefail

HIGH_LIMIT=80
LOW_LIMIT=70
BAT_SYS="/sys/class/power_supply/battery"

# Se o subsistema de bateria nao estiver exposto ou o aparelho estiver com bateria dummy, encerra silenciosamente.
[ -d "$BAT_SYS" ] || exit 0

CAP=$(awk '{print ($1 ~ /^[0-9]+$/ ? $1 : 0)}' "$BAT_SYS/capacity" 2>/dev/null || echo "0")
STATUS=$(awk '{print ($1 != "" ? $1 : "Unknown")}' "$BAT_SYS/status" 2>/dev/null || echo "Unknown")
USB_ONLINE=$(cat /sys/class/power_supply/usb/online 2>/dev/null || echo "1")

if [ "$CAP" -ge "$HIGH_LIMIT" ] && [ "$STATUS" = "Charging" ]; then
    if [ -w "$BAT_SYS/online" ]; then
        echo 0 > "$BAT_SYS/online" 2>/dev/null || true
    elif [ -w "$BAT_SYS/charging_enabled" ]; then
        echo 0 > "$BAT_SYS/charging_enabled" 2>/dev/null || true
    fi
    logger -t sanders-battery-guard "Bateria atingiu ${CAP}%. Interrompendo carregamento (limite ${HIGH_LIMIT}%)."
elif [ "$CAP" -le "$LOW_LIMIT" ] && [ "$STATUS" != "Charging" ] && [ "$USB_ONLINE" = "1" ]; then
    if [ -w "$BAT_SYS/online" ]; then
        echo 1 > "$BAT_SYS/online" 2>/dev/null || true
    elif [ -w "$BAT_SYS/charging_enabled" ]; then
        echo 1 > "$BAT_SYS/charging_enabled" 2>/dev/null || true
    fi
    logger -t sanders-battery-guard "Bateria em ${CAP}%. Retomando carregamento (histerese ${LOW_LIMIT}%)."
fi

