#!/bin/bash
# Baixa tarball Arch Linux ARM aarch64 e gera imagem ext4 3 GiB com label "rootfs".
# Precisa sudo (mount loop + bsdtar preservando perms).
#
# Saída: $ROOTFS_IMG (em $BUILD)

source "$(dirname "$0")/lib.sh"
check_cmd bsdtar
check_cmd mkfs.ext4

if [ "$(id -u)" -ne 0 ]; then
    msg "este script precisa de sudo. Reexecutando..."
    exec sudo -E bash "$0" "$@"
fi

cd "$BUILD"

if [ ! -f "$ARCH_TARBALL" ]; then
    msg "baixando $ARCH_TARBALL_URL..."
    curl -fLO "$ARCH_TARBALL_URL"
fi

MNT="$BUILD/_rootfs_mnt"
mountpoint -q "$MNT" && umount "$MNT" || true
rm -rf "$MNT"
mkdir -p "$MNT"

case "$FLAVOR" in
    headless) IMG_SIZE=3G ;;
    desktop)  IMG_SIZE=6G ;;
esac
rm -f "$ROOTFS_IMG"
msg "criando imagem ext4 $IMG_SIZE (FLAVOR=$FLAVOR) com LABEL=rootfs..."
truncate -s "$IMG_SIZE" "$ROOTFS_IMG"
mkfs.ext4 -L rootfs -F "$ROOTFS_IMG" >/dev/null

msg "extraindo Arch Linux ARM tarball..."
mount -o loop "$ROOTFS_IMG" "$MNT"
bsdtar -xpf "$ARCH_TARBALL" -C "$MNT"

# multi-user.target.wants/ ja vem populado na tarball oficial (confirmado:
# remote-fs.target, systemd-networkd.service, sshd.service), entao os
# ln -sf mais abaixo funcionam hoje. Mas isso e implicito — garante aqui
# de forma explicita, caso uma tarball futura venha sem esses symlinks
# base (o ln -sf pra um diretorio inexistente falha com set -e ativo e
# derruba o build inteiro).
mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"

# Inicializa pacman keyring no rootfs (via arch-chroot + qemu-user binfmt).
# Sem isso, no primeiro boot o pacman da:
#   "Public keyring not found; have you run 'pacman-key --init'?"
# e nao consegue instalar pacote nenhum. Faz aqui pra deixar tudo pronto.
if command -v arch-chroot >/dev/null && [ -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
    msg "inicializando pacman keyring (via arch-chroot + qemu-aarch64)..."
    arch-chroot "$MNT" pacman-key --init >/dev/null 2>&1 || warn "pacman-key --init falhou"
    arch-chroot "$MNT" pacman-key --populate archlinuxarm >/dev/null 2>&1 \
        || warn "pacman-key --populate archlinuxarm falhou"
    # Locale pt_BR.UTF-8 (+ en_US.UTF-8 como fallback). Habilita as
    # entradas em /etc/locale.gen e compila com locale-gen. O LANG=
    # default vai em rootfs-overlay/common/etc/locale.conf.
    msg "habilitando locale pt_BR.UTF-8 + en_US.UTF-8..."
    arch-chroot "$MNT" sed -i \
        -e 's/^#\(pt_BR.UTF-8 UTF-8\)/\1/' \
        -e 's/^#\(en_US.UTF-8 UTF-8\)/\1/' \
        /etc/locale.gen
    # sed -i nao retorna erro se o padrao simplesmente nao casar (ex.:
    # formato do locale.gen mudou numa tarball futura) — só confere se
    # falhar em erro real de arquivo. Verifica que as linhas foram de
    # fato descomentadas antes de seguir, senao locale-gen roda "com
    # sucesso" gerando um rootfs sem nenhum dos dois locales.
    grep -q "^pt_BR.UTF-8 UTF-8" "$MNT/etc/locale.gen" \
        && grep -q "^en_US.UTF-8 UTF-8" "$MNT/etc/locale.gen" \
        || warn "sed nao descomentou pt_BR/en_US em /etc/locale.gen — formato do arquivo mudou?"
    arch-chroot "$MNT" locale-gen >/dev/null 2>&1 || warn "locale-gen falhou"

    # Pacotes base para os dois flavors.
    msg "instalando pacotes base (iw + wpa_supplicant + dhcpcd + samba + bluez)..."
    # terminus-font: corrige systemd-vconsole-setup.service (setfont ter-v16b).
    # iw + wpa_supplicant: pilha Wi-Fi (scan + autenticacao WPA2).
    # dhcpcd: fallback de DHCP caso systemd-networkd nao pegue.
    # wireless-regdb + crda: database de regulacao RF por pais.
    # samba: util pros dois flavors (compartilhar /home via Wi-Fi/USB).
    # bluez e bluez-utils: pilha bluetooth (bluetoothd) e utilitarios CLI (bluetoothctl, btmgmt).
    # Nao habilita servico — usuario decide com `systemctl enable smb nmb`.
    # -Syu (nao -Sy sozinho): sincronizar a db sem atualizar os pacotes
    # ja instalados e o classico partial-upgrade do Arch — pode puxar
    # uma lib (glibc/openssl) mais nova que quebra binarios ja instalados.
    arch-chroot "$MNT" pacman -Syu --noconfirm --needed \
        iw wpa_supplicant dhcpcd wireless-regdb crda \
        samba terminus-font bluez bluez-utils \
        alsa-utils alsa-ucm-conf \
        || warn "pacman -S pacotes base falhou"

    if [ "$FLAVOR" = "desktop" ]; then
        msg "instalando stack desktop (weston + xwayland + mesa) via arch-chroot..."
        # Instala dois compositores: phosh (default, shell mobile) +
        # weston (alternativa minimalista). Decisao de qual usar e via
        # systemctl enable/disable. Default no overlay e phosh.
        arch-chroot "$MNT" pacman -Syu --noconfirm --needed \
            phoc phosh squeekboard \
            weston \
            seatd libdisplay-info \
            xorg-xwayland mesa mesa-utils mesa-demos \
            ttf-dejavu noto-fonts \
            || die "pacman -S do stack desktop falhou"
        arch-chroot "$MNT" systemctl enable seatd.service >/dev/null
    fi
else
    warn "arch-chroot/qemu-aarch64 binfmt nao disponivel; keyring sera inicializado no primeiro boot do device"
    [ "$FLAVOR" = "desktop" ] && \
        die "FLAVOR=desktop requer arch-chroot + qemu-aarch64 binfmt no host (instale qemu-user-static-binfmt)"
    # Fallback: cria oneshot service que inicializa o keyring no primeiro boot
    cat > "$MNT/etc/systemd/system/pacman-keyring-init.service" <<'EOF'
[Unit]
Description=Inicializa pacman keyring (apenas uma vez)
After=network-online.target
Wants=network-online.target
ConditionPathExists=!/etc/pacman.d/gnupg/pubring.gpg

[Service]
Type=oneshot
ExecStart=/usr/bin/pacman-key --init
ExecStart=/usr/bin/pacman-key --populate archlinuxarm
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    ln -sf /etc/systemd/system/pacman-keyring-init.service \
        "$MNT/etc/systemd/system/multi-user.target.wants/pacman-keyring-init.service"
fi

# Expansao do filesystem na primeira boot. A imagem ext4 e gerada com 3 GiB
# fixos (truncate + mkfs), mas o fastboot grava ela numa particao userdata
# de ~24 GiB — sem resize2fs sobrariam ~21 GiB inutilizados e a / lota
# rapido (pacman -Syu enche). Oneshot que se auto-desativa via marker em
# /var/lib/sanders-rootfs-expanded.
msg "instalando sanders-rootfs-expand.service (resize2fs no primeiro boot)..."
cat > "$MNT/etc/systemd/system/sanders-rootfs-expand.service" <<'EOF'
[Unit]
Description=Expande o filesystem root para ocupar toda a particao (apenas uma vez)
DefaultDependencies=no
After=systemd-remount-fs.service
Before=local-fs.target sysinit.target
ConditionPathExists=!/var/lib/sanders-rootfs-expanded

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'ROOT=$(findmnt -no SOURCE /); echo "expanding $ROOT"; /usr/sbin/resize2fs "$ROOT"'
ExecStartPost=/bin/sh -c 'mkdir -p /var/lib && touch /var/lib/sanders-rootfs-expanded'

[Install]
WantedBy=sysinit.target
EOF
mkdir -p "$MNT/etc/systemd/system/sysinit.target.wants"
ln -sf /etc/systemd/system/sanders-rootfs-expand.service \
    "$MNT/etc/systemd/system/sysinit.target.wants/sanders-rootfs-expand.service"

# Permite login root via console serial
echo "ttyMSM0" >> "$MNT/etc/securetty" 2>/dev/null || true
echo "ttyGS0"  >> "$MNT/etc/securetty" 2>/dev/null || true

# Autologin root no tty1 (framebuffer console) — sem teclado USB ainda,
# então pelo menos garantimos shell ativo na tela em vez de prompt parado.
msg "instalando autologin root no tty1..."
mkdir -p "$MNT/etc/systemd/system/getty@tty1.service.d"
cat > "$MNT/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I 38400 linux
EOF

# Habilita serial-getty@ttyGS0 com autologin root. Sem isso, o systemd nao
# spawna getty no CDC ACM (so spawna em ttyS*/ttyMSM* por padrao) e o tty
# fica brigado entre o shell do initramfs e nada — daí trava.
msg "instalando serial-getty@ttyGS0 com autologin root..."
mkdir -p "$MNT/etc/systemd/system/serial-getty@ttyGS0.service.d"
cat > "$MNT/etc/systemd/system/serial-getty@ttyGS0.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --keep-baud 115200,57600,38400,9600 %I $TERM
# CDC ACM nao tem flow control de hardware — desliga pra evitar travas
TTYReset=no
TTYVHangup=no
TTYVTDisallocate=no
EOF
mkdir -p "$MNT/etc/systemd/system/getty.target.wants"
ln -sf /usr/lib/systemd/system/serial-getty@.service \
    "$MNT/etc/systemd/system/getty.target.wants/serial-getty@ttyGS0.service"

# Desliga USB autosuspend para o gadget (evita o controller suspender com a
# sessao ACM ativa, o que aparece como "trava" no host).
msg "desligando USB autosuspend via udev rule..."
mkdir -p "$MNT/etc/udev/rules.d"
cat > "$MNT/etc/udev/rules.d/50-usb-gadget-noautosuspend.rules" <<'EOF'
ACTION=="add", SUBSYSTEM=="usb", TEST=="power/control", ATTR{power/control}="on"
EOF

# Rede USB (NCM gadget): IP estatico 10.42.0.2/24 em usb0 via systemd-networkd.
# Host fica com 10.42.0.1/24. Match por MAC pra nao depender do nome da iface.
msg "configurando systemd-networkd em usb0 (10.42.0.2/24)..."
mkdir -p "$MNT/etc/systemd/network"
cat > "$MNT/etc/systemd/network/10-usb0.network" <<'EOF'
[Match]
MACAddress=02:55:44:33:22:11

[Network]
Address=10.42.0.2/24
Gateway=10.42.0.1
DNS=8.8.8.8
DNS=1.1.1.1
ConfigureWithoutCarrier=yes

[Route]
Destination=0.0.0.0/0
Gateway=10.42.0.1
# Metrica alta pra nao competir com wlan — usb gadget eh rede de servico.
Metric=100
EOF

# Wi-Fi (wlan0): DHCP via systemd-networkd. wpa_supplicant gerencia a
# associacao; o config real (SSID/senha) e preenchido pelo usuario com
# sanders-network-setup.sh ou wpa_passphrase. Servico so sobe se o
# arquivo de config existir e tiver conteudo (opcao do wpa_supplicant@).
mkdir -p "$MNT/etc/wpa_supplicant"
cat > "$MNT/etc/wpa_supplicant/wpa_supplicant-wlan0.conf" <<'EOF'
ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=root
update_config=1
EOF
ln -sf /usr/lib/systemd/system/wpa_supplicant@.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service"
cat > "$MNT/etc/systemd/network/20-wlan0.network" <<'EOF'
[Match]
Name=wlan0

[Network]
DHCP=yes
DNS=8.8.8.8
DNS=1.1.1.1

[DHCP]
RouteMetric=10
EOF

# USB OTG Ethernet (host mode): DHCP via systemd-networkd.
# Quando um adaptador USB-C pra Ethernet e plugado no phone, o DWC3
# troca pra host mode e o adaptador aparece como enx[MAC] (predizivel).
# Tambem cobre eth0/naming legacy. Metrica baixa (10) pra ser a
# saida primaria quando conectado — preferida sobre wlan0 (10) e usb0 (100).
cat > "$MNT/etc/systemd/network/30-en-eth.network" <<'EOF'
[Match]
Driver=cdc_ether|rndis_host|cdc_ncm|usb_ether

[Network]
DHCP=yes
DNS=8.8.8.8
DNS=1.1.1.1

[DHCP]
RouteMetric=10
EOF

# resolv.conf gerenciado por systemd-resolved (que pega DNS do networkd)
ln -sf /run/systemd/resolve/stub-resolv.conf "$MNT/etc/resolv.conf"
ln -sf /usr/lib/systemd/system/systemd-resolved.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/systemd-resolved.service"
mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"
mkdir -p "$MNT/etc/systemd/system/sockets.target.wants"
ln -sf /usr/lib/systemd/system/systemd-networkd.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/systemd-networkd.service"
ln -sf /usr/lib/systemd/system/systemd-networkd.socket \
    "$MNT/etc/systemd/system/sockets.target.wants/systemd-networkd.socket"

# SSH: login root so por chave publica (Ed25519 recomendado). Desabilita
# senha pra impedir brute-force. O usuario deve copiar sua chave pub
# pra /root/.ssh/authorized_keys antes de habilitar sshd (via serial).
# IMPORTANTE: a chave DEVE estar em authorized_keys ANTES de desabilitar
# senha, caso contrario trava fora do device.
msg "configurando sshd (chave apenas, sem senha)..."
mkdir -p "$MNT/etc/ssh/sshd_config.d"
cat > "$MNT/etc/ssh/sshd_config.d/10-sanders.conf" <<'EOF'
PermitRootLogin prohibit-password
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM yes
EOF
ln -sf /usr/lib/systemd/system/sshd.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/sshd.service"

# Firmware proprietario (Wi-Fi pronto + iris cal). Sem isso o
# remoteproc do wcnss falha em carregar e o wcn36xx nao traz iface up.
# Use scripts/09-extract-firmware.sh pra extrair do flashfile stock.
if [ -d "$REPO/firmware" ] && ls "$REPO/firmware"/wcnss.* >/dev/null 2>&1; then
    msg "instalando firmware Wi-Fi (wcnss) no rootfs..."
    mkdir -p "$MNT/lib/firmware/wlan/prima"
    cp -v "$REPO/firmware"/wcnss.* "$MNT/lib/firmware/" | tail -3
    if [ -f "$REPO/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin" ]; then
        cp "$REPO/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin" \
            "$MNT/lib/firmware/wlan/prima/"
    else
        warn "WCNSS_qcom_wlan_nv.bin ausente — Wi-Fi nao vai funcionar"
        warn "Extrai-lo de /persist do device. Veja docs/."
    fi
else
    warn "diretorio firmware/ vazio — Wi-Fi nao funcionara"
    warn "Rode scripts/09-extract-firmware.sh para populá-lo"
fi

if [ -d "$REPO/firmware" ] && ls "$REPO/firmware"/adsp.* >/dev/null 2>&1; then
    msg "instalando firmware ADSP (Hexagon QDSP6 audio) no rootfs..."
    mkdir -p "$MNT/lib/firmware/qcom/msm8953"
    cp "$REPO/firmware"/adsp.* "$MNT/lib/firmware/qcom/msm8953/"
    cp "$REPO/firmware"/adsp.* "$MNT/lib/firmware/"
fi

# Keepalive do link USB: ping no host (10.42.0.1) a cada 60s.
# Sem trafego o cdc_ether/dwc3 ocasionalmente reseta a iface no host
# (a iface re-enumera, perde IP). Com ping leve continuo o link aguenta
# indefinidamente (testado 150s a 5s, 60s continuo, zero drops).
msg "instalando usb-keepalive.timer..."
cat > "$MNT/etc/systemd/system/usb-keepalive.service" <<'EOF'
[Unit]
Description=Ping no host (10.42.0.1) pra manter o link USB ativo
After=systemd-networkd.service
Wants=systemd-networkd.service

[Service]
Type=oneshot
ExecStart=/bin/bash -c '[ "$(cat /sys/class/net/usb0/carrier 2>/dev/null)" = "1" ] && /usr/bin/ping -c 1 -W 1 10.42.0.1 || true'

EOF
cat > "$MNT/etc/systemd/system/usb-keepalive.timer" <<'EOF'
[Unit]
Description=Dispara usb-keepalive a cada 60s

[Timer]
OnBootSec=30s
OnUnitActiveSec=60s
AccuracySec=5s

[Install]
WantedBy=timers.target
EOF
mkdir -p "$MNT/etc/systemd/system/timers.target.wants"
ln -sf /etc/systemd/system/usb-keepalive.timer \
    "$MNT/etc/systemd/system/timers.target.wants/usb-keepalive.timer"

# Overlays: common/ aplica nos dois flavors; $FLAVOR/ aplica so no escolhido.
# cp -a preserva symlinks (incluindo os de multi-user.target.wants).
for ov in common "$FLAVOR"; do
    OVERLAY="$REPO/rootfs-overlay/$ov"
    if [ -d "$OVERLAY" ]; then
        msg "aplicando overlay $ov ($OVERLAY)..."
        cp -a "$OVERLAY"/. "$MNT"/
    fi
done

# Desktop: weston.service substitui getty@tty1. Remove o drop-in de
# autologin pra evitar conflito (weston.service tem Conflicts=getty@tty1).
if [ "$FLAVOR" = "desktop" ]; then
    rm -rf "$MNT/etc/systemd/system/getty@tty1.service.d"
fi

# fstab: fonte unica e o overlay (rootfs-overlay/common/etc/fstab), ja
# aplicado no loop de overlays acima. NAO reescrever aqui — um cat>
# heredoc chegou a existir nesse ponto e sobrescrevia silenciosamente o
# fstab do overlay (que tem as entradas de MicroSD), fazendo o suporte a
# MicroSD nunca ir pra imagem final. Ver git log deste arquivo.
#
# Ponto de montagem do MicroSD (fstab referencia /mnt/microsd via
# LABEL=SDCARD + x-systemd.automount; o diretorio precisa existir na
# imagem pro mount funcionar).
mkdir -p "$MNT/mnt/microsd"

sync
umount "$MNT"
rmdir "$MNT"

msg "OK: $ROOTFS_IMG ($(du -h "$ROOTFS_IMG" | cut -f1)) [FLAVOR=$FLAVOR]"
msg "Login default Arch ARM: root/root  ou  alarm/alarm"
