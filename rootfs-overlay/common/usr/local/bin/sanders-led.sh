#!/bin/bash
# sanders-led.sh — Controle do LED de notificacao frontal (white:notification)
#
# Motivacao: o LED branco frontal esta disponivel via sysfs
# (/sys/class/leds/white:notification) mas so aceita on/off via GPIO
# (sem PWM no mainline atual). Este script fornece modos de sinalizacao
# uteis para status do servidor headless.
#
# Modos:
#   on          — LED aceso fixo (servidor online e operacional)
#   off         — LED apagado
#   blink       — blink periodico via trigger 'timer' (500ms on/off)
#   heartbeat   — padrao heartbeat via trigger 'heartbeat' (atividade do sistema)
#   status      — auto-detecta: blink se sem rede, on se ok
#   pulse N     — pisca N vezes e volta ao estado anterior (notificacao pontual)
#
# Uso:
#   sanders-led.sh on
#   sanders-led.sh status
#   sanders-led.sh pulse 3
#
# Pode ser chamado por systemd services, cron, ou manualmente.

set -euo pipefail

LED_DIR="/sys/class/leds/white:notification"

# --- helpers ----------------------------------------------------------------

die() { echo "[sanders-led] ERRO: $*" >&2; exit 1; }

led_write() {
    # Escreve valor no arquivo sysfs do LED. Uso de printf + awk ao inves
    # de sed para evitar problemas com trailing newlines do sysfs.
    local file="$1" val="$2"
    [ -d "$LED_DIR" ] || die "LED sysfs dir $LED_DIR nao encontrado"
    printf '%s\n' "$val" > "$LED_DIR/$file"
}

led_read() {
    local file="$1"
    awk 'NR==1{print}' "$LED_DIR/$file" 2>/dev/null || echo ""
}

current_brightness() {
    led_read brightness
}

current_trigger() {
    # O arquivo 'trigger' tem formato:
    #   none [heartbeat] timer default-on ...
    # O trigger ativo fica entre colchetes.
    awk '{for(i=1;i<=NF;i++){if($i ~ /^\[.*\]$/){gsub(/[\[\]]/,"",$i); print $i; exit}}}' \
        "$LED_DIR/trigger"
}

# --- comandos ---------------------------------------------------------------

cmd_on() {
    led_write trigger none
    led_write brightness 1
}

cmd_off() {
    led_write trigger none
    led_write brightness 0
}

cmd_blink() {
    # Usa trigger 'timer' com periodo de 500ms (on 500, off 500).
    led_write trigger timer
    led_write delay_on 500
    led_write delay_off 500
}

cmd_heartbeat() {
    led_write trigger heartbeat
}

cmd_status() {
    # Auto-detecta: blink se sem rede, on se ok.
    # Verifica usb0 (gadget), wlan0 (Wi-Fi) e qualquer iface Ethernet
    # USB (enx*/eth0 — OTG host mode com adaptador).
    local has_net=0

    # usb0 (conectado ao host via USB CDC ECM)
    if ip -4 addr show dev usb0 2>/dev/null | awk '/inet /{found=1} END{exit !found}'; then
        has_net=1
    fi
    # wlan0 (Wi-Fi wcn36xx)
    if ip -4 addr show dev wlan0 2>/dev/null | awk '/inet /{found=1} END{exit !found}'; then
        has_net=1
    fi
    # USB OTG Ethernet (enx[MAC], eth0, etc.)
    if [ "$has_net" -eq 0 ]; then
        for iface in /sys/class/net/en* /sys/class/net/eth0; do
            [ -d "$iface" ] || continue
            local name
            name=$(basename "$iface")
            if ip -4 addr show dev "$name" 2>/dev/null | awk '/inet /{found=1} END{exit !found}'; then
                has_net=1
                break
            fi
        done
    fi

    if [ "$has_net" -eq 1 ]; then
        cmd_on
        echo "[sanders-led] status: online (LED aceso)"
    else
        cmd_blink
        echo "[sanders-led] status: sem rede (LED blink)"
    fi
}

cmd_pulse() {
    # Pisca N vezes e restaura o estado anterior.
    local count="${1:-3}"
    case "$count" in
        ''|*[!0-9]*) die "pulse: N precisa ser um numero inteiro (recebido '$count')" ;;
    esac
    local prev_brightness prev_trigger

    prev_brightness=$(current_brightness)
    prev_trigger=$(current_trigger)

    # Garante modo manual para piscar.
    led_write trigger none

    local i=0
    while [ "$i" -lt "$count" ]; do
        led_write brightness 1
        sleep 0.3
        led_write brightness 0
        sleep 0.3
        i=$((i + 1))
    done

    # Restaura estado anterior.
    if [ "$prev_trigger" != "none" ] && [ -n "$prev_trigger" ]; then
        led_write trigger "$prev_trigger"
    else
        led_write brightness "$prev_brightness"
    fi
}

# --- main -------------------------------------------------------------------

usage() {
    cat <<'EOF'
Uso: sanders-led.sh <comando>

Comandos:
  on          LED aceso fixo
  off         LED apagado
  blink       Piscando (500ms on/off)
  heartbeat   Padrao heartbeat do kernel
  status      Auto-detecta: blink se sem rede, on se ok
  pulse N     Pisca N vezes e restaura estado anterior

Exemplos:
  sanders-led.sh on
  sanders-led.sh status
  sanders-led.sh pulse 5
EOF
    exit 1
}

[ -d "$LED_DIR" ] || die "LED sysfs dir $LED_DIR nao encontrado (driver leds-gpio nao carregado?)"

case "${1:-}" in
    on)         cmd_on ;;
    off)        cmd_off ;;
    blink)      cmd_blink ;;
    heartbeat)  cmd_heartbeat ;;
    status)     cmd_status ;;
    pulse)      cmd_pulse "${2:-3}" ;;
    *)          usage ;;
esac
