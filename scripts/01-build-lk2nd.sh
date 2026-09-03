#!/bin/bash
# Clona e compila o lk2nd a partir do fork playday3008.
# Saida: $OUT/lk2nd.img

source "$(dirname "$0")/lib.sh"
check_cmd ${ARM32_TC}gcc

if [ ! -d "$LK2ND_SRC" ]; then
    msg "clonando lk2nd fork playday3008..."
    git clone "$LK2ND_FORK" "$LK2ND_SRC"
fi

cd "$LK2ND_SRC"
git checkout "$LK2ND_COMMIT" 2>/dev/null || warn "commit $LK2ND_COMMIT não disponível, usando HEAD"

if [ -d "$REPO/lk2nd-patches" ]; then
    for p in "$REPO"/lk2nd-patches/*.patch; do
        [ -f "$p" ] || continue
        if git apply --check --reverse "$p" >/dev/null 2>&1; then
            msg "patch $(basename "$p") já aplicado, pulando..."
        else
            msg "aplicando patch $(basename "$p")..."
            git apply "$p"
        fi
    done
fi

msg "compilando lk2nd-msm8953..."
make TOOLCHAIN_PREFIX="$ARM32_TC" lk2nd-msm8953 -j"$(nproc)"

cp build-lk2nd-msm8953/lk2nd.img "$OUT/lk2nd.img"
msg "OK: $OUT/lk2nd.img ($(du -h "$OUT/lk2nd.img" | cut -f1))"
