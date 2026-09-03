#!/bin/bash
# 10-deploy-boot.sh — Atualização ao vivo da partição /boot (eMMC cache) via SSH
#
# Uso:
#   ./scripts/10-deploy-boot.sh [--reboot] [IP]
#
# Exemplo:
#   ./scripts/10-deploy-boot.sh --reboot
#   ./scripts/10-deploy-boot.sh 10.42.0.2
#
# Envia os artefatos de boot atualizados (Image.gz, DTB, initramfs, extlinux.conf)
# para o aparelho via rede SSH e grava na partição ext2 cache (/dev/mmcblk0p52).

source "$(dirname "$0")/lib.sh"

DO_REBOOT=0
TARGET_IP="10.42.0.2"

for arg in "$@"; do
    case "$arg" in
        --reboot|-r)
            DO_REBOOT=1
            ;;
        *)
            TARGET_IP="$arg"
            ;;
    esac
done

BOOT_STAGING="$OUT/boot-staging"

if [ ! -d "$BOOT_STAGING" ]; then
    msg "Diretório de staging $BOOT_STAGING não encontrado. Rodando 06-build-boot.sh..."
    "$REPO/scripts/06-build-boot.sh"
fi

msg "1/4 Testando conexão SSH com $TARGET_IP..."
if ! ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o ConnectTimeout=5 "root@$TARGET_IP" "uname -a" >/dev/null 2>&1; then
    die "Não foi possível conectar via SSH em root@$TARGET_IP. Certifique-se de que o aparelho está online (08-host-net.sh)."
fi

msg "2/4 Copiando arquivos de boot para o dispositivo via SCP..."
ssh -o StrictHostKeyChecking=no "root@$TARGET_IP" "mkdir -p /tmp/boot-staging"
scp -o StrictHostKeyChecking=no -r "$BOOT_STAGING"/* "root@$TARGET_IP:/tmp/boot-staging/"

msg "3/4 Gravando arquivos na partição de boot eMMC (/dev/mmcblk0p52, ext2)..."
ssh -o StrictHostKeyChecking=no "root@$TARGET_IP" '
    set -e
    MNT="/mnt_boot_tmp"
    mkdir -p "$MNT"
    # Idempotencia: se uma execucao anterior falhou entre o mount e o
    # umount (ex.: conexao SSH caiu no meio), o device fica montado e o
    # proximo "mount" aqui falharia com "already mounted". Desmonta
    # primeiro se for o caso, para o script sempre poder ser reexecutado
    # com seguranca sem intervencao manual.
    mountpoint -q "$MNT" && { umount -f "$MNT" 2>/dev/null || umount -l "$MNT"; }
    mount /dev/mmcblk0p52 "$MNT"
    
    mkdir -p "$MNT/boot/extlinux"
    cp -r /tmp/boot-staging/boot/* "$MNT/boot/"
    (
        cd "$MNT"
        ln -sf boot/extlinux extlinux
        ln -sf boot/Image.gz Image.gz
        ln -sf boot/initramfs.cpio.gz initramfs.cpio.gz
        ln -sf boot/msm8953-motorola-sanders.dtb msm8953-motorola-sanders.dtb
        ln -sf boot/msm8953-motorola-potter.dtb msm8953-motorola-potter.dtb
    )
    cd /
    sync
    umount -f "$MNT" || umount -l "$MNT"
    rm -rf "$MNT" /tmp/boot-staging
    sync
'

msg "OK: Partição /boot eMMC atualizada com sucesso!"

if [ "$DO_REBOOT" -eq 1 ]; then
    msg "4/4 Reiniciando o dispositivo..."
    ssh -o StrictHostKeyChecking=no "root@$TARGET_IP" "reboot" || true
    msg "Reboot disparado! O aparelho inicializará o novo kernel de forma autônoma."
fi
