#!/bin/bash
# Script de sincronização de relógio NTP (timesync) para Moto G5 series
# Aguarda conexão de rede/DNS e força a sincronização via systemd-timesyncd.

set -euo pipefail

echo "[timesync] Aguardando conectividade de rede e resolução DNS para NTP..."

# Aguarda até 30s por resolução DNS dos servidores NTP
REACHABLE=0
for i in $(seq 1 30); do
    if getent hosts a.st1.ntp.br >/dev/null 2>&1 || getent hosts pool.ntp.org >/dev/null 2>&1; then
        REACHABLE=1
        break
    fi
    sleep 1
done

if [ "$REACHABLE" -eq 1 ]; then
    echo "[timesync] Conexão com servidores NTP estabelecida. Reiniciando systemd-timesyncd..."
    timedatectl set-ntp true 2>/dev/null || true
    systemctl restart systemd-timesyncd 2>/dev/null || true
    
    # Aguarda o alinhamento do relógio (até 15s)
    for _ in $(seq 1 15); do
        SYNCED=$(timedatectl status 2>/dev/null | awk '/System clock synchronized: yes/ { found=1 } END { print found+0 }')
        if [ "$SYNCED" -eq 1 ]; then
            break
        fi
        sleep 1
    done
    
    NOW=$(date '+%Y-%m-%d %H:%M:%S %Z')
    echo "[timesync] Relógio ajustado: $NOW"
else
    echo "[timesync] WARN: Servidores NTP inalcançáveis. Mantendo horário atual de boot." >&2
fi
