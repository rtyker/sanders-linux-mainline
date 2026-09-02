#!/bin/bash
# 07-flash-and-boot.sh — Carrega lk2nd e (opcional) flasha rootfs Arch + boot do kernel mainline.
#
# Uso:
#   ./07-flash-and-boot.sh                     # Carrega lk2nd + boota kernel em RAM (modo transitorio)
#   ./07-flash-and-boot.sh --flash             # Flasha userdata com rootfs Arch + boota em RAM
#   ./07-flash-and-boot.sh --flash-boot        # Flasha partição boot de forma permanente
#   ./07-flash-and-boot.sh --flash -y          # Flasha de forma automatica sem prompts (modo non-interactive)

source "$(dirname "$0")/lib.sh"
check_cmd fastboot

FLASH_ROOTFS=0
FLASH_BOOT=0
AUTO_YES=0

for arg in "$@"; do
    case "$arg" in
        --flash)       FLASH_ROOTFS=1 ;;
        --flash-boot)  FLASH_BOOT=1 ;;
        -y|--yes)      AUTO_YES=1 ;;
        *) warn "Parametro desconhecido: $arg" ;;
    esac
done

[ -f "$OUT/lk2nd.img" ]        || die "$OUT/lk2nd.img nao existe (rode 01-build-lk2nd.sh)"
[ -f "$OUT/boot-sanders.img" ] || die "$OUT/boot-sanders.img nao existe (rode 06-build-boot.sh)"

if [ "$FLASH_ROOTFS" -eq 1 ]; then
    [ -f "$ROOTFS_IMG" ] || die "$ROOTFS_IMG nao existe (rode 05-build-rootfs.sh com FLAVOR=$FLAVOR)"
    msg "Rootfs alvo: $ROOTFS_IMG (FLAVOR=$FLAVOR)"
fi

confirm() {
    local prompt_msg="$1"
    if [ "$AUTO_YES" -eq 1 ] || [ ! -t 0 ]; then
        msg "$prompt_msg (Auto-confirmado)"
        return 0
    fi
    echo ""
    echo "============================================================"
    echo "$prompt_msg"
    echo "Pressione ENTER para continuar, ou Ctrl+C para abortar."
    echo "============================================================"
    read -r _
}

confirm "Verifique se o aparelho esta em modo Fastboot."

msg "Carregando lk2nd..."
sudo fastboot boot "$OUT/lk2nd.img"

msg "Aguardando lk2nd reaparecer em fastboot..."
READY=0
for _ in $(seq 1 15); do
    if sudo timeout 1 fastboot getvar product 2>&1 | grep -qi "product:"; then
        READY=1
        break
    fi
    sleep 1
done

[ "$READY" -eq 1 ] || warn "lk2nd nao respondeu em 15s, tentando mesmo assim..."

if [ "$FLASH_ROOTFS" -eq 1 ]; then
    confirm "ATENCAO: O proximo passo apagara a particao /data (userdata) do aparelho com o rootfs Arch Linux."
    msg "Flashando userdata com rootfs Arch (~2-3 min)..."
    sudo fastboot -S 256M flash userdata "$ROOTFS_IMG" || die "Falha ao flashar userdata"
fi

if [ "$FLASH_BOOT" -eq 1 ]; then
    msg "Flashando particao boot de forma permanente..."
    sudo fastboot flash boot "$OUT/boot-sanders.img" || die "Falha ao flashar boot"
    msg "Boot permanente instalado! O aparelho iniciara o Arch Linux autonomamente ao ligar."
else
    msg "Bootando kernel mainline em RAM (transitorio)..."
    sudo fastboot boot "$OUT/boot-sanders.img" || die "Falha ao bootar kernel"
fi

msg "OK: Processo finalizado."
