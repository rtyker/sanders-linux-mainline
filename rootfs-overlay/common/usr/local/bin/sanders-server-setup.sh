#!/bin/bash
# Script de Setup e Diagnóstico do Servidor Moto G5 series
# Uso: sanders-server-setup.sh [--status | --install]

set -euo pipefail

MODE="${1:---status}"

show_status() {
    echo "====================================================="
    echo "🐧 Moto G5 Series — Headless Linux Server Status"
    echo "====================================================="
    
    # 1. Arquitetura e Kernel
    echo "--- [ Kernel & OS ] ---"
    # $13 = arquitetura em "uname -a" (ex.: aarch64) pro build-string
    # deste projeto ("... Thu May 21 15:08:38 -03 2026 aarch64 GNU/Linux").
    # $12 seria o ano — nao faz sentido misturado com a arquitetura.
    uname -a | awk '{print "Kernel: " $3 " (" $13 ")"}' 2>/dev/null || uname -sr
    uptime | awk -F'up ' '{print "Uptime: " $2}' 2>/dev/null || true

    # 2. CPU & Governor
    echo -e "\n--- [ CPU & Governor ] ---"
    if [ -d /sys/devices/system/cpu/cpu0/cpufreq ]; then
        GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "N/A")
        FREQ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo "N/A")
        echo "Governor Ativo: $GOV"
        echo "Frequência cpu0: ${FREQ} kHz"
    else
        echo "Interface cpufreq: Não detectada em sysfs"
    fi

    # 3. Swap ZRAM & Memória
    echo -e "\n--- [ Memória & ZRAM ] ---"
    free -h | awk 'NR==1||NR==2||NR==3'
    if [ -f /proc/swaps ] && grep -q "/dev/zram" /proc/swaps; then
        echo "ZRAM Status: ATIVO"
        cat /proc/swaps | awk 'NR>1 {print "  " $1 " (" $3/1024 "MB usado de " $2/1024 "MB)"}'
    else
        echo "ZRAM Status: Inativo"
    fi

    # 4. Telemetria Térmica
    echo -e "\n--- [ Temperatura ] ---"
    if [ -x /usr/local/bin/sanders-thermal.sh ]; then
        # sanders-thermal.sh sai com 1/2 de proposito quando a
        # temperatura passa dos thresholds WARNING/CRITICAL. Com
        # `set -euo pipefail` isso mataria o script bem aqui, no meio
        # do relatorio, e Watchdog/Rede/Armazenamento nunca apareceriam
        # — justo no caso (esquentando) em que mais precisamos ver o
        # resto do diagnostico. `|| true` evita a propagacao.
        /usr/local/bin/sanders-thermal.sh | awk 'NR>2' || true
    else
        awk '{ print "Temp: " $1/1000 "°C" }' /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo "N/A"
    fi

    # 5. Hardware Watchdog
    echo -e "\n--- [ Hardware Watchdog ] ---"
    if [ -c /dev/watchdog0 ] || [ -c /dev/watchdog ]; then
        echo "Watchdog Device: Presente (/dev/watchdog)"
    else
        echo "Watchdog Device: Ausente"
    fi

    # 6. Conectividade de Rede
    echo -e "\n--- [ Interfaces de Rede ] ---"
    ip -brief addr 2>/dev/null | awk '{printf "  %-12s %-8s %s\n", $1, $2, $3}' || ip addr

    # 7. Armazenamento e /etc/fstab
    echo -e "\n--- [ Armazenamento ] ---"
    df -h / /tmp /mnt/microsd 2>/dev/null | awk 'NR==1||NR>1' || true

    echo "====================================================="
}

install_packages() {
    echo "[sanders-server] Instalando suíte de ferramentas de servidor..."
    if command -v pacman >/dev/null 2>&1; then
        # -Syu, nao -Sy: este script roda num sistema ja instalado, as
        # vezes muito depois do build da imagem — "-Sy" sozinho sincroniza
        # a db sem atualizar o que ja esta instalado (partial upgrade),
        # podendo puxar uma lib nova (glibc/openssl) incompativel com
        # binarios antigos do sistema. Risco real aqui, nao so no build.
        # fastfetch, nao neofetch: o pacote neofetch foi removido dos
        # repositorios oficiais do Arch (upstream descontinuado,
        # substituido pelo fork ativo fastfetch). Com "neofetch" na
        # lista, o pacman inteiro falhava em "error: target not found"
        # ANTES de instalar qualquer coisa (resolucao de dependencias
        # acontece pra todos os alvos de uma vez) — confirmado ao vivo
        # 2026-09-04.
        #
        # Sem podman (decidido 2026-09-04, docker ja cobre o caso de uso
        # de containers, nao precisa dos dois) nem tmux (decidido
        # 2026-09-04, sem necessidade real pra este uso).
        pacman -Syu --noconfirm --needed \
            htop git curl vim fastfetch docker bluez-utils \
            || echo "[sanders-server] WARN: falha instalando alguns pacotes via pacman"

        # docker fica instalado mas NUNCA habilitado por padrao — este e o
        # flavor "server" headless, nao deve subir o daemon de containers
        # sozinho no boot. `disable` aqui e so idempotencia/documentacao
        # explicita da intencao (a instalacao via pacman ja nao habilita
        # nada sozinha); use `systemctl enable --now docker` manualmente
        # quando de fato for usar.
        systemctl disable docker.service >/dev/null 2>&1 || true

    else
        echo "[sanders-server] ERRO: gerenciador de pacotes pacman não encontrado" >&2
        exit 1
    fi
}

case "$MODE" in
    --status|-s)
        show_status
        ;;
    --install|-i)
        install_packages
        show_status
        ;;
    *)
        echo "Uso: $0 [--status | --install]" >&2
        exit 1
        ;;
esac
