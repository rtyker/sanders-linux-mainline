#!/bin/bash
# Extrai firmware proprietario do Android stock pra o port mainline.
# Roda esse script depois de ter o stock zip do sanders em mao.
#
# Inputs:
#   $STOCK_ZIP — caminho pro zip de retail flashfile (NON-HLOS.bin + fsg.mbn)
#   (default: $REPO/../stock/SANDERS_RETAIL_*.zip, relativo ao submodulo)
#
# Output: $REPO/firmware/ com:
#   - wcnss.mdt + wcnss.bXX  (pronto firmware p/ remoteproc)
#   - wlan/prima/WCNSS_qcom_wlan_nv.bin  (cal data — se conseguir extrair;
#     senao precisa pegar da particao /persist do device, ver docs)
#
# Sem firmware o pronto nao boota e o wcn36xx nao traz a iface up.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

STOCK_ZIP="${STOCK_ZIP:-}"
# Fallback relativo a $REPO (diretorio pai do submodulo), nao mais um path
# absoluto fixo da maquina do dev original — funciona em qualquer clone
# do projeto que mantenha essa mesma estrutura de diretorios pai/submodulo.
STOCK_DIR_DEFAULT="$REPO/../stock"
if [ -z "$STOCK_ZIP" ]; then
    STOCK_ZIP=$(ls "$STOCK_DIR_DEFAULT"/SANDERS_RETAIL_*.zip 2>/dev/null | head -1 || true)
fi
[ -f "$STOCK_ZIP" ] || die "stock zip nao encontrado. Defina STOCK_ZIP=... ou ponha em $STOCK_DIR_DEFAULT/"

check_cmd unzip
check_cmd simg2img
check_cmd debugfs

FW_OUT="$REPO/firmware"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

msg "extraindo NON-HLOS.bin de $STOCK_ZIP..."
unzip -o "$STOCK_ZIP" NON-HLOS.bin -d "$TMP" >/dev/null
simg2img "$TMP/NON-HLOS.bin" "$TMP/NON-HLOS.raw" >/dev/null

mkdir -p "$FW_OUT"

msg "extraindo wcnss pronto firmware..."
for f in wcnss.mdt wcnss.b00 wcnss.b01 wcnss.b02 wcnss.b04 wcnss.b06 \
         wcnss.b09 wcnss.b10 wcnss.b11 wcnss.b12; do
    debugfs -R "dump image/$f $FW_OUT/$f" "$TMP/NON-HLOS.raw" 2>/dev/null
    [ -f "$FW_OUT/$f" ] && [ -s "$FW_OUT/$f" ] || warn "falhou: $f"
done

# WCNSS_qcom_wlan_nv.bin (NV cal data) NAO esta no flashfile.zip do
# sanders e tambem NAO esta em /persist do device (vimos vazio).
# No sanders fica na particao "vendor" (mmcblk0p51), em
# /firmware/wlan/prima/WCNSS_cfg.dat (Motorola renomeou de
# WCNSS_qcom_wlan_nv.bin para WCNSS_cfg.dat).
#
# Como obter (com o Linux mainline ja bootado e console serial via picocom):
#   mount -o ro /dev/mmcblk0p51 /mnt        # particao "vendor"
#   base64 -w 0 /mnt/firmware/wlan/prima/WCNSS_cfg.dat
#   # cole no host, decodifique, salve em firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin
mkdir -p "$FW_OUT/wlan/prima"
if [ ! -f "$FW_OUT/wlan/prima/WCNSS_qcom_wlan_nv.bin" ]; then
    warn "WCNSS_qcom_wlan_nv.bin nao encontrado em $FW_OUT/wlan/prima/"
    warn "Esse arquivo vive em /vendor (mmcblk0p51) como WCNSS_cfg.dat."
    warn "Boota o Linux mainline e copia via serial. Sem ele Wi-Fi nao sobe."
fi

msg "OK. firmware em $FW_OUT/"
ls -la "$FW_OUT/"
