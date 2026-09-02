#!/bin/bash
# Script de Telemetria e Monitoramento Térmico para Moto G5 series
# Uso: sanders-thermal.sh [--json] [--warn=WARN_TEMP] [--crit=CRIT_TEMP]

set -euo pipefail

WARN_TEMP=70
CRIT_TEMP=85
FORMAT="text"

for arg in "$@"; do
    case "$arg" in
        --json) FORMAT="json" ;;
        --warn=*) WARN_TEMP="${arg#*=}" ;;
        --crit=*) CRIT_TEMP="${arg#*=}" ;;
        *) echo "Uso: $0 [--json] [--warn=70] [--crit=85]" >&2; exit 1 ;;
    esac
done

# Coleta os tipos e valores de temperatura das zonas térmicas do sysfs.
# Indexa por nome da zona (ex.: "thermal_zone0"), extraido do FILENAME —
# NAO por um contador sequencial (idx++ a cada novo arquivo): como os
# arquivos *_/type vem todos antes dos *_/temp no glob, um contador
# global deixa os dois arrays desalinhados (types[0..N] vs temps[N..2N])
# e temps[i] fica sempre vazio -> toda leitura reporta 0.0°C.
TZ_DATA=$(awk '
    {
        match(FILENAME, /thermal_zone[0-9]+/)
        zone = substr(FILENAME, RSTART, RLENGTH)
        if (FILENAME ~ /type$/) {
            types[zone] = $0
        } else if (FILENAME ~ /temp$/) {
            temps[zone] = $0
        }
    }
    END {
        for (zone in types) {
            t_val = temps[zone] + 0
            if (t_val > 10000) { t_val = t_val / 1000 }
            print types[zone] " " t_val
        }
    }
' /sys/class/thermal/thermal_zone*/type /sys/class/thermal/thermal_zone*/temp 2>/dev/null || true)

if [ -z "$TZ_DATA" ]; then
    echo "[thermal] Nenhum sensor de temperatura encontrado em /sys/class/thermal/" >&2
    exit 1
fi

# Formata via awk
echo "$TZ_DATA" | awk -v fmt="$FORMAT" -v warn="$WARN_TEMP" -v crit="$CRIT_TEMP" '
BEGIN {
    max_t = -999
    min_t = 999
    sum_t = 0
    cnt_t = 0
    has_warn = 0
    has_crit = 0
}

{
    name = $1
    temp = $2 + 0
    
    if (temp > max_t) max_t = temp
    if (temp < min_t) min_t = temp
    sum_t += temp
    cnt_t++
    
    status = "OK"
    if (temp >= crit) {
        status = "CRITICAL"
        has_crit = 1
    } else if (temp >= warn) {
        status = "WARNING"
        has_warn = 1
    }
    
    names[cnt_t] = name
    values[cnt_t] = temp
    statuses[cnt_t] = status
}

END {
    avg_t = (cnt_t > 0) ? (sum_t / cnt_t) : 0
    
    if (fmt == "json") {
        printf "{\n"
        printf "  \"summary\": {\n"
        printf "    \"max_celsius\": %.1f,\n", max_t
        printf "    \"min_celsius\": %.1f,\n", min_t
        printf "    \"avg_celsius\": %.1f,\n", avg_t
        printf "    \"warning_threshold\": %d,\n", warn
        printf "    \"critical_threshold\": %d,\n", crit
        printf "    \"status\": \"%s\"\n", (has_crit ? "CRITICAL" : (has_warn ? "WARNING" : "OK"))
        printf "  },\n"
        printf "  \"sensors\": {\n"
        for (i = 1; i <= cnt_t; i++) {
            printf "    \"%s\": %.1f%s\n", names[i], values[i], (i == cnt_t ? "" : ",")
        }
        printf "  }\n"
        printf "}\n"
    } else {
        printf "=== 🌡️ Telemetria Térmica (SoC MSM8953) ===\n"
        printf "%-25s %-12s %-10s\n", "SENSOR", "TEMPERATURA", "STATUS"
        printf "---------------------------------------------------\n"
        for (i = 1; i <= cnt_t; i++) {
            printf "%-25s %6.1f°C      [%s]\n", names[i], values[i], statuses[i]
        }
        printf "---------------------------------------------------\n"
        printf "Resumo: Máx: %.1f°C | Mín: %.1f°C | Média: %.1f°C | Status Geral: [%s]\n",
            max_t, min_t, avg_t, (has_crit ? "CRITICAL" : (has_warn ? "WARNING" : "OK"))
    }
    
    if (has_crit) exit 2
    if (has_warn) exit 1
}
'
