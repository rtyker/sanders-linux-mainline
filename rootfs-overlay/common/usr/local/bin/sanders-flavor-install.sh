#!/bin/bash
# Ponto UNICO de entrada pos-deploy pra escolher o "flavor" do sistema —
# roda num sistema JA instalado e rodando (nao em build time; o
# FLAVOR=headless|desktop do 05-build-rootfs.sh so decide o que vai
# embutido na imagem inicial, hoje so wifi/ssh/samba/bluez/alsa base).
# Cada flavor e um set de pacotes + config + servicos que o usuario liga
# sob demanda, depois que a base (kernel, SSH, rede) ja esta pronta.
#
# Uso:
#   sanders-flavor-install.sh list              # lista flavors disponiveis
#   sanders-flavor-install.sh <flavor>           # instala e habilita
#   sanders-flavor-install.sh <flavor> --remove  # desabilita (nao desinstala pacotes)
#
# Flavors disponiveis: minimal, server, xorg, weston-minimal, xfce
#
# Reorganizado em 2026-09-04 (antes: "server" vivia so em
# sanders-server-setup.sh, "minimal" nao existia como opcao explicita, e
# os flavors graficos se chamavam xorg-minimal/wayland-minimal). Nomes
# antigos (xorg-minimal, wayland-minimal) continuam aceitos como alias
# com aviso de depreciacao — nao quebra scripts/memoria de quem já usava.

set -euo pipefail

FLAVOR="${1:-}"
ACTION="${2:---install}"

# Nomes antigos (pre-reorganizacao) -> nomes atuais. Mantido pra nao
# quebrar quem digitar de cabeca o nome antigo ou tiver isso documentado
# em algum lugar externo a este repo.
case "$FLAVOR" in
    xorg-minimal)
        echo "[aviso] 'xorg-minimal' foi renomeado para 'xorg' — use o novo nome a partir de agora." >&2
        FLAVOR=xorg
        ;;
    wayland-minimal)
        echo "[aviso] 'wayland-minimal' foi renomeado para 'weston-minimal' — use o novo nome a partir de agora." >&2
        FLAVOR=weston-minimal
        ;;
esac

# Painel DSI e fisicamente portrait (1080x1920, connector "DSI-1" — via
# DRM_MSM, confirmado ao vivo 2026-09-03 com `xrandr --query`). Usado
# pelos flavors X11 (xrandr) e Wayland (weston.ini transform=).
DSI_CONNECTOR="DSI-1"

list_flavors() {
    echo "Flavors disponiveis:"
    echo "  minimal          — apenas diagnostico, nenhum pacote instalado (roda sanders-server-setup.sh --status)"
    echo "  server           — ferramentas basicas de servidor: htop, git, curl, vim, fastfetch, docker (instalado mas NUNCA habilitado por padrao), bluez-utils"
    echo "  xorg             — so Xorg + xterm, sem desktop, pra rodar seu proprio app (tty1) + x11vnc (:5900) + teclas de Volume -> PipeWire [FALLBACK — Xorg trava o painel DSI em alguns casos, prefira weston-minimal]"
    echo "  weston-minimal   — Weston (compositor Wayland minimo, sem shell/painel extra) com GPU real (freedreno), backend VNC nativo (:5900) + teclas de Volume -> PipeWire [PREFERENCIAL]"
    echo "  xfce             — Xorg + XFCE4 (desktop leve via X11, tty1) + x11vnc (:5900) + teclas de Volume -> PipeWire"
}

pacman_install() {
    # -Syu, nao -Sy: evita partial upgrade (mesma razao do
    # sanders-server-setup.sh — puxar lib nova via -Sy sozinho pode
    # quebrar binarios ja instalados no sistema).
    pacman -Syu --noconfirm --needed "$@"
}

# --- Helper compartilhado: x11vnc (usado por xfce e xorg) ----------
#
# Generico de proposito — nao tem Requires= fixo numa unit de flavor
# especifica. Assim funciona com qualquer sessao X11 que suba em :0,
# nao importa qual flavor (xfce ou xorg) esta ativo no momento;
# so fica tentando reconectar (Restart=on-failure) ate a sessao aparecer.
setup_x11vnc_service() {
    local tag="$1"

    pacman_install x11vnc

    echo "[$tag] Configurando senha do VNC (x11vnc)..."
    mkdir -p /root/.vnc
    if [ ! -f /root/.vnc/passwd ]; then
        # Senha aleatoria gerada na primeira instalacao — x11vnc exige
        # arquivo de senha pra nao expor a sessao root sem autenticacao
        # nenhuma na rede. Reexecucoes preservam a senha ja gerada
        # (idempotente) — compartilhada entre flavors X11.
        VNC_PASS="$(head -c 12 /dev/urandom | base64 | tr -dc 'a-zA-Z0-9' | head -c 12)"
        x11vnc -storepasswd "$VNC_PASS" /root/.vnc/passwd >/dev/null
        echo "[$tag] Senha VNC gerada: $VNC_PASS  (salva em /root/.vnc/passwd, guarde/anote agora)"
    else
        echo "[$tag] Senha VNC ja configurada em /root/.vnc/passwd (nao alterada)."
    fi

    echo "[$tag] Instalando unit systemd do x11vnc (porta 5900, compartilha a sessao :0)..."
    cat > /etc/systemd/system/sanders-x11vnc.service <<'EOF'
[Unit]
Description=x11vnc — acesso remoto VNC pra sessao X11 (sanders flavor)
After=graphical.target

[Service]
User=root
# XAUTHORITY fixo (nao o -auth aleatorio do startx) — os flavors X11
# (xfce, xorg) tambem apontam pra esse mesmo arquivo, entao o
# x11vnc consegue se autenticar contra o display :0 sem precisar
# descobrir o cookie. Sem Requires= numa unit especifica de flavor —
# fica retentando (Restart=on-failure) ate a sessao X aparecer,
# funciona com qualquer flavor X11 ativo no tty1.
Environment=XAUTHORITY=/root/.Xauthority
ExecStart=/usr/bin/x11vnc -display :0 -auth /root/.Xauthority -rfbauth /root/.vnc/passwd -forever -shared -rfbport 5900 -noxdamage -bg -o /var/log/x11vnc.log
Type=forking
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
}

# --- Helper compartilhado: teclas de Volume Up/Down -> PipeWire ------------
#
# Generico de proposito, igual ao x11vnc acima — nao depende de X11 nem
# Wayland (le os eventos direto de /dev/input/eventN via evdev), entao
# funciona igual nos tres flavors graficos. So faz sentido nesses
# flavors: em headless/server nao ha PipeWire rodando (nem sentido em
# volume de saida de audio sem sessao grafica nenhuma), por isso este
# helper so e chamado pelos instaladores de flavor abaixo, nunca no
# build base (05-build-rootfs.sh).
setup_volume_keys_service() {
    local tag="$1"

    # PipeWire so aceita comandos do wpctl (via runuser -u alarm) se a
    # sessao --user de "alarm" estiver de fato rodando pipewire+wireplumber.
    # Sem isso o wpctl ate conecta (socket activation cria uma instancia
    # efemera), mas sem WirePlumber nenhum device ALSA e enumerado — sem
    # Sinks/Sources, "set-volume" nao tem o que controlar. Confirmado ao
    # vivo 2026-09-04 (ver docs/archived/AUDIO_VOLUME_BUTTONS_INVESTIGATION.md
    # secao 7): sem enable-linger + enable --now dos 3 units abaixo, o
    # teste ponta-a-ponta com os botoes fisicos nao mudava o volume real.
    echo "[$tag] Habilitando sessao PipeWire padrao para o usuario alarm..."
    loginctl enable-linger alarm
    runuser -u alarm -- bash -c '
        export XDG_RUNTIME_DIR=/run/user/1000
        systemctl --user enable --now pipewire.socket pipewire-pulse.socket wireplumber.service pipewire.service
    '

    echo "[$tag] Instalando unit systemd das teclas de Volume Up/Down -> PipeWire..."
    cat > /etc/systemd/system/sanders-volume-keys.service <<'EOF'
[Unit]
Description=Volume Up/Down (evdev) -> PipeWire via wpctl (sanders)
# After=systemd-user-sessions.service, NAO multi-user.target: a unit e
# WantedBy=multi-user.target, e "After=multi-user.target" + want do
# mesmo target = ciclo (o systemd apagava o job em todo boot; e como o
# sanders-player.service e After= deste, o job do PLAYER tambem era
# apagado — o player nunca subia no boot). Fix 2026-09-10 (BF) — ver
# docs/KNOWN_ISSUES_AND_POTENTIAL_BUGS.md BUG-013.
After=systemd-user-sessions.service

[Service]
Type=simple
# PIPEWIRE_USER: usuario dono da sessao PipeWire (--user) que o wpctl
# do script vai controlar via runuser. Ver docs/ROADMAP_AND_TODOS.md
# (secao PipeWire) pra qual usuario efetivamente roda a sessao de audio
# neste sistema — hoje "alarm". Ajuste aqui se isso mudar.
Environment=SANDERS_PIPEWIRE_USER=alarm
ExecStart=/usr/bin/python3 /usr/local/bin/sanders-volume-keys.py
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl enable --now sanders-volume-keys.service
}

# --- Flavor: minimal ---------------------------------------------------------
#
# Nao instala pacote nenhum de proposito — e o estado "cru" logo apos o
# build headless (kernel, SSH, rede ja prontos, mais nada). Existe como
# flavor explicito so pra ter um comando unico de diagnostico e pra
# documentar que "nao fazer nada" e uma escolha valida, nao um estado
# intermediario esquecido.

flavor_minimal_install() {
    echo "[flavor:minimal] Nenhum pacote instalado — apenas diagnostico."
    /usr/local/bin/sanders-server-setup.sh --status
}

flavor_minimal_remove() {
    echo "[flavor:minimal] Nao ha nada a desabilitar (flavor sem servicos/pacotes proprios)."
}

# --- Flavor: server ------------------------------------------------------------
#
# Ferramentas basicas de linha de comando pra uso como servidor headless
# (sem sessao grafica). A mecanica real (lista de pacotes + relatorio de
# diagnostico) vive em sanders-server-setup.sh — mantido como script
# separado porque tambem e util standalone (`--status` sem reinstalar
# nada); este flavor so e a porta de entrada unificada.

flavor_server_install() {
    echo "[flavor:server] Instalando ferramentas basicas de servidor..."
    /usr/local/bin/sanders-server-setup.sh --install
}

flavor_server_remove() {
    echo "[flavor:server] Este flavor nao tem servico proprio pra desabilitar"
    echo "(htop/git/curl/vim/fastfetch/docker/bluez-utils continuam instalados —"
    echo "remova pacotes individuais via 'pacman -R <pacote>' se quiser)."
}

# --- Flavor: xfce -----------------------------------------------------------

flavor_xfce_install() {
    echo "[flavor:xfce] Instalando Xorg + XFCE4..."
    # Driver KMS generico ("modesetting") ja vem embutido no proprio
    # xorg-server desde 2018 — nao existe mais como pacote separado
    # (confirmado ao vivo 2026-09-03: "target not found:
    # xf86-video-modesetting"). Funciona com o DRM_MSM ja ativo sem
    # nada extra.
    # xf86-input-libinput: touchscreen FT5436 (evdev) via libinput.
    pacman_install \
        xorg-server xorg-xinit xorg-xrandr \
        xf86-input-libinput \
        xfce4 xfce4-terminal \
        ttf-dejavu noto-fonts \
        gst-plugins-base gst-plugins-good gst-plugins-ugly evtest

    echo "[flavor:xfce] Escrevendo xinitrc..."
    # dbus-run-session (nao "exec startxfce4" puro, nem "dbus-launch"):
    # sem sessao D-Bus, o XFCE mostra "Nao foi possivel se comunicar com
    # o servidor de configuracoes" (xfconfd) e varios componentes
    # (notificacoes, thunar, polkit) falham. Confirmado ao vivo
    # 2026-09-03. dbus-launch (double-fork classico) tambem foi
    # tentado primeiro mas o dbus-daemon que ele desanexa em background
    # desaparecia sob a supervisao de cgroup do systemd (o socket em
    # /tmp sumia, xfconfd nunca subia). dbus-run-session mantem o
    # dbus-daemon como filho direto do processo em vez de daemonizar,
    # o que sobrevive normalmente dentro de um systemd service.
    cat > /usr/local/bin/sanders-xfce-xinitrc <<EOF
#!/bin/sh
xrandr --output $DSI_CONNECTOR --rotate left
exec dbus-run-session -- startxfce4
EOF
    chmod +x /usr/local/bin/sanders-xfce-xinitrc

    setup_x11vnc_service "flavor:xfce"
    setup_volume_keys_service "flavor:xfce"

    echo "[flavor:xfce] Instalando unit systemd (tty1, mesmo padrao do weston/phosh)..."
    cat > /etc/systemd/system/sanders-xfce.service <<'EOF'
[Unit]
Description=Xorg + XFCE4 desktop (sanders flavor: xfce)
After=systemd-user-sessions.service
# So um ambiente grafico por vez no tty1 — evita brigar pela mesma tty
# com outros flavors X11/Wayland.
Conflicts=getty@tty1.service weston.service phosh.service sanders-xorg-minimal.service sanders-weston-minimal.service

[Service]
User=root
Environment=XDG_RUNTIME_DIR=/run/user/0
# XAUTHORITY fixo (nao o cookie aleatorio padrao do startx em /tmp) —
# sanders-x11vnc.service precisa desse mesmo arquivo pra autenticar
# contra o display :0.
Environment=XAUTHORITY=/root/.Xauthority
ExecStartPre=/bin/mkdir -p /run/user/0
ExecStartPre=/bin/chmod 700 /run/user/0
ExecStart=/usr/bin/startx /usr/local/bin/sanders-xfce-xinitrc -- :0 vt1 -keeptty -nolisten tcp -auth /root/.Xauthority
Restart=on-failure
RestartSec=3
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    echo "[flavor:xfce] Instalado, mas NADA habilitado automaticamente ainda"
    echo "(evita brigar com quem ja usa o tty1 via serial/console). Pra ligar:"
    echo "  systemctl disable --now getty@tty1.service"
    echo "  systemctl enable --now sanders-xfce.service   # sessao na tela fisica"
    echo "  systemctl enable --now sanders-x11vnc.service # acesso remoto VNC, porta 5900"
}

flavor_xfce_remove() {
    echo "[flavor:xfce] Desabilitando (pacotes permanecem instalados — remova via pacman -R se quiser)..."
    systemctl disable --now sanders-xfce.service 2>/dev/null || true
    systemctl disable --now sanders-x11vnc.service 2>/dev/null || true
    systemctl disable --now sanders-volume-keys.service 2>/dev/null || true
    systemctl enable --now getty@tty1.service 2>/dev/null || true
}

# --- Flavor: xorg -------------------------------------------------------------
#
# So Xorg + xterm, sem gerenciador de janelas nem desktop nenhum — pra
# desenvolver/testar seu proprio app grafico direto, sem overhead de
# painel/DE. Sobe um xterm por default (troque/edite o xinitrc se seu
# app deve subir sozinho no lugar dele).

flavor_xorgmin_install() {
    echo "[flavor:xorg] Instalando Xorg minimo..."
    pacman_install \
        xorg-server xorg-xinit xorg-xrandr \
        xf86-input-libinput \
        xterm

    echo "[flavor:xorg] Escrevendo xinitrc..."
    cat > /usr/local/bin/sanders-xorgmin-xinitrc <<EOF
#!/bin/sh
xrandr --output $DSI_CONNECTOR --rotate left
# Sem gerenciador de janelas — so um xterm. Pra rodar seu proprio app
# no lugar, troque a linha abaixo (ou aponte DISPLAY=:0 pra ele e rode
# via SSH/systemd separado, com esse xterm so de fallback/debug).
exec xterm -fa Monospace -fs 14
EOF
    chmod +x /usr/local/bin/sanders-xorgmin-xinitrc

    setup_x11vnc_service "flavor:xorg"
    setup_volume_keys_service "flavor:xorg"

    echo "[flavor:xorg] Instalando unit systemd (tty1)..."
    cat > /etc/systemd/system/sanders-xorg-minimal.service <<'EOF'
[Unit]
Description=Xorg minimo + xterm (sanders flavor: xorg)
After=systemd-user-sessions.service
Conflicts=getty@tty1.service weston.service phosh.service sanders-xfce.service sanders-weston-minimal.service

[Service]
User=root
Environment=XAUTHORITY=/root/.Xauthority
ExecStart=/usr/bin/startx /usr/local/bin/sanders-xorgmin-xinitrc -- :0 vt1 -keeptty -nolisten tcp -auth /root/.Xauthority
Restart=on-failure
RestartSec=3
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    echo "[flavor:xorg] Instalado, mas NADA habilitado automaticamente ainda. Pra ligar:"
    echo "  systemctl disable --now getty@tty1.service"
    echo "  systemctl enable --now sanders-xorg-minimal.service"
    echo "  systemctl enable --now sanders-x11vnc.service # acesso remoto VNC, porta 5900"
}

flavor_xorgmin_remove() {
    echo "[flavor:xorg] Desabilitando..."
    systemctl disable --now sanders-xorg-minimal.service 2>/dev/null || true
    systemctl disable --now sanders-x11vnc.service 2>/dev/null || true
    systemctl disable --now sanders-volume-keys.service 2>/dev/null || true
    systemctl enable --now getty@tty1.service 2>/dev/null || true
}

# --- Flavor: weston-minimal (Weston) ----------------------------------------
#
# Weston puro, sem shell/painel de desktop extra, com o backend VNC
# NATIVO dele (nao x11vnc — Wayland nao e X11). Diferenca importante:
# o backend VNC do Weston cria uma SAIDA VIRTUAL SEPARADA, nao espelha
# a tela fisica (DSI) — quem conectar via VNC ve um desktop Wayland
# independente, nao o que esta na tela do aparelho. Autenticacao e via
# PAM (usuario/senha do sistema — root + a senha local), nao um
# arquivo de senha VNC dedicado; o pacote weston ja instala o
# /etc/pam.d/weston-remote-access necessario, nada a configurar aqui.

flavor_westonmin_install() {
    echo "[flavor:weston-minimal] Instalando Weston..."
    # neatvnc: dependencia opcional do weston pro backend VNC funcionar
    # de verdade — sem ela o "weston --backends=drm-backend.so,vnc-backend.so"
    # falha ao carregar o modulo VNC. seatd: weston moderno usa libseat
    # pra gerenciar acesso ao DRM/VT — sem ele (nem logind, que este
    # rootfs minimal nao tem) weston morre com "fatal: your system
    # should either provide the logind D-Bus API, or use seatd."
    # Confirmado ao vivo 2026-09-03 (mesma dependencia que ja existia
    # pro weston.service do flavor "desktop" antigo, so nao tinha sido
    # replicada aqui). xorg-xwayland: sem ele o weston tenta lancar
    # /usr/bin/Xwayland (lazy, so no primeiro cliente X) e morre com
    # "Couldn't launch client" -> "xserver crashing too fast, not
    # restarting" — confirmado ao vivo 2026-09-04.
    pacman_install weston neatvnc seatd xorg-xwayland gst-plugins-base gst-plugins-good gst-plugins-ugly evtest

    echo "[flavor:weston-minimal] Habilitando seatd..."
    systemctl enable --now seatd.service

    setup_volume_keys_service "flavor:weston-minimal"

    echo "[flavor:weston-minimal] Escrevendo weston.ini..."
    mkdir -p /root/.config
    cat > /root/.config/weston.ini <<EOF
[core]
xwayland=true
idle-time=0
require-input=false

[shell]
locking=false

[output]
name=$DSI_CONNECTOR
transform=rotate-270

[output]
name=vnc
mode=1920x1080
EOF

    echo "[flavor:weston-minimal] Instalando unit systemd (tty1, backends drm+vnc simultaneos)..."
    # --disable-transport-layer-security: sem isso o backend VNC exige
    # TLS (certificado auto-assinado ou fornecido) — pra manter simples
    # e consistente com o x11vnc dos outros flavors (tambem sem TLS,
    # mesma postura de seguranca — uso pretendido e rede local/confiavel).
    cat > /etc/systemd/system/sanders-weston-minimal.service <<'EOF'
[Unit]
Description=Weston minimo (Wayland) + backend VNC nativo (sanders flavor: weston-minimal)
After=systemd-user-sessions.service seatd.service
Wants=seatd.service
Conflicts=getty@tty1.service weston.service phosh.service sanders-xfce.service sanders-xorg-minimal.service

[Service]
User=root
Environment=XDG_RUNTIME_DIR=/run/user/0
ExecStartPre=/bin/mkdir -p /run/user/0
ExecStartPre=/bin/chmod 700 /run/user/0
# /tmp/.X11-unix pode nao existir ainda neste rootfs minimal (sem
# systemd-tmpfiles-setup rodando as regras do xorg-server a tempo) —
# sem ele o Xwayland falha "failed to bind to /tmp/.X11-unix/X0: No
# such file or directory" e o weston mata o processo. Confirmado ao
# vivo 2026-09-04.
ExecStartPre=/bin/mkdir -p /tmp/.X11-unix
ExecStartPre=/bin/chmod 1777 /tmp/.X11-unix
# --renderer=gl: GPU Adreno 506 (freedreno) ativa e validada ao vivo
# em 2026-09-04 (ver docs/HARDWARE_STATUS.md) — usa o driver freedreno
# real via GBM/EGL em vez do renderer por software. Ate 2026-09-03 este
# kernel nao expunha GPU nenhuma ("msm_mdp: no GPU device was found"),
# por isso o --renderer=pixman original; nao se aplica mais.
ExecStart=/usr/bin/weston --backends=drm-backend.so,vnc-backend.so --renderer=gl --disable-transport-layer-security
Restart=on-failure
RestartSec=3
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    echo "[flavor:weston-minimal] Instalado, mas NAO habilitado automaticamente. Pra ligar:"
    echo "  systemctl disable --now getty@tty1.service"
    echo "  systemctl enable --now sanders-weston-minimal.service"
    echo "[flavor:weston-minimal] Acesso remoto: VNC na porta 5900, login = usuario 'root' + senha local do sistema"
    echo "(PAM via /etc/pam.d/weston-remote-access, ja vem com o pacote weston). SEM TLS — rede local/confiavel apenas."
    echo "[flavor:weston-minimal] Lembrete: a saida VNC e uma tela VIRTUAL separada, nao espelha a tela fisica."
}

flavor_westonmin_remove() {
    echo "[flavor:weston-minimal] Desabilitando..."
    systemctl disable --now sanders-weston-minimal.service 2>/dev/null || true
    systemctl disable --now sanders-volume-keys.service 2>/dev/null || true
    systemctl enable --now getty@tty1.service 2>/dev/null || true
}

# --- Dispatch -----------------------------------------------------------

case "$FLAVOR" in
    list|"")
        list_flavors
        ;;
    minimal)
        case "$ACTION" in
            --remove) flavor_minimal_remove ;;
            *)        flavor_minimal_install ;;
        esac
        ;;
    server)
        case "$ACTION" in
            --remove) flavor_server_remove ;;
            *)        flavor_server_install ;;
        esac
        ;;
    xfce)
        case "$ACTION" in
            --remove) flavor_xfce_remove ;;
            *)        flavor_xfce_install ;;
        esac
        ;;
    xorg)
        case "$ACTION" in
            --remove) flavor_xorgmin_remove ;;
            *)        flavor_xorgmin_install ;;
        esac
        ;;
    weston-minimal)
        case "$ACTION" in
            --remove) flavor_westonmin_remove ;;
            *)        flavor_westonmin_install ;;
        esac
        ;;
    *)
        echo "Flavor desconhecido: $FLAVOR" >&2
        list_flavors >&2
        exit 1
        ;;
esac
