#!/bin/bash
# Instalador de "flavors" pos-deploy — roda num sistema JA instalado e
# rodando (nao em build time, diferente do FLAVOR=headless|desktop do
# 05-build-rootfs.sh). Cada flavor e um set de pacotes + config opcional
# que o usuario liga sob demanda, depois que a base (kernel, SSH, rede)
# ja esta pronta.
#
# Uso:
#   sanders-flavor-install.sh list              # lista flavors disponiveis
#   sanders-flavor-install.sh <flavor>           # instala e habilita
#   sanders-flavor-install.sh <flavor> --remove  # desabilita (nao desinstala pacotes)
#
# Flavors disponiveis: xfce

set -euo pipefail

FLAVOR="${1:-}"
ACTION="${2:---install}"

list_flavors() {
    echo "Flavors disponiveis:"
    echo "  xfce   — Xorg + XFCE4 (desktop leve via X11, ativado no tty1) + x11vnc (acesso remoto VNC :5900)"
}

pacman_install() {
    # -Syu, nao -Sy: evita partial upgrade (mesma razao do
    # sanders-server-setup.sh — puxar lib nova via -Sy sozinho pode
    # quebrar binarios ja instalados no sistema).
    pacman -Syu --noconfirm --needed "$@"
}

flavor_xfce_install() {
    echo "[flavor:xfce] Instalando Xorg + XFCE4..."
    # Driver KMS generico ("modesetting") ja vem embutido no proprio
    # xorg-server desde 2018 — nao existe mais como pacote separado
    # (confirmado ao vivo 2026-09-03: "target not found:
    # xf86-video-modesetting"). Funciona com o DRM_MSM (card1-DSI-1)
    # ja ativo sem nada extra.
    # xf86-input-libinput: touchscreen FT5436 (evdev) via libinput.
    # x11vnc: acesso remoto — compartilha a sessao X real (:0, a mesma
    # que aparece na tela fisica via tty1), em vez de abrir uma sessao
    # nova por conexao como o xrdp faria. Trocado de xrdp pra x11vnc
    # porque xrdp/xorgxrdp nao existem nos repositorios binarios do
    # Arch Linux ARM (so via AUR/compilacao — "target not found: xrdp",
    # confirmado ao vivo 2026-09-03); x11vnc/tigervnc estao em extra/.
    pacman_install \
        xorg-server xorg-xinit xorg-xrandr \
        xf86-input-libinput \
        xfce4 xfce4-terminal \
        x11vnc \
        ttf-dejavu noto-fonts

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
    cat > /usr/local/bin/sanders-xfce-xinitrc <<'EOF'
#!/bin/sh
exec dbus-run-session -- startxfce4
EOF
    chmod +x /usr/local/bin/sanders-xfce-xinitrc

    echo "[flavor:xfce] Configurando senha do VNC (x11vnc)..."
    mkdir -p /root/.vnc
    if [ ! -f /root/.vnc/passwd ]; then
        # Senha aleatoria gerada na primeira instalacao — x11vnc exige
        # arquivo de senha pra nao expor a sessao root sem autenticacao
        # nenhuma na rede. Reexecucoes do script preservam a senha ja
        # gerada (idempotente).
        VNC_PASS="$(head -c 12 /dev/urandom | base64 | tr -dc 'a-zA-Z0-9' | head -c 12)"
        x11vnc -storepasswd "$VNC_PASS" /root/.vnc/passwd >/dev/null
        echo "[flavor:xfce] Senha VNC gerada: $VNC_PASS  (salva em /root/.vnc/passwd, guarde/anote agora)"
    else
        echo "[flavor:xfce] Senha VNC ja configurada em /root/.vnc/passwd (nao alterada)."
    fi

    echo "[flavor:xfce] Instalando unit systemd do x11vnc (porta 5900, compartilha a sessao :0)..."
    cat > /etc/systemd/system/sanders-x11vnc.service <<'EOF'
[Unit]
Description=x11vnc — acesso remoto VNC pra sessao XFCE (sanders flavor: xfce)
After=sanders-xfce.service
Requires=sanders-xfce.service

[Service]
User=root
# XAUTHORITY fixo (nao o -auth aleatorio do startx) — sanders-xfce.service
# tambem aponta pra esse mesmo arquivo, entao o x11vnc consegue se
# autenticar contra o display :0 sem precisar descobrir o cookie.
Environment=XAUTHORITY=/root/.Xauthority
ExecStart=/usr/bin/x11vnc -display :0 -auth /root/.Xauthority -rfbauth /root/.vnc/passwd -forever -shared -rfbport 5900 -noxdamage -bg -o /var/log/x11vnc.log
Type=forking
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

    echo "[flavor:xfce] Instalando unit systemd (tty1, mesmo padrao do weston/phosh)..."
    cat > /etc/systemd/system/sanders-xfce.service <<'EOF'
[Unit]
Description=Xorg + XFCE4 desktop (sanders flavor: xfce)
After=systemd-user-sessions.service
# So um ambiente grafico por vez no tty1 — evita os dois brigarem pela
# mesma tty se o flavor "desktop" (weston/phosh) tambem estiver presente.
Conflicts=getty@tty1.service weston.service phosh.service

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
    echo "  (x11vnc precisa da sessao XFCE ja rodando — Requires=sanders-xfce.service cuida disso)"
}

flavor_xfce_remove() {
    echo "[flavor:xfce] Desabilitando (pacotes permanecem instalados — remova via pacman -R se quiser)..."
    systemctl disable --now sanders-xfce.service 2>/dev/null || true
    systemctl disable --now sanders-x11vnc.service 2>/dev/null || true
    systemctl enable --now getty@tty1.service 2>/dev/null || true
}

case "$FLAVOR" in
    list|"")
        list_flavors
        ;;
    xfce)
        case "$ACTION" in
            --remove)
                flavor_xfce_remove
                ;;
            *)
                flavor_xfce_install
                ;;
        esac
        ;;
    *)
        echo "Flavor desconhecido: $FLAVOR" >&2
        list_flavors >&2
        exit 1
        ;;
esac
