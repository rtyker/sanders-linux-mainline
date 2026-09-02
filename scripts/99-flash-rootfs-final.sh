#!/bin/bash
# 99-flash-rootfs-final.sh — Gravação final do RootFS Arch Linux na partição eMMC (userdata)
#
# Uso:
#   ./scripts/99-flash-rootfs-final.sh -y
#
# ATENÇÃO: Este script destina-se EXCLUSIVAMENTE ao encerramento/deploy final do projeto.
# Ele grava a imagem rootfs (3GB) na partição userdata do eMMC.

source "$(dirname "$0")/lib.sh"
check_cmd fastboot

AUTO_YES=0
[ "${1:-}" = "-y" ] || [ "${1:-}" = "--yes" ] && AUTO_YES=1

[ -f "$OUT/lk2nd.img" ]        || die "$OUT/lk2nd.img nao existe"
[ -f "$ROOTFS_IMG" ]           || die "$ROOTFS_IMG nao existe (FLAVOR=$FLAVOR)"
[ -f "$OUT/boot-sanders.img" ] || die "$OUT/boot-sanders.img nao existe"

if [ "$AUTO_YES" -ne 1 ] && [ -t 0 ]; then
    echo "============================================================"
    echo "ATENCAO: O proximo passo APAGA a particao /data (userdata) do celular."
    echo "Pressione ENTER para continuar ou Ctrl+C para cancelar."
    echo "============================================================"
    read -r _
fi

msg "1/3 Carregando lk2nd..."
sudo fastboot boot "$OUT/lk2nd.img"

msg "2/3 Aguardando lk2nd em fastboot (2-3s)..."
for _ in $(seq 1 15); do
    if sudo timeout 1 fastboot getvar product 2>&1 | grep -qi "product:"; then break; fi
    sleep 1
done

msg "3/3 Flashando rootfs em userdata (chunking de 256M)..."
sudo fastboot -S 256M flash userdata "$ROOTFS_IMG" || die "Falha no flash do rootfs"

msg "OK: RootFS gravado com sucesso no eMMC."
