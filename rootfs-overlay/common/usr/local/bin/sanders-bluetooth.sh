#!/bin/bash
# ==============================================================================
# sanders-bluetooth.sh — Utilitário de Gerenciamento do Bluetooth (WCN3680B)
# ==============================================================================
# Suporta consulta de status, varredura de dispositivos, pareamento e controle
# de energia do rádio Bluetooth do Moto G5 Plus (potter) / G5s Plus (sanders).
# ==============================================================================

set -euo pipefail

CMD="${1:-status}"
shift || true

require_controller() {
    if [ ! -d /sys/class/bluetooth/hci0 ]; then
        echo "[sanders-bluetooth] ERRO: Controlador hci0 não encontrado!" >&2
        echo "Verifique se o firmware WCNSS e o driver btqcomsmd estão ativos." >&2
        exit 1
    fi
}

cmd_status() {
    require_controller

    echo "====================================================="
    echo "  📡 Status do Subsistema Bluetooth (WCN3680B)"
    echo "====================================================="

    # 1. Hardware e MAC
    local mac
    if command -v bluetoothctl >/dev/null 2>&1 && systemctl is-active --quiet bluetooth 2>/dev/null; then
        mac=$(bluetoothctl list 2>/dev/null | awk '/Controller/ {print $2; exit}')
    else
        mac=$(script -qc "btmgmt --index 0 info" /dev/null 2>/dev/null | awk '/^[[:space:]]*addr / {print $2; exit}')
    fi
    [ -z "$mac" ] && mac="Desconhecido"
    local chip="Qualcomm WCN3680B (btqcomsmd)"
    echo "Controlador : hci0 ($chip)"
    echo "Endereço MAC: $mac (persistido de /persist)"

    # 2. RFKill / Energia
    local rf_state
    rf_state=$(rfkill list bluetooth 2>/dev/null | awk '/Soft blocked:/ {print ($3=="yes"?"Bloqueado (Soft)":"Desbloqueado")}')
    [ -z "$rf_state" ] && rf_state="Ativo"
    echo "RFKill      : $rf_state"

    # 3. Daemon BlueZ
    if systemctl is-active --quiet bluetooth 2>/dev/null; then
        echo "Serviço     : bluetooth.service (bluetoothd ATIVO)"
    else
        echo "Serviço     : bluetooth.service (INATIVO)"
    fi

    # 4. Estado Detalhado do Controlador via bluetoothctl
    if command -v bluetoothctl >/dev/null 2>&1 && systemctl is-active --quiet bluetooth 2>/dev/null; then
        local show_out
        show_out=$(bluetoothctl show 2>/dev/null || true)
        if [ -n "$show_out" ]; then
            echo "$show_out" | awk '
                /Name:/ { printf "Nome Local  : %s\n", substr($0, index($0,$2)) }
                /Powered:/ { printf "Energia     : %s\n", $2 }
                /Discoverable:/ { printf "Visibilidade: %s\n", $2 }
                /Pairable:/ { printf "Pareável    : %s\n", $2 }
                /Discovering:/ { printf "Varredura   : %s\n", $2 }
            '
        fi
    fi

    # 5. Dispositivos Pareados
    echo -e "\n--- [ Dispositivos Pareados ] ---"
    if command -v bluetoothctl >/dev/null 2>&1 && systemctl is-active --quiet bluetooth 2>/dev/null; then
        local devs
        devs=$(bluetoothctl paired-devices 2>/dev/null || true)
        if [ -n "$devs" ]; then
            echo "$devs" | awk '{ printf "  • %-18s %s\n", $2, substr($0, index($0,$3)) }'
        else
            echo "  Nenhum dispositivo pareado."
        fi
    else
        echo "  (Inicie o serviço bluetoothd para consultar dispositivos pareados)"
    fi
    echo "====================================================="
}

cmd_on() {
    require_controller
    echo "[sanders-bluetooth] Ligando rádio Bluetooth..."
    rfkill unblock bluetooth 2>/dev/null || true
    if command -v bluetoothctl >/dev/null 2>&1 && systemctl is-active --quiet bluetooth 2>/dev/null; then
        bluetoothctl power on >/dev/null 2>&1 || true
    else
        btmgmt --index 0 power on >/dev/null 2>&1 || true
    fi
    echo "[sanders-bluetooth] Rádio Bluetooth LIGADO."
}

cmd_off() {
    require_controller
    echo "[sanders-bluetooth] Desligando rádio Bluetooth..."
    if command -v bluetoothctl >/dev/null 2>&1 && systemctl is-active --quiet bluetooth 2>/dev/null; then
        bluetoothctl power off >/dev/null 2>&1 || true
    else
        btmgmt --index 0 power off >/dev/null 2>&1 || true
    fi
    echo "[sanders-bluetooth] Rádio Bluetooth DESLIGADO."
}

cmd_scan() {
    require_controller
    local timeout_sec="${1:-10}"
    echo "[sanders-bluetooth] Iniciando varredura por ${timeout_sec}s..."

    if command -v bluetoothctl >/dev/null 2>&1 && systemctl is-active --quiet bluetooth 2>/dev/null; then
        bluetoothctl power on >/dev/null 2>&1 || true
        # Varredura via bluetoothctl
        timeout "${timeout_sec}" bluetoothctl --timeout "${timeout_sec}" scan on 2>/dev/null || true
        echo -e "\n--- [ Dispositivos Descobertos ] ---"
        bluetoothctl devices 2>/dev/null | awk '{ printf "  %-18s %s\n", $2, substr($0, index($0,$3)) }'
    else
        # Fallback via btmgmt se bluetoothd estiver desligado
        btmgmt --index 0 power on >/dev/null 2>&1 || true
        btmgmt --index 0 le on >/dev/null 2>&1 || true
        timeout "${timeout_sec}" btmgmt --index 0 find 2>/dev/null | awk '
            /dev_found:/ {
                addr = $3;
                rssi = $8;
            }
            /name / {
                name = substr($0, index($0,$2));
                if (addr != "") {
                    printf "  %-18s (RSSI %4s dBm) %s\n", addr, rssi, name;
                    addr = "";
                }
            }
        ' || true
    fi
}

cmd_pair() {
    require_controller
    local target="${1:-}"
    if [ -z "$target" ]; then
        echo "Uso: sanders-bluetooth.sh pair <MAC_ADDRESS>" >&2
        exit 1
    fi
    echo "[sanders-bluetooth] Pareando com $target..."
    bluetoothctl pair "$target"
}

cmd_connect() {
    require_controller
    local target="${1:-}"
    if [ -z "$target" ]; then
        echo "Uso: sanders-bluetooth.sh connect <MAC_ADDRESS>" >&2
        exit 1
    fi
    echo "[sanders-bluetooth] Conectando a $target..."
    bluetoothctl connect "$target"
}

cmd_disconnect() {
    require_controller
    local target="${1:-}"
    if [ -z "$target" ]; then
        echo "Uso: sanders-bluetooth.sh disconnect <MAC_ADDRESS>" >&2
        exit 1
    fi
    echo "[sanders-bluetooth] Desconectando de $target..."
    bluetoothctl disconnect "$target"
}

cmd_remove() {
    require_controller
    local target="${1:-}"
    if [ -z "$target" ]; then
        echo "Uso: sanders-bluetooth.sh remove <MAC_ADDRESS>" >&2
        exit 1
    fi
    echo "[sanders-bluetooth] Removendo $target dos dispositivos pareados..."
    bluetoothctl remove "$target"
}

cmd_help() {
    echo "Uso: sanders-bluetooth.sh [comando]"
    echo ""
    echo "Comandos disponíveis:"
    echo "  status            Exibe informações detalhadas de hardware e conexão (padrão)"
    echo "  on                Liga o rádio Bluetooth"
    echo "  off               Desliga o rádio Bluetooth"
    echo "  scan [segundos]   Varre dispositivos no ambiente (padrão: 10s)"
    echo "  pair <MAC>        Inicia pareamento com dispositivo"
    echo "  connect <MAC>     Conecta a dispositivo pareado"
    echo "  disconnect <MAC>  Desconecta de dispositivo"
    echo "  remove <MAC>      Remove emparelhamento com dispositivo"
    echo "  help              Exibe esta mensagem de ajuda"
}

case "$CMD" in
    status|--status|-s)
        cmd_status
        ;;
    on|--on)
        cmd_on
        ;;
    off|--off)
        cmd_off
        ;;
    scan|--scan)
        cmd_scan "${1:-10}"
        ;;
    pair)
        cmd_pair "${1:-}"
        ;;
    connect)
        cmd_connect "${1:-}"
        ;;
    disconnect)
        cmd_disconnect "${1:-}"
        ;;
    remove)
        cmd_remove "${1:-}"
        ;;
    help|--help|-h)
        cmd_help
        ;;
    *)
        echo "Comando desconhecido: $CMD" >&2
        cmd_help
        exit 1
        ;;
esac
