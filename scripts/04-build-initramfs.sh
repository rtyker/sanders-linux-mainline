#!/bin/bash
# Monta a arvore do initramfs:
#   - busybox + symlinks de /bin e /sbin
#   - script /init
#   - mountpoints virtuais
# Compacta em cpio.gz.
#
# Saida: $OUT/initramfs.cpio.gz

source "$(dirname "$0")/lib.sh"
check_cmd cpio
check_cmd gzip

BB="$BUSYBOX_SRC/busybox"
[ -x "$BB" ] || die "busybox nao compilado. Rode 03-build-busybox.sh primeiro."

msg "limpando $INITRAMFS_ROOT..."
rm -rf "$INITRAMFS_ROOT"
mkdir -p "$INITRAMFS_ROOT"/{bin,sbin,etc,proc,sys,dev,new_root,run,tmp,mnt}
mkdir -p "$INITRAMFS_ROOT/sys/kernel/config"

msg "copiando busybox e criando symlinks..."
cp "$BB" "$INITRAMFS_ROOT/bin/busybox"
chmod +x "$INITRAMFS_ROOT/bin/busybox"

while read -r app; do
    app="${app%$'\r'}"
    [ -z "$app" ] && continue
    ln -sf busybox "$INITRAMFS_ROOT/bin/$app"
done < "$REPO/initramfs/busybox-symlinks-bin.txt"

while read -r app; do
    app="${app%$'\r'}"
    [ -z "$app" ] && continue
    ln -sf ../bin/busybox "$INITRAMFS_ROOT/sbin/$app"
done < "$REPO/initramfs/busybox-symlinks-sbin.txt"

msg "instalando /init..."
cp "$REPO/initramfs/init" "$INITRAMFS_ROOT/init"
chmod +x "$INITRAMFS_ROOT/init"

# Firmware embutido no initramfs. Necessario porque drivers builtin
# (wcnss-pil etc) chamam request_firmware na init dos drivers, MUITO
# antes do switch_root para o rootfs. Sem isso, "wcnss.mdt failed: -2".
if [ -d "$REPO/firmware" ] && ls "$REPO/firmware"/*.* >/dev/null 2>&1; then
    msg "incorporando firmware no initramfs..."
    mkdir -p "$INITRAMFS_ROOT/lib/firmware/wlan/prima"
    cp "$REPO/firmware"/wcnss.* "$INITRAMFS_ROOT/lib/firmware/" 2>/dev/null || true
    if [ -f "$REPO/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin" ]; then
        cp "$REPO/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin" \
            "$INITRAMFS_ROOT/lib/firmware/wlan/prima/"
    fi
fi

msg "compactando em $OUT/initramfs.cpio.gz..."
# cpio -o imprime a linha "N blocks" informativa no stderr — nao suprime
# tudo (2>/dev/null escondia isso E erros reais tipo disco cheio ou
# permissao, deixando um initramfs corrompido passar sem aviso nenhum).
CPIO_LOG="$(mktemp)"
trap 'rm -f "$CPIO_LOG"' EXIT
if ! (cd "$INITRAMFS_ROOT" && find . | cpio -o -H newc) 2>"$CPIO_LOG" \
    | gzip -9 > "$OUT/initramfs.cpio.gz"; then
    cat "$CPIO_LOG" >&2
    die "cpio falhou ao gerar o initramfs"
fi
grep -v "^[0-9]* blocks$" "$CPIO_LOG" >&2 || true
msg "OK: $OUT/initramfs.cpio.gz ($(du -h "$OUT/initramfs.cpio.gz" | cut -f1))"
