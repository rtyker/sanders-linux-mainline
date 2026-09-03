#!/bin/bash
# 07-boot-kernel.sh — Boot rápido do Kernel Linux Mainline via RAM (Modo Desenvolvimento)
#
# Uso:
#   ./scripts/07-boot-kernel.sh
#
# Este script carrega o lk2nd em RAM e em seguida envia o kernel mainline (boot-sanders.img)
# para inicializacao imediata (~5s), sem realizar gravacao de rootfs no eMMC.
#
# NOTA: Como o lk2nd definitivo e a partição /boot (cache extlinux) já estão gravados no eMMC,
# o aparelho dá boot autônomo sozinho. Use este script apenas para testar kernels em RAM
# sem gravar na flash (override de desenvolvimento). Para atualizar o kernel no eMMC,
# use '10-deploy-boot.sh' (via SSH ao vivo) ou '99-flash-rootfs-final.sh' (via Fastboot).

source "$(dirname "$0")/lib.sh"
check_cmd fastboot

[ -f "$OUT/lk2nd.img" ]        || die "$OUT/lk2nd.img nao existe (rode 01-build-lk2nd.sh)"
[ -f "$OUT/boot-sanders.img" ] || die "$OUT/boot-sanders.img nao existe (rode 06-build-boot.sh)"

msg "1/3 Carregando lk2nd em RAM..."
sudo fastboot boot "$OUT/lk2nd.img" || die "Falha ao carregar lk2nd"

msg "2/3 Aguardando lk2nd inicializar a interface Fastboot (2-3s)..."
READY=0
for _ in $(seq 1 15); do
    if sudo timeout 1 fastboot getvar product 2>&1 | grep -qi "product:"; then
        READY=1
        break
    fi
    sleep 1
done

[ "$READY" -eq 1 ] || warn "lk2nd nao respondeu em 15s, tentando enviar o boot mesmo assim..."

msg "3/3 Enviando boot-sanders.img para inicializacao direta do kernel..."
sudo fastboot boot "$OUT/boot-sanders.img" || die "Falha ao enviar boot-sanders.img"

msg "============================================================"
msg "🚀 OK: Boot disparado com sucesso!"
msg "O kernel Linux Mainline esta subindo na tela do aparelho."
msg "============================================================"
