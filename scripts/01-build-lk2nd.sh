#!/bin/bash
# Clona e compila o lk2nd a partir do fork playday3008.
# Saida: $OUT/lk2nd.img

source "$(dirname "$0")/lib.sh"
check_cmd ${ARM32_TC}gcc

if [ ! -d "$LK2ND_SRC" ]; then
    # Tenta shallow fetch so do commit fixo (LK2ND_COMMIT) — GitHub
    # suporta fetch por SHA completo/curto (uploadpack.allowReachableSHA1InWant),
    # entao isso evita baixar o historico inteiro do fork na maioria dos
    # casos. Se o host remoto nao suportar fetch por SHA, cai pro clone
    # completo de antes (mesmo comportamento, so como fallback).
    msg "clonando lk2nd fork playday3008 (shallow, commit $LK2ND_COMMIT)..."
    mkdir -p "$LK2ND_SRC"
    git -C "$LK2ND_SRC" init -q
    git -C "$LK2ND_SRC" remote add origin "$LK2ND_FORK"
    if git -C "$LK2ND_SRC" fetch --depth 1 origin "$LK2ND_COMMIT" 2>/dev/null; then
        git -C "$LK2ND_SRC" checkout -q FETCH_HEAD
    else
        warn "fetch shallow do commit $LK2ND_COMMIT falhou (host nao suporta fetch por SHA) — fazendo clone completo..."
        rm -rf "$LK2ND_SRC"
        git clone "$LK2ND_FORK" "$LK2ND_SRC"
        git -C "$LK2ND_SRC" checkout "$LK2ND_COMMIT" 2>/dev/null \
            || warn "commit $LK2ND_COMMIT não disponível, usando HEAD"
    fi
fi

cd "$LK2ND_SRC"

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
