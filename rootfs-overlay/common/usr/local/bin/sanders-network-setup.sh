#!/bin/bash
# sanders-network-setup.sh — Configuração de rede standalone para o sanders.
#
# Suporta:
#   1) Wi-Fi (wcn36xx): configura wpa_supplicant com SSID/senha via wpa_passphrase.
#   2) USB OTG Ethernet: detecta adaptador USB-Ethernet plugado no modo host.
#   3) Status: mostra interfaces, IPs e estado da conexão.
#
# Uso:
#   sanders-network-setup.sh wifi <SSID> <senha>    # Configura Wi-Fi
#   sanders-network-setup.sh wifi-clear              # Remove config Wi-Fi
#   sanders-network-setup.sh status                  # Mostra estado da rede
#   sanders-network-setup.sh usb-status              # Detecta adaptador USB Ethernet

set -euo pipefail

WPA_CONF="/etc/wpa_supplicant/wpa_supplicant-wlan0.conf"
WPA_TEMPLATE='ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=root
update_config=1'

msg()  { echo -e "\033[1;36m[network-setup]\033[0m $*"; }
warn() { echo -e "\033[1;33m[WARN]\033[0m $*" >&2; }
err()  { echo -e "\033[1;31m[ERRO]\033[0m $*" >&2; exit 1; }

cmd_wifi() {
    local ssid="${1:-}"
    local pass="${2:-}"

    if [ -z "$ssid" ]; then
        err "Uso: $0 wifi <SSID> <senha>"
    fi

    # Valida que wpa_supplicant esta instalado
    command -v wpa_passphrase >/dev/null 2>&1 \
        || err "wpa_supplicant nao instalado. Rode: pacman -S wpa_supplicant"

    # Valida que wlan0 existe
    [ -d /sys/class/net/wlan0 ] \
        || err "Interface wlan0 nao encontrada. Verifique se o driver wcn36xx carregou (dmesg | grep wcn36xx)."

    msg "Gerando config wpa_supplicant para SSID='$ssid'..."

    # Gera config com wpa_passphrase (injeta PSK hasheado)
    mkdir -p "$(dirname "$WPA_CONF")"
    {
        echo "$WPA_TEMPLATE"
        echo ""
        if [ -n "$pass" ]; then
            wpa_passphrase "$ssid" "$pass"
        else
            # Rede aberta (sem senha)
            cat <<OPENEOF
network={
    ssid="$ssid"
    key_mgmt=NONE
}
OPENEOF
        fi
    } > "$WPA_CONF"

    chmod 600 "$WPA_CONF"
    msg "Config salva em $WPA_CONF"

    # Reinicia wpa_supplicant pra aplicar
    if systemctl is-active --quiet wpa_supplicant@wlan0.service 2>/dev/null; then
        msg "Reiniciando wpa_supplicant@wlan0..."
        systemctl restart wpa_supplicant@wlan0.service
    else
        msg "Iniciando wpa_supplicant@wlan0..."
        systemctl start wpa_supplicant@wlan0.service 2>/dev/null \
            || warn "Falha ao iniciar wpa_supplicant (pode falhar sem fix WPA2 — veja HARDWARE_STATUS.md)"
    fi

    msg "Wi-Fi configurado. Verifique com: $0 status"
}

cmd_wifi_clear() {
    msg "Removendo config Wi-Fi..."
    cat > "$WPA_CONF" <<EOF
$WPA_TEMPLATE
EOF
    chmod 600 "$WPA_CONF"

    if systemctl is-active --quiet wpa_supplicant@wlan0.service 2>/dev/null; then
        systemctl restart wpa_supplicant@wlan0.service
    fi
    msg "Config Wi-Fi limpa. wpa_supplicant continua ativo sem redes configuradas."
}

cmd_status() {
    echo "=========================================="
    echo "  Status de Rede — sanders-linux-mainline"
    echo "=========================================="
    echo ""

    # Listar todas as interfaces de rede
    echo "--- Interfaces ---"
    awk '
    BEGIN { count=0 }
    /^[0-9]+:/ {
        iface = $2
        sub(/:.*/, "", iface)
        flags = $0
        state = (flags ~ /UP/) ? "UP" : "DOWN"
        printf "  %-12s  %s\n", iface, state
        count++
    }
    END { if (count == 0) print "  (nenhuma interface encontrada)" }
    ' /proc/net/dev
    echo ""

    # USB OTG Ethernet
    echo "--- USB OTG Ethernet ---"
    local usb_eth
    usb_eth=$(ip -o link show 2>/dev/null | awk -F': ' '/enx|eth0/ {print $2}' | head -1)
    if [ -n "$usb_eth" ]; then
        local usb_ip
        usb_ip=$(ip -4 addr show dev "$usb_eth" 2>/dev/null | awk '/inet /{print $2; exit}')
        echo "  Adaptador: $usb_eth"
        echo "  IP:        ${usb_ip:-nenhum}"
        echo "  Driver:    $(cat /sys/class/net/"$usb_eth"/device/driver/module/refcnt 2>/dev/null || echo 'desconhecido')"
    else
        echo "  Nenhum adaptador USB Ethernet detectado."
        echo "  (Plugue um adaptador USB-C pra Ethernet no modo OTG)"
    fi
    echo ""

    # Wi-Fi
    echo "--- Wi-Fi (wlan0) ---"
    if [ -d /sys/class/net/wlan0 ]; then
        local wlan_state wlan_ip
        wlan_state=$(cat /sys/class/net/wlan0/operstate 2>/dev/null || echo "unknown")
        wlan_ip=$(ip -4 addr show dev wlan0 2>/dev/null | awk '/inet /{print $2; exit}')
        echo "  Estado:    $wlan_state"
        echo "  IP:        ${wlan_ip:-nenhum}"

        if [ -n "$wlan_ip" ]; then
            echo "  Conectado: $(iw dev wlan0 link 2>/dev/null | awk '/Connected to/{print $3; exit}' || echo 'N/A')"
        fi

        # wpa_supplicant
        if systemctl is-active --quiet wpa_supplicant@wlan0.service 2>/dev/null; then
            echo "  wpa_supplicant: ativo"
        else
            echo "  wpa_supplicant: inativo"
        fi
    else
        echo "  Interface wlan0 nao encontrada."
        echo "  (Verifique dmesg | grep wcn36xx)"
    fi
    echo ""

    # USB CDC ECM (gadget mode — conexao ao PC)
    echo "--- USB CDC ECM (usb0 — gadget mode) ---"
    if [ -d /sys/class/net/usb0 ]; then
        local usb0_ip
        usb0_ip=$(ip -4 addr show dev usb0 2>/dev/null | awk '/inet /{print $2; exit}')
        echo "  IP: ${usb0_ip:-nenhum}"
    else
        echo "  Interface usb0 nao encontrada."
    fi
    echo ""

    # Rota default
    echo "--- Rota Default ---"
    ip -4 route show default 2>/dev/null | awk '{printf "  via %s dev %s (metric %s)\n", $3, $5, $0}' || echo "  (nenhuma)"
    echo ""

    # DNS
    echo "--- DNS ---"
    if [ -f /run/systemd/resolve/resolv.conf ]; then
        awk '/^nameserver/{printf "  %s\n", $2}' /run/systemd/resolve/resolv.conf
    elif [ -f /etc/resolv.conf ]; then
        awk '/^nameserver/{printf "  %s\n", $2}' /etc/resolv.conf
    else
        echo "  (nenhum DNS configurado)"
    fi
}

cmd_usb_status() {
    echo "=== USB OTG Ethernet — Status ==="
    echo ""

    # Verifica se DWC3 esta em host mode
    local role
    role=$(cat /sys/class/udc/7000000.usb/device/role 2>/dev/null || echo "desconhecido")
    echo "Modo USB: $role"

    if [ "$role" != "host" ]; then
        echo ""
        echo "O phone esta em gadget mode (periferico)."
        echo "Para usar USB OTG Ethernet:"
        echo "  1. Desconecte o cabo USB do PC"
        echo "  2. Plugue um adaptador USB-C pra Ethernet com cabo de rede"
        echo "  3. O phone automaticamente troca pra host mode"
        echo ""
        echo "Adaptadores compatíveis (CDC ECM/RNDIS/NCM):"
        echo "  - TP-Link UE306 (RTL8153 — driver r8152 no kernel)"
        echo "  - Anker USB-C to Ethernet (AX88179 — driver ax88179_178a)"
        echo "  - UGREEN USB-C to Ethernet (RTL8153)"
        echo "  - Qualquer adaptador CDC ECM padrao"
        return 0
    fi

    echo ""

    # Procura interfaces Ethernet via USB
    local found=0
    for iface_dir in /sys/class/net/*/; do
        local iface
        iface=$(basename "$iface_dir")
        local dev_path
        dev_path=$(readlink -f "$iface_dir/device" 2>/dev/null || continue)

        # Verifica se e dispositivo USB
        if echo "$dev_path" | grep -q 'usb'; then
            local driver
            driver=$(basename "$(readlink "$iface_dir/device/driver" 2>/dev/null)" 2>/dev/null || echo "N/A")
            local ip_addr
            ip_addr=$(ip -4 addr show dev "$iface" 2>/dev/null | awk '/inet /{print $2; exit}')
            local mac
            mac=$(cat "$iface_dir/address" 2>/dev/null || echo "N/A")

            echo "Interface: $iface"
            echo "  Driver:  $driver"
            echo "  MAC:     $mac"
            echo "  IP:      ${ip_addr:-aguardando DHCP...}"
            echo "  Dispositivo: $dev_path"
            echo ""
            found=1
        fi
    done

    if [ "$found" -eq 0 ]; then
        echo "Nenhum adaptador USB Ethernet detectado."
        echo ""
        echo "Verifique:"
        echo "  - lsusb (lista dispositivos USB conectados)"
        echo "  - dmesg | tail -20 (mensagens do kernel sobre USB)"
    fi
}

# --- Main ---
case "${1:-status}" in
    wifi)       cmd_wifi "${2:-}" "${3:-}" ;;
    wifi-clear) cmd_wifi_clear ;;
    status)     cmd_status ;;
    usb-status) cmd_usb_status ;;
    *)
        echo "Uso: $0 {wifi <SSID> [senha] | wifi-clear | status | usb-status}"
        echo ""
        echo "  wifi <SSID> [senha]  Configura Wi-Fi (salva em wpa_supplicant)"
        echo "  wifi-clear           Remove config Wi-Fi"
        echo "  status               Mostra estado de todas as interfaces"
        echo "  usb-status           Detecta adaptador USB Ethernet (OTG mode)"
        exit 1
        ;;
esac
