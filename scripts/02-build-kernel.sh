#!/bin/bash
# Clona Linux mainline (se necessário), aplica DTS sanders + Makefile entry,
# aplica config fragment, compila Image.gz + DTB.
#
# Saídas:
#   $LINUX_SRC/arch/arm64/boot/Image.gz
#   $LINUX_SRC/arch/arm64/boot/dts/qcom/msm8953-motorola-sanders.dtb

source "$(dirname "$0")/lib.sh"
check_cmd ${ARM64_TC}gcc
check_cmd flex
check_cmd bison
check_cmd dtc
check_cmd ccache
msg "ccache: CCACHE_DIR=$CCACHE_DIR"

if [ ! -d "$LINUX_SRC" ]; then
    msg "clonando Linux mainline (shallow, branch=$LINUX_BRANCH)..."
    git clone --depth=1 --branch="$LINUX_BRANCH" "$LINUX_REPO" "$LINUX_SRC"
fi

cd "$LINUX_SRC"

DTS_DIR="arch/arm64/boot/dts/qcom"
DTS_NAME="msm8953-motorola-sanders"

msg "copiando $DTS_NAME.dts do repo para a árvore do kernel..."
cp "$REPO/dts/$DTS_NAME.dts" "$DTS_DIR/$DTS_NAME.dts"

if ! grep -q "$DTS_NAME.dtb" "$DTS_DIR/Makefile"; then
    msg "adicionando entrada no Makefile..."
    echo "dtb-\$(CONFIG_ARCH_QCOM) += $DTS_NAME.dtb" >> "$DTS_DIR/Makefile"
fi

# Aplica patches do sanders (touchscreen FT5436, etc.) — idempotente.
for p in "$REPO"/kernel/*.patch; do
    [ -f "$p" ] || continue
    msg "checando patch $(basename "$p")..."
    if ! git -C "$LINUX_SRC" apply --check --reverse "$p" 2>/dev/null; then
        git -C "$LINUX_SRC" apply "$p" || die "falha aplicando $p"
        msg "  aplicado."
    else
        msg "  ja aplicado, pulando."
    fi
done

if [ ! -f .config ]; then
    msg "make defconfig (arm64)..."
    make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" defconfig
fi

msg "aplicando config fragment do sanders..."
./scripts/kconfig/merge_config.sh -m .config "$REPO/kernel/sanders.config.fragment"
make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" olddefconfig

msg "compilando kernel (Image.gz + dtbs)..."
make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" -j"$(nproc)" Image.gz dtbs

KERNEL="$LINUX_SRC/arch/arm64/boot/Image.gz"
DTB="$LINUX_SRC/arch/arm64/boot/dts/qcom/$DTS_NAME.dtb"
msg "OK:"
msg "  $KERNEL ($(du -h "$KERNEL" | cut -f1))"
msg "  $DTB ($(du -h "$DTB" | cut -f1))"
msg "ccache stats:"
ccache -s
