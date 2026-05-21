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
LINUX_BRANCH="master"
ARCH_TARBALL_URL="http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"

# Cross toolchains
ARM32_TC="arm-none-eabi-"
ARM64_TC="aarch64-linux-gnu-"

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
