#!/bin/bash
# ARQUIVADO (2026-09-14): experimento de kernel enxuto testado e deployado
# ao vivo com sucesso no potter, mas o esforco foi abandonado a pedido do
# usuario (ganho de tamanho modesto, ~6%, nao justificou o trabalho de
# manter um segundo kernel). O aparelho foi revertido pro kernel completo.
# Mantido aqui so como referencia caso alguem queira retomar. Ver
# _tombstone/README.md.
#
# Build EXPERIMENTAL de um kernel "enxuto" (headless-only): mesma arvore/
# patches/DTS do sanders, mas com _tombstone/sanders-lean.config.fragment
# (sem display/DRM/GPU, audio, touchscreen, sensores, LED — ver esse
# arquivo pro detalhamento completo do que caiu e por que).
#
# Isolamento de proposito (ver AGENTS.md "Ephemeral Kernel Tree" e o
# pedido original deste script): NUNCA toca no kernel "completo" atual.
#   - $BUILD e forcado pra $REPO/build-lean/ (diferente do build/ da
#     02-build-kernel.sh) ANTES de dar source em lib.sh — arvore do
#     kernel, .config e saidas ficam 100% separados. Um `git clone`
#     novo aqui nao mexe em nada dentro de build/linux.
#   - $CCACHE_DIR e o MESMO de sempre (lib.sh resolve pro cache
#     compartilhado do projeto pai, cache/ccache_aarch64/) — de proposito
#     NAO sobrescrito aqui. ccache indexa pelo hash do fonte pre-processado,
#     entao a maioria dos objetos (core do kernel, drivers que nao mudam
#     entre os dois fragments) ja vem CACHE HIT do build completo que ja
#     rodou antes — so os objetos dos subsistemas removidos (display/audio/
#     touch/sensores/led) simplesmente nao sao compilados, nada e perdido
#     nem duplicado no cache.
#
# Uso:
#   ./scripts/build-kernel-lean.sh
#
# Saidas (dentro de build-lean/, nao build/):
#   build-lean/linux/arch/arm64/boot/Image.gz
#   build-lean/linux/arch/arm64/boot/dts/qcom/msm8953-motorola-sanders.dtb

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Precisa ser exportado ANTES do source em lib.sh — lib.sh calcula
# BUILD/LINUX_SRC/OUT em cima dessa variavel (com fallback pro build/
# normal se nao setada). Isolamento e so isto: nao precisa de mais nada.
export BUILD="${BUILD:-$REPO_ROOT/build-lean}"

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if [ "$BUILD" = "$REPO/build" ]; then
    die "BUILD apontando pro build/ principal — isso derrotaria o proposito deste script (nao prejudicar o kernel atual). Rode sem sobrescrever \$BUILD, ou aponte pra outro diretorio."
fi

LEAN_FRAGMENT="$REPO/_tombstone/sanders-lean.config.fragment"
[ -f "$LEAN_FRAGMENT" ] || die "fragment enxuto nao encontrado: $LEAN_FRAGMENT"

check_cmd "${ARM64_TC}gcc"
check_cmd flex
check_cmd bison
check_cmd dtc
check_cmd ccache
msg "build ISOLADO em: $BUILD"
msg "ccache (compartilhado com o build completo): CCACHE_DIR=$CCACHE_DIR"

if [ ! -d "$LINUX_SRC" ]; then
    msg "clonando Linux mainline (shallow, branch=$LINUX_BRANCH) em $LINUX_SRC..."
    git clone --depth=1 --branch="$LINUX_BRANCH" "$LINUX_REPO" "$LINUX_SRC"
fi

cd "$LINUX_SRC"

DTS_DIR="arch/arm64/boot/dts/qcom"
DTS_NAME="msm8953-motorola-sanders"

msg "copiando $DTS_NAME.dts do repo para a árvore isolada do kernel..."
cp "$REPO/dts/$DTS_NAME.dts" "$DTS_DIR/$DTS_NAME.dts"

if ! grep -q "$DTS_NAME.dtb" "$DTS_DIR/Makefile"; then
    msg "adicionando entrada no Makefile..."
    echo "dtb-\$(CONFIG_ARCH_QCOM) += $DTS_NAME.dtb" >> "$DTS_DIR/Makefile"
fi

# Mesmos patches do build completo (idempotentes, mesma logica de
# 02-build-kernel.sh) — sao todos harmless mesmo pros subsistemas que
# este fragment desativa (ex.: o patch do painel DRM so afeta um driver
# que aqui nem entra no build, porque CONFIG_DRM_MSM_* fica "not set";
# o Makefile do kernel so compila o que o Kconfig manda).
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

# Mesma logica de 02-build-kernel.sh: sempre regenera o .config do zero
# a partir do defconfig (nao reaproveita .config de build anterior) —
# ver comentario la e docs/TROUBLESHOOTING.md item 18. Aqui o motivo e
# ainda mais direto: esta arvore e exclusiva deste script, entao nao ha
# outra sessao concorrente pra gerar drift, mas manter o mesmo padrao
# custa segundos e evita qualquer duvida.
msg "make defconfig (arm64)..."
make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" defconfig

msg "aplicando config fragment ENXUTO (sanders-lean)..."
./scripts/kconfig/merge_config.sh -m .config "$LEAN_FRAGMENT"
make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" olddefconfig

msg "compilando kernel enxuto (Image.gz + dtb do sanders)..."
make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" -j"$(nproc)" \
    Image.gz "qcom/$DTS_NAME.dtb"

KERNEL="$LINUX_SRC/arch/arm64/boot/Image.gz"
DTB="$LINUX_SRC/arch/arm64/boot/dts/qcom/$DTS_NAME.dtb"
msg "OK (build enxuto, isolado em $BUILD):"
msg "  $KERNEL ($(du -h "$KERNEL" | cut -f1))"
msg "  $DTB ($(du -h "$DTB" | cut -f1))"

# Comparacao direta com o kernel completo, se ja tiver sido buildado —
# e o numero que de fato importa pra saber se valeu a pena.
FULL_KERNEL="$REPO/build/linux/arch/arm64/boot/Image.gz"
if [ -f "$FULL_KERNEL" ]; then
    msg "comparacao com o kernel completo ($FULL_KERNEL):"
    msg "  completo: $(du -h "$FULL_KERNEL" | cut -f1)"
    msg "  enxuto:   $(du -h "$KERNEL" | cut -f1)"
else
    msg "kernel completo (build/linux) ainda nao existe aqui — rode 02-build-kernel.sh se quiser comparar tamanhos lado a lado."
fi

msg "ccache stats (cache compartilhado com o build completo):"
ccache -s
