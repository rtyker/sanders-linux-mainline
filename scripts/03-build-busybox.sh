#!/bin/bash
# Baixa busybox source, configura static aarch64 (com CONFIG_TC desabilitado),
# compila. Saída: $BUSYBOX_SRC/busybox

source "$(dirname "$0")/lib.sh"
check_cmd ${ARM64_TC}gcc

if [ ! -d "$BUSYBOX_SRC" ]; then
    msg "baixando busybox $BUSYBOX_VER..."
    cd "$BUILD"
    curl -fLO "https://busybox.net/downloads/busybox-$BUSYBOX_VER.tar.bz2"
    tar xjf "busybox-$BUSYBOX_VER.tar.bz2"
    rm "busybox-$BUSYBOX_VER.tar.bz2"
fi

cd "$BUSYBOX_SRC"
# So gera .config do zero se nao existir — mesmo padrao do
# 02-build-kernel.sh. "make defconfig" incondicional aqui descartava
# qualquer tuning manual feito entre builds (ex.: ajustes via
# `make menuconfig` num rebuild incremental).
[ -f .config ] || make defconfig
sed -i 's|^# CONFIG_STATIC is not set|CONFIG_STATIC=y|' .config
sed -i 's|^CONFIG_TC=y|# CONFIG_TC is not set|' .config

msg "compilando busybox static (aarch64)..."
make CROSS_COMPILE="$ARM64_TC" -j"$(nproc)"

if [ -x ./busybox ]; then
    msg "OK: $BUSYBOX_SRC/busybox ($(du -h busybox | cut -f1))"
else
    die "busybox não foi gerado"
fi
