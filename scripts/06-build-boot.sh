#!/bin/bash
# Empacota Image.gz + DTB + initramfs em Android boot.img.
# Pre-requisitos: 02 e 04 ja rodaram.
#
# Saida: $OUT/boot-sanders.img

source "$(dirname "$0")/lib.sh"
check_cmd mkbootimg

KERNEL="$LINUX_SRC/arch/arm64/boot/Image.gz"
DTB="$LINUX_SRC/arch/arm64/boot/dts/qcom/msm8953-motorola-sanders.dtb"
INITRAMFS="$OUT/initramfs.cpio.gz"

for f in "$KERNEL" "$DTB" "$INITRAMFS"; do
    [ -f "$f" ] || die "faltando: $f"
done

msg "concatenando Image.gz + DTB (appended-dtb style esperado pelo lk2nd)..."
cat "$KERNEL" "$DTB" > "$OUT/kernel-dtb"

msg "gerando boot-sanders.img..."
mkbootimg \
    --kernel "$OUT/kernel-dtb" \
    --ramdisk "$INITRAMFS" \
    --cmdline "$KERNEL_CMDLINE" \
    --base "$BOOT_BASE" \
    --kernel_offset "$BOOT_KERNEL_OFFSET" \
    --ramdisk_offset "$BOOT_RAMDISK_OFFSET" \
    --tags_offset "$BOOT_TAGS_OFFSET" \
    --pagesize "$BOOT_PAGESIZE" \
    --header_version 0 \
    -o "$OUT/boot-sanders.img"

msg "OK: $OUT/boot-sanders.img ($(du -h "$OUT/boot-sanders.img" | cut -f1))"

# ============================================================
# Boot Autônomo eMMC (extlinux na partição cache / mmcblk0p52)
# ============================================================
BOOT_STAGING="$OUT/boot-staging"
msg "preparando estrutura extlinux em $BOOT_STAGING..."
rm -rf "$BOOT_STAGING"
mkdir -p "$BOOT_STAGING/boot/extlinux"

cp "$KERNEL" "$BOOT_STAGING/boot/Image.gz"
cp "$INITRAMFS" "$BOOT_STAGING/boot/initramfs.cpio.gz"
cp "$DTB" "$BOOT_STAGING/boot/msm8953-motorola-sanders.dtb"
cp "$DTB" "$BOOT_STAGING/boot/msm8953-motorola-potter.dtb"

cat << "EOF" > "$BOOT_STAGING/boot/extlinux/extlinux.conf"
timeout 1
default arch-mainline

label arch-mainline
    linux /Image.gz
    initrd /initramfs.cpio.gz
    fdt /msm8953-motorola-sanders.dtb
    append console=tty0 console=ttyGS0 console=ttyMSM0,115200n8 earlycon ignore_loglevel printk.time=1 printk.devkmsg=on panic=0 fbcon=font:TER16x32 wcn36xx.debug_mask=0x104
EOF

# Symlinks na raiz para máxima compatibilidade com o parser do lk2nd
(
    cd "$BOOT_STAGING"
    ln -sf boot/extlinux extlinux
    ln -sf boot/Image.gz Image.gz
    ln -sf boot/initramfs.cpio.gz initramfs.cpio.gz
    ln -sf boot/msm8953-motorola-sanders.dtb msm8953-motorola-sanders.dtb
    ln -sf boot/msm8953-motorola-potter.dtb msm8953-motorola-potter.dtb
)

CACHE_IMG="$OUT/boot-cache.img"
msg "gerando imagem ext2 da partição cache ($CACHE_IMG, 256MB)..."
truncate -s 256M "$CACHE_IMG"
mkfs.ext2 -F -L boot -d "$BOOT_STAGING" "$CACHE_IMG" >/dev/null 2>&1 || {
    warn "mkfs.ext2 com -d falhou ou nao suportado, tentando sem -d..."
    mkfs.ext2 -F -L boot "$CACHE_IMG" >/dev/null 2>&1
}

msg "OK: $CACHE_IMG ($(du -h "$CACHE_IMG" | cut -f1)) [ext2 pronto para partição cache]"

