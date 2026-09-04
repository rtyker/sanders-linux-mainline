#!/usr/bin/env bash
# Ponto de entrada UNICO pra restabelecer acesso ao aparelho (sanders/potter)
# depois de um boot/reboot: serial (/dev/ttyACM0) E rede USB (10.42.0.2),
# ja que os dois dependem do MESMO gadget composite USB re-enumerando
# corretamente no host. Cobre todos os modos de falha vistos ao vivo
# (2026-09-04) numa sessao so, em vez de cada um exigir seu proprio
# workaround manual (unbind/bind, ip addr, 08-host-net.sh, matar leitor
# preso etc) toda vez que o aparelho reinicia.
#
# Uso:
#   conecta_serial.sh              # conecta via picocom (interativo), já
#                                   # com toda a recuperação automática abaixo
#   conecta_serial.sh --ensure     # so garante conectividade (serial + rede)
#                                   # e sai, SEM abrir picocom — pensado pra
#                                   # ser chamado por agentes/scripts antes de
#                                   # eles mesmos falarem com /dev/ttyACM0 ou
#                                   # SSH, em vez de reimplementar o
#                                   # unbind/bind ou o ip addr na mao
#   conecta_serial.sh --no-network # pula o passo de rede (só serial)
#   conecta_serial.sh [PORT]       # PORT default: /dev/ttyACM0
#
# Modos de falha cobertos (todos vistos ao vivo, nao hipoteticos):
#   1. /dev/ttyACM0 demora/nunca aparece depois do reboot — so precisa
#      esperar mais (aparelho ainda bootando).
#   2. Bug de host: o gadget composite (1d6b:0104) as vezes fica preso num
#      "reset high-speed USB device" DUPLO sem re-registrar cdc_acm/cdc_ether
#      — porta nunca aparece embora o aparelho ja tenha bootado (visivel via
#      'lsusb'). Fix: unbind/bind do device no driver "usb" generico forca
#      o host a re-enumerar do zero.
#   3. Mesmo com a porta serial em pé, a interface de rede
#      (enp0s20f0u4/usbX) perde o IP do lado do host a cada re-enumeracao —
#      SSH em 10.42.0.2 fica inacessivel ate rodar 08-host-net.sh de novo.
#      Esse script agora roda isso automaticamente (best-effort).
#   4. Um processo anterior (script interrompido, leitura travada) pode
#      ficar preso segurando o fd de /dev/ttyACM0 — escritas/leituras
#      subsequentes bloqueiam pra sempre. Limpo com fuser -k antes de usar.

set -uo pipefail

REPO_SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ENSURE_ONLY=0
DO_NETWORK=1
PORT=""

for arg in "$@"; do
    case "$arg" in
        --ensure|--check) ENSURE_ONLY=1 ;;
        --no-network)     DO_NETWORK=0 ;;
        -h|--help)
            sed -n '2,33p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *) PORT="$arg" ;;
    esac
done
PORT="${PORT:-/dev/ttyACM0}"

WAIT_BEFORE_KICK=8    # segundos esperando o normal antes de suspeitar do bug #2
MAX_KICKS=3

USB_VENDOR="1d6b"
USB_PRODUCT="0104"
GADGET_MAC="02:11:22:33:44:55"

log() { echo "[conecta_serial] $*"; }

if [ "$ENSURE_ONLY" -eq 0 ]; then
    echo "============================================================"
    echo " Conectando no terminal serial: $PORT"
    echo " Pressione Ctrl+A e depois Ctrl+X para sair do picocom"
    echo "============================================================"
fi

find_gadget_devpath() {
    # Acha o path sysfs (ex: "3-4") do gadget composite do phone pelo
    # idVendor/idProduct. Pode nao ter interfaces registradas ainda —
    # e exatamente o caso que queremos detectar (modo de falha #2).
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
        log "  (nao achei o gadget $USB_VENDOR:$USB_PRODUCT em /sys/bus/usb/devices/, nao da pra forcar re-enum)"
        return 1
    }
    log "  Forcando re-enumeracao USB do device $devpath (unbind/bind)..."
    if ! echo "$devpath" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null 2>&1; then
        log "  Falhou o unbind (precisa de sudo?). Pulei o kick."
        return 1
    fi
    sleep 1
    echo "$devpath" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null 2>&1
    return 0
}

clear_stale_holders() {
    # Modo de falha #4: processo anterior travado segurando o fd da porta.
    # fuser -k so mata quem tem o arquivo aberto — inofensivo se ninguem tiver.
    [ -e "$PORT" ] || return 0
    if command -v fuser >/dev/null 2>&1; then
        local holders
        holders="$(fuser "$PORT" 2>/dev/null)"
        if [ -n "$holders" ]; then
            log "  $PORT tem processo(s) preso(s) ($holders) — limpando antes de conectar..."
            fuser -k "$PORT" >/dev/null 2>&1 || true
            sleep 1
        fi
    fi
}

fix_host_network() {
    # Modo de falha #3: a interface de rede sobrevive a re-enumeracao mas
    # perde o IP do lado do host — sintoma classico eh "SSH a 10.42.0.2
    # da connection timed out" mesmo com a porta serial ja respondendo.
    # 08-host-net.sh ja faz auto-deteccao de interface + WAN + regras NAT
    # de forma idempotente, entao so delegamos pra ele em vez de duplicar
    # a logica aqui.
    local iface
    iface="$(ip -o link 2>/dev/null | awk -F': ' -v mac="$GADGET_MAC" '$0 ~ mac {print $2; exit}')"
    if [ -z "$iface" ]; then
        log "  Rede: interface do gadget (MAC $GADGET_MAC) ainda nao apareceu, pulando por ora."
        return 1
    fi
    log "  Rede: rodando 08-host-net.sh pra reconfigurar IP/NAT em $iface..."
    if sudo bash "$REPO_SCRIPTS/08-host-net.sh" >/tmp/conecta_serial-host-net.log 2>&1; then
        log "  Rede: OK — 10.42.0.2 deve estar alcancavel agora."
        return 0
    else
        log "  Rede: 08-host-net.sh falhou (log em /tmp/conecta_serial-host-net.log) — nao bloqueante."
        return 1
    fi
}

# --- Serial: espera a porta aparecer, com kick automatico se travar -------

if [ ! -c "$PORT" ]; then
    log "Aguardando dispositivo $PORT aparecer..."
    kicks=0
    elapsed=0
    while [ ! -c "$PORT" ]; do
        sleep 1
        elapsed=$((elapsed + 1))
        [ "$ENSURE_ONLY" -eq 0 ] && echo -n "."
        if [ "$elapsed" -ge "$WAIT_BEFORE_KICK" ] && [ "$kicks" -lt "$MAX_KICKS" ]; then
            [ "$ENSURE_ONLY" -eq 0 ] && echo ""
            log "  $PORT nao apareceu em ${elapsed}s — pode ser o bug de"
            log "  enumeracao USB travada (reset duplo sem re-registrar as"
            log "  interfaces). Tentando destravar..."
            kick_usb_reenum
            kicks=$((kicks + 1))
            elapsed=0
        fi
    done
    [ "$ENSURE_ONLY" -eq 0 ] && echo ""
    log "Dispositivo $PORT encontrado!"
fi

clear_stale_holders

# --- Rede: best-effort, nao trava a conexao serial se falhar --------------

if [ "$DO_NETWORK" -eq 1 ]; then
    fix_host_network || true
fi

if [ "$ENSURE_ONLY" -eq 1 ]; then
    log "Pronto: $PORT disponivel$( [ "$DO_NETWORK" -eq 1 ] && echo ", rede verificada" )."
    exit 0
fi

# Executa picocom com mapeamento correto de Newline/Carriage Return
exec picocom --omap crlf --imap lfcrlf --baud 115200 "$PORT"
