#!/bin/bash
# Variáveis compartilhadas. Source este arquivo nos demais scripts.

set -euo pipefail

# Raiz do repo (resolvido a partir do path desse arquivo)
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Onde os builds geram artefatos. Fica fora do repo (gitignored se dentro).
BUILD="${BUILD:-$REPO/build}"
OUT="$BUILD/out"

# Componentes
LK2ND_SRC="$BUILD/lk2nd-src"
LINUX_SRC="$BUILD/linux"
BUSYBOX_SRC="$BUILD/busybox-1.36.1"
INITRAMFS_ROOT="$BUILD/initramfs-root"
ARCH_TARBALL="$BUILD/ArchLinuxARM-aarch64-latest.tar.gz"

# Flavor do rootfs: "headless" (default, minimal — SSH/SAMBA/server) ou
# "desktop" (Weston + Xwayland + mesa). Kernel/initramfs/lk2nd sao
# identicos entre os dois; so muda o userspace no rootfs.
FLAVOR="${FLAVOR:-headless}"
case "$FLAVOR" in
    headless|desktop) ;;
    *) echo "FLAVOR invalido: $FLAVOR (use headless|desktop)" >&2; exit 1 ;;
esac
ROOTFS_IMG="$BUILD/rootfs-arch-$FLAVOR.img"

# Versões
BUSYBOX_VER="1.36.1"
LK2ND_FORK="https://github.com/playday3008/lk2nd.git"
LK2ND_COMMIT="c8b47cd"
LINUX_REPO="https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git"
# Tag estavel fixa (nao "master") — reproduzivel: um clone novo sempre
# pega o mesmo ponto exato, em vez de qualquer que seja o HEAD do
# master no dia do clone (que pode ser um -rc). Atualizado 2026-09-03
# de 7.1.0-rc4 (commit fixado do primeiro clone) pra v7.2 (ultima
# release estavel do mainline na data), a pedido do usuario pra testar
# Docker com um kernel mais recente. Pra atualizar de novo: `git fetch
# --depth=1 origin tag vX.Y && git reset --hard vX.Y` dentro de
# $LINUX_SRC (nao basta mudar essa variavel — 02-build-kernel.sh so
# clona se $LINUX_SRC nao existir ainda), depois mudar essa constante
# pra documentar a versao atual.
LINUX_BRANCH="v7.2"
ARCH_TARBALL_URL="http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"

# Cross toolchains
ARM32_TC="arm-none-eabi-"
ARM64_TC="aarch64-linux-gnu-"

# ccache: a arvore do kernel e clonada fresca (git clone --depth=1) toda
# vez que $BUILD/linux nao existe (rebuild limpo) — sem ccache isso
# recompila TUDO do zero, porque o incremental do make e baseado em
# mtime/arvore de objetos que nao sobrevive a um clone novo. ccache
# cacheia pelo hash do fonte pre-processado, entao sobrevive a clones
# novos e a "make clean". Aponta por padrao pro diretorio de cache ja
# provisionado no projeto pai (cache_ccache_aarch64/, max_size=5G
# configurado la); ajustavel via CCACHE_DIR se este submodulo for
# clonado isolado em outro lugar (sem esse diretorio sibling).
CCACHE_DIR="${CCACHE_DIR:-$REPO/../cache_ccache_aarch64}"
export CCACHE_DIR
mkdir -p "$CCACHE_DIR"

# Prefixo de CROSS_COMPILE com ccache. O truque de por "ccache " na
# frente (com espaco) funciona porque o Makefile do kernel invoca
# "$(CROSS_COMPILE)gcc" — o shell que roda a receita faz word-splitting
# nisso, entao "ccache aarch64-linux-gnu-gcc ..." vira dois argumentos
# (comando ccache + o gcc de verdade), nao um binario literal chamado
# "ccache aarch64-linux-gnu-gcc". Padrao usado por builds de kernel em
# geral pra isso. Use $ARM64_CC no lugar de "$ARM64_TC" em CROSS_COMPILE=
# nas chamadas de make dos scripts de build.
ARM64_CC="ccache $ARM64_TC"

# Offsets Android boot.img para msm8953 Motorola
BOOT_BASE="0x80000000"
BOOT_KERNEL_OFFSET="0x00008000"
BOOT_RAMDISK_OFFSET="0x01000000"
BOOT_TAGS_OFFSET="0x00000100"
BOOT_PAGESIZE="2048"

# Cmdline
KERNEL_CMDLINE="console=tty0 console=ttyGS0 console=ttyMSM0,115200n8 earlycon ignore_loglevel printk.time=1 printk.devkmsg=on panic=30 fbcon=font:TER16x32 wcn36xx.debug_mask=0x104"

mkdir -p "$BUILD" "$OUT"

msg()  { echo -e "\033[1;36m[$(basename "${BASH_SOURCE[1]:-$0}")]\033[0m $*"; }
warn() { echo -e "\033[1;33m[WARN]\033[0m $*" >&2; }
die()  { echo -e "\033[1;31m[ERRO]\033[0m $*" >&2; exit 1; }

check_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "comando '$1' não encontrado. Veja scripts/00-setup-host.sh"
}
