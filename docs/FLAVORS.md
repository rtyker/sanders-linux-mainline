# Flavors pós-deploy

Ponto único de entrada: `sanders-flavor-install.sh <flavor> [--remove]`. Roda
num sistema **já instalado e rodando** (não em build-time — o
`FLAVOR=headless|desktop` do `05-build-rootfs.sh` é uma coisa diferente,
decidida antes, e hoje só afeta o que já vem embutido na imagem inicial:
Wi-Fi/SSH/Samba/BlueZ/ALSA base).

| Flavor | Pacotes instalados | Interface | Acesso remoto | Uso ideal |
| :--- | :--- | :--- | :--- | :--- |
| **`minimal`** | nenhum | nenhuma (só diagnóstico) | — | estado cru pós-build, só roda `--status` |
| **`server`** | htop, git, curl, vim, fastfetch, bluez, bluez-utils (sem Docker) | linha de comando (headless) | SSH | servidor puro, sem GUI, sem peso de containers |
| **`server-docker`** | mesma base do `server` + docker (nunca auto-habilitado) | linha de comando (headless) | SSH | servidor headless que vai rodar containers |
| **`xorg`** | Xorg + xterm (sem gerenciador de janelas) | X11 mínimo | x11vnc (:5900) | rodar app gráfico próprio, sem overhead — mas **fallback**, pode travar o painel DSI |
| **`weston-minimal`** | Weston (Wayland) + GPU real (freedreno) | compositor Wayland puro, sem shell/painel | VNC **nativo** do Weston (:5900, saída virtual separada, não espelha a tela física) | **preferencial** pra uso gráfico |
| **`xfce`** | Xorg + XFCE4 completo | desktop leve via X11 | x11vnc (:5900) | desktop tradicional completo |

## Detalhes que valem notar

- Todos (exceto `minimal`) instalam a tecla de Volume Up/Down → PipeWire (`setup_volume_keys_service`).
- `xorg` e `xfce` compartilham o mesmo helper `x11vnc` (senha gerada na primeira instalação, salva em `/root/.vnc/passwd`).
- `weston-minimal` usa autenticação **PAM** (usuário/senha do sistema) em vez de senha VNC própria — vem pronto no pacote `weston` (`/etc/pam.d/weston-remote-access`).
- Nenhum flavor gráfico é habilitado automaticamente no boot — todos exigem `systemctl enable --now` manual, e só um roda no `tty1` por vez (`Conflicts=` entre as units).
- Nomes antigos `xorg-minimal`/`wayland-minimal` continuam aceitos como alias (renomeados em 2026-09-04), com aviso de depreciação.
- `server-docker` (2026-09-13) roda a instalação do `server` primeiro e depois adiciona Docker por cima — Docker foi separado do `server` porque traz peso real (imagens, storage driver overlay, daemon) que nem todo uso headless precisa.

## Comandos

```bash
sanders-flavor-install.sh list              # lista flavors disponíveis
sanders-flavor-install.sh <flavor>          # instala e habilita os serviços do flavor
sanders-flavor-install.sh <flavor> --remove # desabilita (não desinstala pacotes)
```

Script: [`rootfs-overlay/common/usr/local/bin/sanders-flavor-install.sh`](../rootfs-overlay/common/usr/local/bin/sanders-flavor-install.sh).
