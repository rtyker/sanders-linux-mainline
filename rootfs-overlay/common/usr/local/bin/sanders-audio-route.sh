#!/bin/bash
# Aplica a rota DAPM que liga o audio ate o alto-falante do Moto G5s
# Plus (potter). O PCM abrir e tocar (RUNNING, sem erro) nao energiza
# sozinho o caminho analogico — faltam controles que o UCM2 normalmente
# aplicaria, mas que nao sao carregados automaticamente com hw:0,0
# direto. Ver docs/archived/AUDIO_RESOLVIDO_SOM_AUDIVEL.md no repo pai
# para a investigacao completa.
#
# Idempotente: pode rodar quantas vezes quiser, inclusive antes do card
# de som terminar de registrar (sai limpo, sem falhar o boot).

set -euo pipefail

CARD=$(awk '/motorolapotter/ {print $1; exit}' /proc/asound/cards 2>/dev/null || true)

if [ -z "$CARD" ]; then
    echo "[audio-route] Card 'motorolapotter' nao encontrado, pulando." >&2
    exit 0
fi

set_ctl() {
    local name="$1" value="$2"
    if amixer -c "$CARD" cset name="$name" "$value" >/dev/null 2>&1; then
        echo "[audio-route] '$name' = $value"
    else
        echo "[audio-route] WARN: control '$name' nao encontrado, ignorando." >&2
    fi
}

# Rota front-end -> back-end no DSP (sem isso o PCM open retorna EINVAL)
set_ctl "PRI_MI2S_RX Audio Mixer MultiMedia1" 1

# Rota fisica ate o alto-falante (equivalente ao SectionDevice."Speaker"
# do UCM2 em Motorola/potter/HiFi.conf)
set_ctl "RX3 MIX1 INP1" "RX1"
set_ctl "SPK DAC Switch" 1

# Nome enganoso: 'Mute Switch' = on significa MUDO ATIVO, nao "unmute
# habilitado". O default do driver ja vem em 'on' (mudo).
set_ctl "RX1 Mute Switch" 0
set_ctl "RX2 Mute Switch" 0
set_ctl "RX3 Mute Switch" 0

echo "[audio-route] Rota do alto-falante aplicada."
