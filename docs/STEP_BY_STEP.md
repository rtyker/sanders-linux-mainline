# Step-by-step: do zero ao login prompt

Este documento detalha cada script numerado, o que ele faz, **quanto tempo
demora** e **o que esperar de output**. Use como guia quando algo falhar.

## Pré-requisitos

- Arch Linux x86_64 (ou outra distro com pacotes equivalentes — ver
  `00-setup-host.sh`)
- ~5 GiB livres em disco (kernel mainline + builds)
- Motorola Moto G5s Plus com bootloader **desbloqueado**
  - Verifique com `sudo fastboot getvar unlocked` → deve retornar `yes`
  - Se não, ver [`LK2ND_SETUP.md`](LK2ND_SETUP.md) primeiro
- Cabo USB tipo C/micro (depende da revisão do G5s Plus)
- ~1h de tempo total (a maior parte gasta compilando kernel)

## 00. Setup do host (~2 min)

```bash
./scripts/00-setup-host.sh
```

Instala via `pacman`: cross-toolchains aarch64 (kernel) e arm-none-eabi
(lk2nd), `android-tools` (fastboot/mkbootimg), `dtc`, e utilitários
(bsdtar, cpio, etc.).

## 01. Build do lk2nd (~3 min)

```bash
./scripts/01-build-lk2nd.sh
```

Clona o **fork `playday3008/lk2nd`** (commit `c8b47cd`) — único com o
`msm8953-motorola-sanders.dtsi`. Compila o target `lk2nd-msm8953`.

Saída: `build/out/lk2nd.img` (~360 KiB).

**Por que não pode usar o upstream nem builds prontos:** o upstream
`msm8916-mainline/lk2nd` não tem o DTSI do sanders. Builds pré-compilados
publicados também não funcionam neste device (testado). Mais detalhes em
[`TROUBLESHOOTING.md`](TROUBLESHOOTING.md).

## 02. Build do kernel mainline (~15-30 min)

```bash
./scripts/02-build-kernel.sh
```

Primeira execução: clona Linux mainline (shallow, branch `master`,
~300 MiB).

A cada execução:
1. Copia `dts/msm8953-motorola-sanders.dts` para
   `arch/arm64/boot/dts/qcom/`.
2. Adiciona entrada no `Makefile` do diretório DTS.
3. Aplica o config fragment `kernel/sanders.config.fragment` sobre o
   arm64 `defconfig`.
4. Compila `Image.gz` (~15 MiB) e DTBs.

**Configs builtin essenciais** (no fragment):

- `FB_SIMPLE`, `FRAMEBUFFER_CONSOLE`, `LOGO` — texto na tela
- `USB_DWC3*`, `USB_CONFIGFS_ACM`, `U_SERIAL_CONSOLE` — gadget CDC ACM
  (built-in pq o initramfs precisa criar o gadget antes do rootfs estar
  acessível)

## 03. Build do busybox aarch64 estático (~2 min)

```bash
./scripts/03-build-busybox.sh
```

Baixa `busybox-1.36.1`, aplica `defconfig + STATIC=y + #CONFIG_TC is not set`
(o `tc.c` do busybox 1.36.1 quebra com headers Linux atuais), compila.

Saída: `build/busybox-1.36.1/busybox` (~2.1 MiB, ELF aarch64 static).

## 04. Build do initramfs (~5 s)

```bash
./scripts/04-build-initramfs.sh
```

Monta `build/initramfs-root/` com:
- `/bin/busybox` (binário aarch64 estático)
- 46 symlinks em `/bin/` (`sh`, `mount`, `blkid`, `cat`, `cut`, etc.)
- 4 symlinks em `/sbin/` (`switch_root`, `init`, `mount`, `umount`)
- `/init` — nosso script (ver `initramfs/init`)
- mountpoints vazios: `/proc`, `/sys`, `/dev`, `/run`, `/tmp`, `/new_root`,
  `/sys/kernel/config`

Compacta em `build/out/initramfs.cpio.gz` (~1.1 MiB).

### O que o `init` faz

1. Monta virtuais (`proc`, `sys`, `dev`, `run`, `tmp`, `configfs`).
2. **Aguarda até 20s** o UDC (USB controller) aparecer em `/sys/class/udc/`
   — o `dwc3-qcom` sofre probe deferral. **Hoje falha** (`failed to
   initialize core`).
3. Se UDC aparecer, configura USB gadget CDC ACM via configfs e
   tenta abrir shell em `/dev/ttyGS0` (console via cabo USB).
4. Procura rootfs em ordem:
   1. `blkid -L rootfs`
   2. iteração em `/dev/mmcblk0p*` procurando `LABEL=rootfs`
   3. fallback: maior partição ext4
5. `mount` e `switch_root /sbin/init`.
6. Se algum passo falhar, dropa shell de emergência no `tty0`.

## 05. Rootfs Arch Linux ARM (~3 min, precisa sudo)

```bash
sudo ./scripts/05-build-rootfs.sh
```

1. Baixa `ArchLinuxARM-aarch64-latest.tar.gz` (~700 MiB) — só na 1ª vez.
2. Cria imagem ext4 de 3 GiB com `LABEL=rootfs`.
3. Monta loop, extrai tarball preservando perms, adiciona `ttyMSM0` e
   `ttyGS0` em `/etc/securetty`.

Saída: `build/rootfs-arch.img` (3 GiB sparse).

**Credenciais default Arch Linux ARM:** `root/root` e `alarm/alarm`.

## 06. Empacotamento boot.img (~5 s)

```bash
./scripts/06-build-boot.sh
```

1. Concatena `Image.gz + DTB` no estilo "appended-dtb" (esperado pelo
   lk2nd).
2. `mkbootimg` com offsets msm8953 Motorola padrão:
   - base `0x80000000`, kernel `0x8000`, ramdisk `0x1000000`,
     tags `0x100`, pagesize `2048`, header v0.
3. Cmdline: `console=tty0 console=ttyGS0 console=ttyMSM0,115200n8
   earlycon ignore_loglevel printk.time=1 printk.devkmsg=on panic=30`.

Saída: `build/out/boot-sanders.img` (~16 MiB).

## 07. Flash e boot

### Provisionamento Inicial Completo (Flash do eMMC)

Para gravar permanentemente o bootloader `lk2nd`, formatar a partição de boot `cache` (ext2) e flashar o sistema de arquivos `userdata` (ext4):

```bash
sudo ./scripts/99-flash-rootfs-final.sh
```

⚠️ **APAGA os dados do aparelho.** O script solicita confirmação antes de prosseguir.

Sequência realizada:
1. Aparelho em modo fastboot (desligar, depois segurar Power + Vol↓).
2. Grava permanentemente o `lk2nd.img` nas partições `boot` (`mmcblk0p37`) e `recovery` (`mmcblk0p38`).
3. Formata e grava a partição `cache` (`mmcblk0p52`, ext2 com rótulo `boot`) contendo `/extlinux/extlinux.conf`, `Image.gz` e DTBs.
4. Grava o rootfs Arch Linux ARM na partição `userdata` (`mmcblk0p54`, ext4).
5. Reinicia o aparelho para boot autônomo.

### Deploy Normal de Novo Kernel / DTB (Aparelho em Execução)

Com o aparelho já inicializado e conectado via rede USB (`usb0` @ `10.42.0.2`):

```bash
sudo ./scripts/08-host-net.sh           # Se reconectou o cabo USB
./scripts/10-deploy-boot.sh --reboot   # Copia kernel/DTBs para o cache e reinicia
```

### Recuperação de Emergência (Fastboot RAM Boot)

Caso uma modificação cause falha no boot (boot hang), entre no Fastboot de fábrica (Power + Vol↓) e restaure a imagem de boot ou execute boot transitório via RAM:

```bash
./scripts/07-boot-kernel.sh
# Ou restaure a partição cache via fastboot:
fastboot flash cache build/out/boot-cache.img
```

### O que esperar na tela

1. **Logo Motorola** (resíduo do bootloader ABOOT de fábrica, ~2-3s).
2. **Tela do lk2nd** (segundo estágio Little Kernel, ~1-2s).
3. **Texto rolando no painel nativo MIPI-DSI:** dmesg do kernel e mensagens do initramfs.
4. **systemd Arch Linux ARM:** inicialização dos serviços do sistema.
5. **Prompt de Login / Interface Gráfica:** `archlinuxarm login:` no console ou ambiente gráfico Wayland/X11 se habilitado.

---

## Iteração rápida (após mudar kernel/DTS/init)

```bash
./scripts/02-build-kernel.sh         # compila kernel/DTS
./scripts/04-build-initramfs.sh      # se alterou o initramfs
./scripts/06-build-boot.sh           # gera os pacotes de boot e boot-cache.img
./scripts/10-deploy-boot.sh --reboot # envia via SSH/SCP diretamente para a partição cache
```
