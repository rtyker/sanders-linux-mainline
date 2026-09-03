#!/bin/bash
# 99-flash-rootfs-final.sh — Provisionamento e Flash Definitivo do eMMC para Boot Autônomo
#
# Uso:
#   ./scripts/99-flash-rootfs-final.sh -y
#
# Grava permanentemente no eMMC do aparelho:
#   1. lk2nd.img -> partição 'boot' e 'recovery' (mmcblk0p37 / p38)
#   2. boot-cache.img -> partição 'cache' (mmcblk0p52, ext2 /boot)
#   3. rootfs-arch-*.img -> partição 'userdata' (mmcblk0p54, ext4 /)

source "$(dirname "$0")/lib.sh"
check_cmd fastboot

AUTO_YES=0
[ "${1:-}" = "-y" ] || [ "${1:-}" = "--yes" ] && AUTO_YES=1

CACHE_IMG="$OUT/boot-cache.img"

[ -f "$OUT/lk2nd.img" ] || die "$OUT/lk2nd.img nao existe (rode 01-build-lk2nd.sh)"
[ -f "$CACHE_IMG" ]     || die "$CACHE_IMG nao existe (rode 06-build-boot.sh)"
[ -f "$ROOTFS_IMG" ]    || die "$ROOTFS_IMG nao existe (FLAVOR=$FLAVOR, rode 05-build-rootfs.sh)"

if [ "$AUTO_YES" -ne 1 ] && [ -t 0 ]; then
    echo "============================================================"
    echo "⚠️  ATENÇÃO: Este script realizará o provisionamento DEFINITIVO do eMMC."
    echo "Serão gravadas as partições:"
    echo "  - boot e recovery (lk2nd bootloader permanente)"
    echo "  - cache (partição /boot ext2 com kernel mainline e extlinux)"
    echo "  - userdata (sistema raiz Arch Linux ARM64)"
    echo ""
    echo "Pressione ENTER para prosseguir ou Ctrl+C para cancelar."
    echo "============================================================"
    read -r _
fi

msg "1/5 Verificando conexão Fastboot..."
if ! sudo fastboot devices | grep -q .; then
    die "Nenhum aparelho detectado em modo Fastboot. Conecte o aparelho com Vol- e Power."
fi

# Se estiver no Fastboot da Motorola, damos boot no lk2nd em RAM primeiro para liberar escrita em 'boot'
if sudo fastboot getvar product 2>&1 | grep -qi "potter\|sanders"; then
    if ! sudo fastboot getvar lk2nd:version 2>&1 | grep -qi "lk2nd"; then
        msg "Aparelho no Fastboot stock Motorola. Inicializando lk2nd em RAM para desbloqueio de flash..."
        sudo fastboot boot "$OUT/lk2nd.img" || die "Falha ao carregar lk2nd"
        msg "Aguardando lk2nd subir em modo Fastboot (segure Vol- se necessário)..."
        for _ in $(seq 1 15); do
            if sudo timeout 1 fastboot getvar lk2nd:version 2>&1 | grep -qi "lk2nd"; then break; fi
            sleep 1
        done
    fi
fi

msg "2/5 Gravando lk2nd permanentemente em 'boot' e 'recovery'..."
sudo fastboot flash boot "$OUT/lk2nd.img" || die "Falha ao gravar partição boot"
sudo fastboot flash recovery "$OUT/lk2nd.img" || die "Falha ao gravar partição recovery"

msg "3/5 Gravando partição /boot dedicada em 'cache' (ext2 extlinux)..."
sudo fastboot flash cache "$CACHE_IMG" || die "Falha ao gravar partição cache"

msg "4/5 Gravando rootfs Arch Linux em 'userdata' (chunking de 256M)..."
sudo fastboot -S 256M flash userdata "$ROOTFS_IMG" || die "Falha no flash do rootfs"

msg "5/5 Concluído com sucesso!"
msg "============================================================"
msg "🚀 PROVISIONAMENTO DEFINITIVO COMPLETO!"
msg "O aparelho agora inicializa de forma 100% autônoma direto do eMMC."
msg "Reinicie com: sudo fastboot reboot"
msg "============================================================"

