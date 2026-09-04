#!/usr/bin/env python3
"""Associa os botoes fisicos de Volume Up/Down (evdev) ao volume real do
PipeWire via `wpctl`. So faz sentido em flavors com sessao grafica de
verdade (xfce/xorg-minimal/wayland-minimal) — em flavors headless/server
nao ha PipeWire rodando (nem sentido em ter volume de saida de audio),
por isso este script so e instalado/habilitado por
sanders-flavor-install.sh, nunca no build base.

Os dispositivos de input sao resolvidos pelo NOME em
/proc/bus/input/devices (nao pelo numero de /dev/input/eventN, que pode
mudar de boot pra boot dependendo da ordem de probe):
  - "pm8941_resin"  -> KEY_VOLUMEDOWN (code 114)
  - "gpio-keys"     -> KEY_VOLUMEUP   (code 115)

wpctl roda dentro da sessao do usuario dono do PipeWire (nao root — este
script roda como root via systemd system service pra poder ler
/dev/input/eventN, mas invoca wpctl via `runuser` na sessao do usuario
configurado em PIPEWIRE_USER).
"""
import os
import re
import select
import struct
import subprocess
import sys
import time

PIPEWIRE_USER = os.environ.get("SANDERS_PIPEWIRE_USER", "alarm")
STEP = os.environ.get("SANDERS_VOLUME_STEP", "5%")

# struct input_event no Linux 64-bit: timeval{tv_sec, tv_usec} como
# long (8 bytes cada nesta arch) + type/code (u16) + value (s32) =
# 24 bytes. Usa "qq" (int64 explicito) em vez de "ll" nativo do struct
# pra nao depender da definicao de "long" da plataforma que roda este
# script (pode nao bater com a do kernel-alvo em outra arch).
EVENT_FORMAT = "qqHHi"
EVENT_SIZE = struct.calcsize(EVENT_FORMAT)

EV_KEY = 1
KEY_VOLUMEDOWN = 114
KEY_VOLUMEUP = 115

DEVICES_BY_NAME = {
    "pm8941_resin": KEY_VOLUMEDOWN,
    "gpio-keys": KEY_VOLUMEUP,
}


def find_event_nodes():
    """Mapeia code (114/115) -> caminho /dev/input/eventN atual, lendo
    /proc/bus/input/devices. Retorna dict vazio se nenhum for encontrado
    ainda (aparelho pode estar cedo no boot)."""
    found = {}
    try:
        text = open("/proc/bus/input/devices").read()
    except OSError:
        return found
    for block in text.split("\n\n"):
        name_match = re.search(r'^N: Name="([^"]+)"', block, re.M)
        handlers_match = re.search(r"^H: Handlers=(.+)$", block, re.M)
        if not name_match or not handlers_match:
            continue
        name = name_match.group(1)
        if name not in DEVICES_BY_NAME:
            continue
        ev_match = re.search(r"event(\d+)", handlers_match.group(1))
        if not ev_match:
            continue
        found[DEVICES_BY_NAME[name]] = f"/dev/input/event{ev_match.group(1)}"
    return found


def wpctl_volume(direction):
    sign = "+" if direction == "up" else "-"
    cmd = [
        "runuser", "-u", PIPEWIRE_USER, "--",
        "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{STEP}{sign}",
    ]
    print(f"[sanders-volume-keys] tecla {direction} -> {' '.join(cmd)}", file=sys.stderr, flush=True)
    try:
        result = subprocess.run(cmd, check=False, timeout=3, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"[sanders-volume-keys] wpctl saiu com {result.returncode}: {result.stderr.strip()}", file=sys.stderr, flush=True)
    except (OSError, subprocess.TimeoutExpired) as e:
        print(f"[sanders-volume-keys] wpctl falhou: {e}", file=sys.stderr, flush=True)


def main():
    nodes = {}
    while not nodes:
        nodes = find_event_nodes()
        if not nodes:
            time.sleep(2)
    print(f"[sanders-volume-keys] usando devices: {nodes}", file=sys.stderr)

    fds = {}
    for code, path in nodes.items():
        try:
            fds[os.open(path, os.O_RDONLY)] = code
        except OSError as e:
            print(f"[sanders-volume-keys] falha abrindo {path}: {e}", file=sys.stderr)

    if not fds:
        print("[sanders-volume-keys] nenhum device aberto, saindo", file=sys.stderr)
        sys.exit(1)

    while True:
        ready, _, _ = select.select(list(fds.keys()), [], [])
        for fd in ready:
            data = os.read(fd, EVENT_SIZE)
            if len(data) < EVENT_SIZE:
                continue
            _, _, ev_type, code, value = struct.unpack(EVENT_FORMAT, data)
            if ev_type != EV_KEY or value != 1:
                continue  # so reage ao press (value=1), ignora release/repeat
            if code == KEY_VOLUMEDOWN:
                wpctl_volume("down")
            elif code == KEY_VOLUMEUP:
                wpctl_volume("up")


if __name__ == "__main__":
    main()
