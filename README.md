# sanders-linux-mainline

**Linux kernel mainline + Arch Linux ARM bootando no Motorola Moto G5s Plus (codename `sanders`).**

Este é, até onde sabemos, o **primeiro porte conhecido** do `sanders` para
o kernel Linux mainline upstream. Não há porte oficial em
[postmarketOS](https://postmarketos.org/),
[Mobian](https://mobian-project.org/) ou outras distros para mobile.

> ⚠️ **Unidade física de bancada usada nos testes ao vivo: Moto G5 Plus (`potter`), não `sanders`.** Confirmado via `fastboot getvar all` (`product: potter`, `board: potter`) em 2026-09-02. Este repositório e a maior parte da documentação são escritos pro `sanders`, mas os testes "ao vivo"/"confirmado" nos docs deste projeto até agora rodaram no `potter` físico disponível — hardware quase idêntico (ver `AGENTS.md` da raiz do projeto), porém não é a mesma unidade.
>
> ✅ **Estado: sistema interativo via cabo USB + internet.** Boot completo
> do kernel mainline → Arch Linux ARM → autologin root no framebuffer +
> **shell via CDC ACM no host** (`/dev/ttyACM0`) + **rede USB (ECM)** com
> NAT no host fornecendo **acesso à internet** (`ping google.com` ~10ms via
> cabo). Touchscreen Focaltech FT5436 reportando eventos limpos em
> `/dev/input/event1`. Pendências: Wi-Fi nativo, áudio, painel real, SSH
> interativo estável (link USB ainda é a única ponte de rede).
> Veja [`docs/HARDWARE_STATUS.md`](docs/HARDWARE_STATUS.md).

| | |
|---|---|
| **Device** | Motorola Moto G5s Plus |
| **Codename** | `sanders` |
| **SoC** | Qualcomm Snapdragon 625 (`msm8953`) |
| **Bootloader** | [lk2nd](https://github.com/msm8916-mainline/lk2nd) (fork [playday3008](https://github.com/playday3008/lk2nd) — único com DTSI do sanders) |
| **Kernel** | Linux mainline (`master`, ~v6.x) |
| **Distro testada** | Arch Linux ARM aarch64 |

---

## Demo

![Foto da tela mostrando archlinuxarm login:](docs/screenshots/login.jpg)

(systemd subiu, getty ativo no `tty0`/framebuffer — embora ainda sem
teclado para digitar)

---

## TL;DR — como reproduzir

Pré-requisitos: Arch Linux x86_64 (ou adaptar pacotes), bootloader do
aparelho **desbloqueado** (`fastboot oem unlock`), cabo USB.

```bash
git clone https://github.com/<você>/sanders-linux-mainline.git
cd sanders-linux-mainline

./scripts/00-setup-host.sh      # instala toolchain aarch64/arm-none-eabi/android-tools
./scripts/01-build-lk2nd.sh     # ~3 min — bootloader 2º estágio
./scripts/02-build-kernel.sh    # ~15-30 min — clona linux mainline, aplica DTS, compila
./scripts/03-build-busybox.sh   # ~2 min — initramfs userspace
./scripts/04-build-initramfs.sh # <1 min — empacota cpio
sudo ./scripts/05-build-rootfs.sh                # headless (default): SSH/SAMBA/server, ~3 GiB
# OU:
sudo FLAVOR=desktop ./scripts/05-build-rootfs.sh # desktop: Weston + Xwayland + mesa, ~5 GiB
./scripts/06-build-boot.sh      # <1 min — Android boot.img (mesmo para os 2 flavors)

# Aparelho em fastboot mode:
./scripts/07-flash-and-boot.sh --flash    # ⚠️ APAGA /data do Android
```

Após ~60s na tela do aparelho: `archlinuxarm login:`.

Veja [`docs/STEP_BY_STEP.md`](docs/STEP_BY_STEP.md) para detalhes de cada etapa
e o que esperar.

---

## Por que isso é difícil

O `sanders` (Moto G5s Plus) era um vácuo no Linux mobile open source:

- **lk2nd:** existe, mas só no [fork playday3008](https://github.com/playday3008/lk2nd)
  (commit `c8b47cd`); upstream `msm8916-mainline/lk2nd` não tem o DTSI do
  sanders.
- **Kernel mainline:** tem `msm8953.dtsi` e `msm8953-motorola-potter.dts`
  (Moto G5 Plus, irmão direto), mas **não tinha sanders.dts**.
- **postmarketOS / Mobian:** sem porte.
- **Builds prontos de lk2nd não funcionam** — só compilando localmente.

Este repo resolve isso: contém o `msm8953-motorola-sanders.dts` portado a
partir do potter, o config fragment do kernel, o initramfs minimal e os
scripts que automatizam tudo.

---

## Layout do repositório

```
.
├── README.md                   # você está aqui
├── LICENSE                     # GPL-2.0
├── docs/
│   ├── STEP_BY_STEP.md         # cada etapa, o que faz e o que esperar
│   ├── HARDWARE_STATUS.md      # o que funciona e o que ainda não
│   ├── LK2ND_SETUP.md          # bootloader (pré-requisito)
│   ├── TROUBLESHOOTING.md      # erros que encontramos e como resolvemos
│   └── screenshots/            # fotos do feito
├── dts/
│   └── msm8953-motorola-sanders.dts   # ★ o DTS portado
├── kernel/
│   └── sanders.config.fragment        # configs builtin necessárias
├── initramfs/
│   ├── init                           # script de init (busybox shell)
│   ├── busybox.config.fragment        # ajustes pro busybox build
│   ├── busybox-symlinks-bin.txt       # 46 symlinks em /bin
│   └── busybox-symlinks-sbin.txt      # 4 symlinks em /sbin
└── scripts/
    ├── lib.sh                  # vars + helpers
    ├── 00-setup-host.sh        # deps do host
    ├── 01-build-lk2nd.sh       # bootloader
    ├── 02-build-kernel.sh      # kernel + DTB
    ├── 03-build-busybox.sh
    ├── 04-build-initramfs.sh
    ├── 05-build-rootfs.sh      # Arch ARM ext4 image (precisa sudo)
    ├── 06-build-boot.sh        # Android boot.img
    └── 07-flash-and-boot.sh    # fastboot
```

Build artifacts vão para `build/` e `build/out/` (gitignored).

---

## O que funciona (e o que falta)

✅ **Funcional**
- Boot end-to-end do kernel mainline
- Framebuffer console (texto na tela)
- eMMC (29.1 GiB, particionamento, ext4)
- Boot do systemd
- getty no tty1 com **autologin root** (shell root pronto no framebuffer)
- Touchscreen Focaltech FT5436 reportando eventos em `/dev/input/event1`
- **USB CDC ACM** funcional — autologin root via `picocom /dev/ttyACM0` no host
- **Rede USB CDC ECM** + NAT no host — phone navega a internet pelo cabo USB (`scripts/08-host-net.sh` configura o lado do PC)
- **sshd** habilitado no rootfs (root/root) — escutando em 10.42.0.2
- **Dois flavors de rootfs** selecionáveis via `FLAVOR=` no `05-build-rootfs.sh`: `headless` (default, ~3 GiB, server-style — SSH/SAMBA) e `desktop` (~6 GiB, Phosh + Weston + Xwayland + mesa pré-instalados).
- **Phosh + Phoc + Squeekboard** (default no flavor `desktop`) — shell mobile estilo Android com lockscreen, app drawer, painel, OSK integrado, touch funcional. Renderer pixman (CPU). Veja TROUBLESHOOTING #16 sobre a gotcha de `WLR_RENDERER=pixman` em simpledrm.
- **Wayland (Weston)** disponível como alternativa minimalista no flavor `desktop` — autostart via `weston.service` (dorme por default, ativar com `systemctl disable phosh && systemctl enable weston`).
- **OpenGL via Xwayland + llvmpipe** — `glxgears` rodando ~140 FPS (Mesa 26, llvmpipe LLVM 22.1). Stack: glxgears → libGL → llvmpipe → Xwayland → compositor → DRM SimpleDRM. Apenas no flavor `desktop`.
- **Wi-Fi WCN3680B** parcial — pronto firmware carrega, `wlan0` cria, scan funciona, auth+assoc OK, mas **4-way handshake WPA2 falha** (`hal_config_bss MEM_FAIL`, limitação wcn36xx — veja [`docs/HARDWARE_STATUS.md`](docs/HARDWARE_STATUS.md))

❌ **Pendente**
- Display "de verdade" (painel Tianma NT35596 ou DJN ILI7807D — hoje só `simple-framebuffer` exposto como `/dev/dri/card0` via SimpleDRM, sem aceleração GPU)
- Áudio, sensores, câmera, modem

Veja [`docs/HARDWARE_STATUS.md`](docs/HARDWARE_STATUS.md) para detalhes
e [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) para áreas onde ajuda é
bem-vinda.

---

## Contribuindo

PRs e issues são MUITO bem-vindos. Os próximos passos com maior valor:

1. **Resolver `dwc3: failed to initialize core`** — desbloqueia console
   USB e teclado USB-OTG.
2. **Adicionar driver do painel Tianma NT35596** — display real e brilho.
3. **Habilitar touchscreen + Wi-Fi** — sistema utilizável de fato.

Veja [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md).

---

## Licença

GPL-2.0 (compatível com Linux kernel). Veja [`LICENSE`](LICENSE).

O DTS deriva de `msm8953-motorola-potter.dts` (BSD-3-Clause, autoria
Sireesh Kodali); o cabeçalho está preservado. Scripts e docs próprios
deste repo são GPL-2.0.

---

## Créditos

- **lk2nd**: msm8916-mainline contributors + fork de
  [@playday3008](https://github.com/playday3008) (DTSI do sanders).
- **Linux msm8953 base**: postmarketOS / msm8916-mainline community,
  Sireesh Kodali (potter.dts).
- **Este port**: desenvolvido em 2026-05-19. Veja
  `docs/STEP_BY_STEP.md` para o histórico de descobertas (por que
  builds pré-compilados não funcionam, por que `fastboot flash boot` é
  rejeitado, por que `qcom,board-id` precisa de 6 entradas, etc.).
