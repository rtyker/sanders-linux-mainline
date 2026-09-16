# sanders-linux-mainline

**Porte do Kernel Linux Mainline + Arch Linux ARM para a linha Motorola Moto G5 (Snapdragon 625 / MSM8953).**

Alvos: **Moto G5s Plus (`sanders`)** e **Moto G5 Plus (`potter`)**.

Este é, até onde sabemos, o primeiro porte funcional do `sanders` e do `potter` para o kernel Linux mainline upstream com aceleração gráfica completa, stack de áudio Hexagon ADSP, Wi-Fi/Bluetooth integrados e boot 100% autônomo diretamente do eMMC via `lk2nd`.

> ⚠️ **Unidade física de bancada usada nos testes ao vivo: Moto G5 Plus (`potter` XT1683 RETBR).**  
> Confirmado via `fastboot getvar all` (`product: potter`, `board: potter`). As plataformas `sanders` e `potter` compartilham a quase totalidade da arquitetura (mesmo SoC Snapdragon 625, PMICs, painel DSI e periféricos, com pequenas variações de chip de touchscreen e câmera). Itens marcados como validados "ao vivo" foram confirmados no hardware `potter` de bancada.
>
> 🚀 **Estado atual:** Sistema 100% autônomo e operacional. O aparelho liga diretamente na tomada/bateria e inicializa o Arch Linux ARM sem necessidade de PC ou comandos fastboot. Possui display nativo MIPI-DSI Full HD com aceleração 3D por hardware (GPU Adreno 506 via freedreno), áudio analógico funcional no alto-falante, botões físicos integrados, Wi-Fi WPA2, Bluetooth com A2DP Sink (aptX HD), console serial CDC ACM e rede SSH sobre USB e Wi-Fi.

---

| Especificação | Detalhe |
| :--- | :--- |
| **Dispositivos** | Motorola Moto G5s Plus (`sanders`) / Moto G5 Plus (`potter`) |
| **SoC** | Qualcomm Snapdragon 625 (MSM8953) — 8x ARM Cortex-A53 @ 2.0 GHz |
| **GPU** | Adreno 506 (aceleração OpenGL/EGL via driver `freedreno`) |
| **RAM / Armazenamento** | 2 GB / 3 GB / 4 GB LPDDR3 — 32 GB eMMC 5.1 (`mmcblk0`) |
| **Bootloader** | [lk2nd](https://github.com/msm8916-mainline/lk2nd) (2º estágio gravado permanentemente em `mmcblk0p37`/`p38`) |
| **Kernel** | Linux Mainline upstream (v6.x / v7.x) + patches de integração |
| **Distribuição** | Arch Linux ARM (`aarch64`) |
| **Acesso padrão** | Serial USB CDC ACM (`/dev/ttyACM0`), SSH via USB (`10.42.0.2`) e Wi-Fi (`wlan0`) |

---

## 📱 Demonstração

![Foto da tela mostrando archlinuxarm login:](docs/screenshots/login.jpg)

*(Systemd em execução nativa com console no painel MIPI-DSI e shell serial/SSH ativos).*

---

## ⚡ Status dos Componentes de Hardware

| Componente | Status | Detalhes Técnicos |
| :--- | :---: | :--- |
| **Boot Autônomo (eMMC)** | ✅ | ABOOT → `lk2nd` (`boot` `mmcblk0p37`) → extlinux (`cache` `mmcblk0p52`, ext2) → Linux Mainline → Rootfs (`userdata` `mmcblk0p54`, ext4). Inicializa sem intervenção de PC. |
| **Display Nativo MIPI-DSI** | ✅ | Painéis Tianma TL052VDXP02 / BOE BS052FHM-A00-6C01 operacionais via driver DRM MSM nativo (MDSS/MDP5/DSI). Resolução 1080x1920 portrait. Backlight WLED controlável via sysfs. |
| **Aceleração 3D / GPU** | ✅ | Adreno 506 com driver upstream `freedreno` ativo (`/dev/dri/card1` e `renderD128`). Suporte a OpenGL, EGL e Wayland (Weston `--renderer=gl`). |
| **Touchscreen** | ✅ | Synaptics S3603R em `0x20` no Potter (driver `rmi4_i2c` com patch de fallback F12) e FocalTech FT5436 em `0x38` no Sanders (`edt-ft5x06`). Multi-touch Full HD ativo. |
| **Wi-Fi (WCN3680B)** | ✅ | Driver `wcn36xx` via `qcom-wcnss-pil`. WPA2/PSK 100% funcional com patch de fallback NOVHT (`0002-wcn36xx-wcn3680-novht-fallback.patch`). DHCP e internet nativos. |
| **Bluetooth (WCN3680B)** | ✅ | BlueZ 5.87 via `btqcomsmd`. Endereço MAC original de fábrica restaurado via `/persist/bluetooth/.bt_nv.bin`. Pareamento BLE e receptor de áudio A2DP Sink com codec aptX HD via PipeWire. |
| **Áudio e Alto-falante** | ✅ | Qualcomm Hexagon ADSP QDSP6 + codec analógico SPMI PM8953 (WCD). Saída de som no alto-falante frontal (`plughw:0,0`). Mobile MP3 Player GTK4 com skins retrô integrado. |
| **Botões Físicos** | ✅ | Volume Up (`gpio-keys`), Volume Down (`pm8941_resin`) e Power (`pm8941_pwrkey`). Daemon dedicado ajusta ganho ALSA e PipeWire com feedback visual em tela. |
| **USB Gadget** | ✅ | Controlador DWC3 operando em modo periférico: console serial CDC ACM (`/dev/ttyACM0`) + rede CDC ECM (`usb0` @ 10.42.0.2). |
| **Gerenciamento de Energia** | ✅ | Watchdog de hardware MSM (`/dev/watchdog0`) supervisionado pelo systemd (auto-reboot em 30s se congelar); zRAM swap dinâmico (50% da RAM). |
| **Containers (Docker / Podman)** | ✅ | Docker Engine e Podman totalmente suportados com storage driver `overlay2` e compatibilidade `iptables-nft` via `CONFIG_NFT_COMPAT=y`. |
| **Sensor Proximidade / Luz** | ⚠️ | LiteON LTR559 funcional no bus I2C 1 (`ltr501`). Proximidade responde a obstruções; calibração ALS (luz ambiente) em andamento. |
| **Acelerômetro / Giroscópio** | 🔴 | Roteados internamente ao Hexagon ADSP. Diagnóstico QRTR confirmou que a porta SMGR reportada é artefato; requer canal FastRPC para ativação futura. |
| **LED Frontal** | ❌ | Inexistente/não-povoado no hardware de bancada XT1683 (investigação detalhada concluída). |

Para relatórios detalhados, consulte [`docs/HARDWARE_STATUS.md`](docs/HARDWARE_STATUS.md).

---

## 🛠️ Como Reproduzir (Build do Zero)

### Pré-requisitos
- Sistema host Linux x86_64 (preferencialmente Arch Linux ou derivado).
- Pacotes base: `cross-aarch64-linux-gnu-gcc`, `arm-none-eabi-gcc`, `android-tools`, `dtc`, `git`, `make`, `ccache`.
- Aparelho com bootloader **desbloqueado** (`fastboot oem unlock`).

### Passo a Passo de Compilação

Execute os scripts numerados em ordem a partir da raiz do repositório:

```bash
git clone https://github.com/rtyker/sanders-linux-mainline.git
cd sanders-linux-mainline

# 1. Instala dependências do host
./scripts/00-setup-host.sh

# 2. Compila o bootloader de segundo estágio lk2nd
./scripts/01-build-lk2nd.sh

# 3. Clona o kernel Linux mainline, aplica patches e compila (Image.gz + DTBs)
./scripts/02-build-kernel.sh

# 4. Compila o BusyBox estático para o initramfs
./scripts/03-build-busybox.sh

# 5. Gera a imagem initramfs (cpio.gz) com suporte a UDC e montagem de rootfs
./scripts/04-build-initramfs.sh

# 6. Constrói o sistema de arquivos Arch Linux ARM base (requer sudo)
sudo ./scripts/05-build-rootfs.sh

# 7. Prepara as imagens de boot (Android boot.img e staging extlinux ext2)
./scripts/06-build-boot.sh
```

---

## 💾 Provisionamento e Deploy

### 1. Gravação Inicial no Aparelho (Flash Permanente)
Com o aparelho no modo **Fastboot**:

```bash
# ⚠️ ATENÇÃO: Formata o eMMC e grava o lk2nd e o Arch Linux ARM
sudo ./scripts/99-flash-rootfs-final.sh
```

O script grava o `lk2nd.img` nas partições `boot` e `recovery`, formata a partição `cache` (`mmcblk0p52`, ext2) como partição de boot extlinux e instala o rootfs ext4 na partição `userdata` (`mmcblk0p54`).

A partir deste momento, o aparelho **inicializa sozinho de forma autônoma** ao receber energia.

### 2. Deploy Contínuo de Atualizações de Kernel/DTB
Após o provisionamento inicial, **não é necessário usar fastboot nem apagar o eMMC** para testar novos kernels:

```bash
# Com o aparelho conectado via USB e rede ativa:
./scripts/10-deploy-boot.sh --reboot
```

O script transfere `Image.gz` e os arquivos `.dtb` via SCP, monta a partição ext2 de boot (`cache`), cria backup de segurança da versão anterior, atualiza os arquivos e reinicia o dispositivo.

---

## 🔌 Acesso e Comunicação

### Console Serial CDC ACM
O aparelho disponibiliza um console serial USB em `/dev/ttyACM0`:

```bash
./scripts/conecta_serial.sh --ensure   # Garante restauração da porta e reconexão
picocom -b 115200 /dev/ttyACM0         # Login automático como root
```

### Rede USB e Conexão SSH
O initramfs e o systemd configuram a interface de rede USB gadget `usb0`:

```bash
# No computador host, configure o NAT e o IP do gateway:
sudo ./scripts/08-host-net.sh

# Conecte-se via SSH:
ssh root@10.42.0.2    # Senha padrão: root
```

---

## 🎨 Flavors do Sistema (Pós-Deploy)

O sistema conta com o utilitário `sanders-flavor-install.sh` para transformar o ambiente de acordo com a finalidade desejada:

```bash
sanders-flavor-install.sh list              # Exibe os perfis disponíveis
sanders-flavor-install.sh <flavor>          # Instala e habilita o perfil
sanders-flavor-install.sh <flavor> --remove # Desabilita os serviços do perfil
```

| Flavor | Descrição | Ambiente |
| :--- | :--- | :--- |
| **`server`** | Servidor headless enxuto (htop, git, curl, bluez, ferramentas de rede). | Linha de comando (SSH/Serial) |
| **`server-docker`** | Servidor headless + Docker Engine e docker-compose pré-configurados. | Containers / Microserviços |
| **`weston-minimal`** | Compositor Wayland (Weston) acelerado por hardware via GPU freedreno + VNC. | Wayland / EGL puro |
| **`xfce`** | Ambiente desktop clássico completo XFCE4 exibido no painel DSI nativo e via x11vnc. | Interface Gráfica Completa |
| **`xorg`** | Servidor X11 minimalista com xterm (modo leve para aplicações dedicadas). | X11 simples |

Além dos flavors tradicionais, está disponível o **Sanders Mobile Launcher** (`sanders-launcher.service`), uma interface móvel touch nativa em GTK4 estilo Android executada sob o Weston.

Mais detalhes em [`docs/FLAVORS.md`](docs/FLAVORS.md).

---

## 📂 Estrutura do Repositório

```text
sanders-linux-mainline/
├── docs/                           # Documentação técnica aprofundada
│   ├── HARDWARE_STATUS.md          # Matriz de suporte de componentes
│   ├── FLAVORS.md                  # Perfis de instalação pós-deploy
│   ├── STEP_BY_STEP.md             # Guia detalhado de compilação
│   ├── TROUBLESHOOTING.md          # Registro de diagnósticos e soluções
│   ├── LK2ND_SETUP.md              # Documentação técnica do bootloader
│   └── archived/                   # Planos e especificações concluídos
├── dts/
│   └── msm8953-motorola-sanders.dts # Device Tree Source customizado
├── kernel/
│   ├── sanders.config.fragment     # Opções essenciais do kernel mainline
│   └── *.patch                     # Patches de hardware (Wi-Fi NOVHT, painel DSI, touch, etc.)
├── initramfs/                      # Scripts e configurações do initramfs
├── rootfs-overlay/                 # Configurações de sistema, daemons e serviços do Arch Linux
│   ├── common/                     # Scripts de rede, bluetooth, volume, áudio e flavors
│   └── desktop/                    # Configurações específicas de Wayland/Weston
├── tools/                          # Utilitários auxiliares (launcher, scan BLE, extração QCDT)
└── scripts/                        # Scripts de automação numerados
    ├── 00-setup-host.sh            # Instalação das ferramentas no host
    ├── 01-build-lk2nd.sh           # Compilação do lk2nd
    ├── 02-build-kernel.sh          # Compilação do kernel com ccache
    ├── 03-build-busybox.sh         # Compilação do BusyBox
    ├── 04-build-initramfs.sh       # Empacotamento do initramfs
    ├── 05-build-rootfs.sh          # Construção da imagem base do rootfs
    ├── 06-build-boot.sh            # Geração das imagens de boot
    ├── 07-boot-kernel.sh           # Inicialização temporária via fastboot (recuperação)
    ├── 08-host-net.sh              # Configuração de NAT e rede no host
    ├── 09-extract-firmware.sh      # Extração de firmwares do stock
    ├── 10-deploy-boot.sh           # Deploy incremental de kernel via SSH
    ├── 99-flash-rootfs-final.sh    # Gravação permanente inicial no eMMC
    ├── conecta_serial.sh           # Conexão e recuperação da porta serial/rede
    └── lib.sh                      # Variáveis e rotinas compartilhadas
```

---

## 🤝 Contribuições

Contribuições e testes em hardware físico adicional são bem-vindos. Frentes ativas de melhoria incluem:
1. **Validação em `sanders` físico:** Testes de confirmação de periféricos específicos (câmera, sensores) no Moto G5s Plus real.
2. **Subssistema de Sensores:** Implementação de canal FastRPC para viabilizar acelerômetro e giroscópio através do Hexagon ADSP.
3. **Calibração do sensor de luminosidade (ALS):** Ajustes no registrador do chip LiteON LTR559.

Consulte [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) para diretrizes de desenvolvimento.

---

## 📄 Licença e Créditos

- **Kernel Linux:** GPL-2.0.
- **lk2nd:** Projeto open-source mantido pela comunidade [msm8916-mainline/lk2nd](https://github.com/msm8916-mainline/lk2nd) e fork por [@playday3008](https://github.com/playday3008).
- **DTS Base:** Baseado no trabalho de Sireesh Kodali (`msm8953-motorola-potter.dts`, BSD-3-Clause) e adaptado para a plataforma Motorola MSM8953 mainline.
- Scripts, ferramentas e documentações deste repositório: GPL-2.0.
