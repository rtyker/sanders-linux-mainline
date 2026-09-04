#!/usr/bin/env bash
# Script para conectar na serial CDC ACM do Moto G5s Plus / Moto G5 Plus
# Configura o picocom com tratamento de quebra de linha (CR/LF) automático.
#
# Bug conhecido do host (nao do aparelho): depois de um reboot do phone,
# o gadget composite USB (1d6b:0104) as vezes fica preso num
# "reset high-speed USB device" duplo e nunca re-registra as interfaces
# cdc_acm/cdc_ether — /dev/ttyACM0 nunca aparece, apesar do aparelho ja
# ter bootado normalmente (confirmado via 'lsusb' mostrando o device
# enumerado, so faltando os endpoints). Sintoma visto ao vivo 2026-09-04.
# Fix: forcar um unbind/bind do device no driver generico "usb" faz o
# host re-enumerar do zero e resolve. Esse script agora detecta esse
# travamento sozinho (timeout esperando o ttyACM0) e tenta o unbind/bind
# automaticamente antes de desistir.

set -uo pipefail

PORT="${1:-/dev/ttyACM0}"
WAIT_BEFORE_KICK=8    # segundos esperando o normal antes de suspeitar do bug
MAX_KICKS=3

USB_VENDOR="1d6b"
USB_PRODUCT="0104"

echo "============================================================"
echo " Conectando no terminal serial: $PORT"
echo " Pressione Ctrl+A e depois Ctrl+X para sair do picocom"
echo "============================================================"

find_gadget_devpath() {
    # Acha o path sysfs (ex: "3-4") do gadget composite do phone pelo
    # idVendor/idProduct. Pode nao ter interfaces registradas ainda —
    # e exatamente o caso que queremos detectar.
    local dev
    for dev in /sys/bus/usb/devices/*/; do
        [ -f "${dev}idVendor" ] || continue
        [ -f "${dev}idProduct" ] || continue
        if [ "$(cat "${dev}idVendor" 2>/dev/null)" = "$USB_VENDOR" ] &&
           [ "$(cat "${dev}idProduct" 2>/dev/null)" = "$USB_PRODUCT" ]; then
            basename "${dev%/}"
            return 0
        fi
    done
    return 1
}

kick_usb_reenum() {
    local devpath
    devpath=$(find_gadget_devpath) || {
        echo "  (nao achei o gadget 1d6b:0104 em /sys/bus/usb/devices/, nao da pra forcar re-enum)"
        return 1
    }
    echo "  Forcando re-enumeracao USB do device $devpath (unbind/bind)..."
    if ! echo "$devpath" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1; then
        echo "  Falhou o unbind (precisa de sudo?). Pulei o kick."
        return 1
    fi
    sleep 1
    echo "$devpath" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
    return 0
}

if [ ! -c "$PORT" ]; then
    echo "Aguardando dispositivo $PORT aparecer..."
    kicks=0
    elapsed=0
    while [ ! -c "$PORT" ]; do
        sleep 1
        elapsed=$((elapsed + 1))
        echo -n "."
        if [ "$elapsed" -ge "$WAIT_BEFORE_KICK" ] && [ "$kicks" -lt "$MAX_KICKS" ]; then
            echo ""
            echo "  $PORT nao apareceu em ${elapsed}s — pode ser o bug de"
            echo "  enumeracao USB travada (reset duplo sem re-registrar as"
            echo "  interfaces). Tentando destravar..."
            kick_usb_reenum
            kicks=$((kicks + 1))
            elapsed=0
        fi
    done
    echo ""
    echo "Dispositivo $PORT encontrado!"
fi

# Executa picocom com mapeamento correto de Newline/Carriage Return
exec picocom --omap crlf --imap lfcrlf --baud 115200 "$PORT"
