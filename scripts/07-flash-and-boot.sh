#!/bin/bash
# Carrega lk2nd e (opcional) flasha rootfs Arch + boot do kernel mainline.
#
# Uso:
#   ./07-flash-and-boot.sh           # carrega lk2nd + boota kernel (NAO flasha rootfs)
#   ./07-flash-and-boot.sh --flash   # tambem flasha userdata com rootfs Arch (APAGA /data!)

source "$(dirname "$0")/lib.sh"
check_cmd fastboot

FLASH=0
[ "${1:-}" = "--flash" ] && FLASH=1

[ -f "$OUT/lk2nd.img" ]        || die "$OUT/lk2nd.img nao existe (rode 01)"
[ -f "$OUT/boot-sanders.img" ] || die "$OUT/boot-sanders.img nao existe (rode 02-04, 06)"
if [ $FLASH -eq 1 ]; then
    [ -f "$ROOTFS_IMG" ] || die "$ROOTFS_IMG nao existe (rode 05 com FLAVOR=$FLAVOR)"
    msg "rootfs alvo: $ROOTFS_IMG (FLAVOR=$FLAVOR)"
fi

cat <<EOF
============================================================
Coloque o aparelho em modo fastboot:
  - Desligue o aparelho (segure power 10s)
  - Power + Volume DOWN ate aparecer fastboot

Pressione ENTER quando estiver pronto, ou Ctrl+C para abortar.
============================================================
EOF
read -r _

msg "carregando lk2nd..."
sudo fastboot boot "$OUT/lk2nd.img"

# Espera o lk2nd re-enumerar em fastboot (ate ~30s: 15 tentativas de
# timeout 1s + sleep 1s cada) em vez de um sleep fixo — em hosts/USB
# mais lentos, 3s fixos podem nao ser suficientes e o proximo
# "fastboot boot" chega antes do lk2nd estar pronto, falhando
# silenciosamente (fastboot so fica esperando o device).
msg "aguardando lk2nd reaparecer em fastboot..."
# IMPORTANTE: "fastboot getvar" sem device conectado fica bloqueado
# indefinidamente em "< waiting for any device >" — sem o timeout aqui
# o loop nunca de fato repete, so trava na 1a tentativa.
READY=0
for _ in $(seq 1 15); do
    sudo timeout 1 fastboot getvar product 2>&1 | grep -qi "product:" && { READY=1; break; }
    sleep 1
done
[ "$READY" -eq 1 ] || warn "lk2nd nao respondeu em 15s, tentando mesmo assim..."

if [ $FLASH -eq 1 ]; then
    cat <<EOF
============================================================
ATENCAO: o proximo passo APAGA /data do Android.
Backup tudo que importa antes (adb pull /sdcard/).
Aguarde a tela do lk2nd aparecer e pressione ENTER.
============================================================
EOF
    read -r _
    msg "flashando userdata com rootfs Arch (~3 min)..."
    sudo fastboot flash userdata "$ROOTFS_IMG"
fi

msg "bootando kernel mainline (transitorio, RAM-only)..."
sudo fastboot boot "$OUT/boot-sanders.img"

cat <<EOF

Aguarde 30-60s. Na tela do aparelho deve aparecer:
  - logs do kernel rolando (rotated portrait)
  - init.script encontrando rootfs em /dev/mmcblk0p5X
  - systemd subindo
  - "archlinuxarm login:"

(Login: root/root ou alarm/alarm; mas sem USB CDC ACM / touch / wifi
ainda nao da pra digitar — ver docs/HARDWARE_STATUS.md.)
EOF
