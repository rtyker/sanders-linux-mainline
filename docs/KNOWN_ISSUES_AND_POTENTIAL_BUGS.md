# ⚠️ Relatório Consolidado de Auditoria Técnica: Bugs Potenciais, Limitações e Casos de Borda

Este documento reúne a auditoria técnica exaustiva realizada no repositório **`sanders-linux-mainline`** e na infraestrutura **`projeto_g5`** em **2026-09-02**.

> 🛑 **REGRA DE CONDUTA PARA AGENTES:** 
> Todos os itens abaixo foram mapeados durante revisões estáticas e testes de bancada. **Nenhum código ou configuração foi modificado nesta sessão de auditoria** para preservar rigorosamente o estado do repositório conforme instrução explícita do usuário. Use este documento como guia técnico para investigações e implementações futuras.

---

## 📑 Sumário da Auditoria

1. [🔋 Proteção e Gerenciamento de Bateria](#1--proteção-e-gerenciamento-de-bateria)
2. [🌐 Conectividade e Scripts de Rede](#2--conectividade-e-scripts-de-rede)
3. [🔵 Bluetooth e Integração de Hardware NV](#3--bluetooth-e-integração-de-hardware-nv)
4. [🐕 Kernel, Drivers e Hardware Watchdog](#4--kernel-drivers-e-hardware-watchdog)
5. [🕒 Sincronismo de Tempo e Diagnósticos](#5--sincronismo-de-tempo-e-diagnósticos)
6. [📦 Scripts de Build e Gerenciamento de Pacotes](#6--scripts-de-build-e-gerenciamento-de-pacotes)
7. [📊 Matriz Consolidada de Bugs e Gravidades](#-matriz-consolidada-de-bugs-e-gravidades)

---

## 1. 🔋 Proteção e Gerenciamento de Bateria

### 🔴 BUG-001: Truncamento de Erro na Leitura da Capacidade Sysfs (`sanders-battery-guard.sh`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh#L14)
- **Gravidade:** 🟡 **Média**
- **Sintoma:** O script encerra abruptamente com o erro de bash `[: : integer expression expected` no `journalctl`.
- **Causa Raiz:** 
  ```bash
  CAP=$(awk '{print $1}' "$BAT_SYS/capacity" 2>/dev/null || echo "0")
  ```
  Se o nó `/sys/class/power_supply/battery/capacity` estiver presente no sysfs mas o driver do fuel gauge ainda estiver inicializando ou retornar uma string vazia `""` (ex: falha temporária no barramento I2C/SPMI), a expressão `|| echo "0"` **não é acionada** porque o comando `awk` retorna código `0` com saída vazia. Como resultado, a variável `$CAP` recebe `""`. Na linha 17, a comparação `[ "" -ge 80 ]` quebra a execução do script com erro de sintaxe.
- **Solução Recomendada para o Futuro:**
  ```bash
  CAP_RAW=$(cat "$BAT_SYS/capacity" 2>/dev/null || echo "0")
  CAP=$(echo "$CAP_RAW" | awk '{print ($1 ~ /^[0-9]+$/ ? $1 : 0)}')
  ```

---

### 🟡 BUG-002: Ciclo de Operação Inútil em Modo Descarregamento Bateria (`STATUS != Charging`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh#L24)
- **Gravidade:** 🔵 **Baixa**
- **Sintoma:** Log constante de `Bateria em X%. Retomando carregamento...` a cada 2 minutos quando o celular estiver desconectado do carregador USB.
- **Causa Raiz:**
  ```bash
  elif [ "$CAP" -le "$LOW_LIMIT" ] && [ "$STATUS" != "Charging" ]; then
  ```
  Quando o cabo USB é removido do aparelho, o estado do sysfs muda para `"Discharging"`. Se a bateria estiver abaixo de 70% (`LOW_LIMIT`), a condição é avaliada como verdadeira, e o script tenta gravar `1` no nó `/sys/class/power_supply/battery/online` continuamente a cada 2 minutos, sem verificar se a fonte USB externa está conectada em `/sys/class/power_supply/usb/online`.
- **Solução Recomendada para o Futuro:** Verificar se a fonte USB externa reporta `online == 1` antes de tentar ligar a chave de carga.

---

### 🟠 BUG-003: Ausência de Driver Mainline C para Charger e Fuel Gauge no PMI8996
- **Arquivo:** [`dts/msm8953-motorola-sanders.dts`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/dts/msm8953-motorola-sanders.dts#L470-L494) / [`kernel/sanders.config.fragment`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/sanders.config.fragment#L212-L229)
- **Gravidade:** 🟠 **Alta (Arquitetural)**
- **Sintoma:** O diretório `/sys/class/power_supply/` permanece vazio no kernel mainline.
- **Causa Raiz:** Os nós `pmi8950_smbcharger` (`qcom,pmi8996-smbchg` @ `0x1000`) e `pmi8950_fg` (`qcom,pmi8996-fg` @ `0x4000`) foram devidamente vinculados no Devicetree com base no DTB stock da Motorola. Porém, **não existem drivers C correspondentes no subsistema Linux Mainline** (`drivers/power/supply/`). Os nós no DT funcionam como scaffolding e ficam inertes até que os drivers sejam portados da árvore Android 3.18 / postmarketOS.

---

## 2. 🌐 Conectividade e Scripts de Rede

### 🔴 BUG-004: Expressão Regular Incorreta para Parsing do `/proc/net/dev` (`sanders-network-setup.sh`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh#L102-L113)
- **Gravidade:** 🔴 **Crítica (Funcional)**
- **Sintoma:** O subcomando `sanders-network-setup.sh status` sempre exibe `(nenhuma interface encontrada)` na seção de interfaces, mesmo com interfaces ativas.
- **Causa Raiz:**
  ```awk
  awk '
  BEGIN { count=0 }
  /^[0-9]+:/ {
      ...
  }
  END { if (count == 0) print "  (nenhuma interface encontrada)" }
  ' /proc/net/dev
  ```
  O padrão `/^[0-9]+:/` foi escrito para o formato do comando `ip -o link show` (ex: `1: lo:...`). No entanto, o arquivo `/proc/net/dev` possui um cabeçalho formatado com espaços à esquerda (ex: `  wlan0: 1234 56...` ou `  usb0: 7890...`). A expressão regular `/^[0-9]+:/` nunca casa nenhuma linha do `/proc/net/dev`, resultando em 0 correspondências.
- **Solução Recomendada para o Futuro:** Alterar o seletor do `awk` para `/^[ ]*[a-zA-Z0-9_-]+:/` ou usar a saída do `ip -o link show`.

---

### 🟡 BUG-005: Exposição de Senha em Tabela de Processos via Pipe (`wpa_passphrase`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh#L53)
- **Gravidade:** 🟡 **Média (Segurança)**
- **Sintoma:** A senha da rede Wi-Fi fornecida como parâmetro para o script pode ser capturada por usuários não-privilegiados monitorando a árvore de processos.
- **Causa Raiz:**
  ```bash
  echo "$pass" | wpa_passphrase "$ssid"
  ```
  Embora passar a senha via pipe `echo` previna que ela apareça na linha de comando do `wpa_passphrase`, a variável `$pass` é recebida como o segundo argumento do próprio script `sanders-network-setup.sh wifi <SSID> <senha>`, permanecendo visível no `/proc/$PID/cmdline` de qualquer usuário durante a execução do comando.

---

### 🔵 BUG-006: Limitação de Throughput Wi-Fi (Trade-off 802.11n vs 802.11ac)
- **Arquivo:** [`kernel/0002-wcn36xx-force-v0-for-wcn3680.patch`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/0002-wcn36xx-force-v0-for-wcn3680.patch)
- **Gravidade:** 🔵 **Baixa (Informativa / Trade-off)**
- **Sintoma:** O Wi-Fi não atinge taxas de transferência VHT (802.11ac 5GHz), ficando limitado a no máximo ~150 Mbps (modo HT40).
- **Causa Raiz:** O patch força as estruturas de dados HAL para o formato `v0` (802.11n legada) quando o chip identificado é o `RF_IRIS_WCN3680`. Isso é necessário para evitar a rejeição de memória `MEM_FAIL=5` na firmware stock `1.5.1.2`, mas desativa a pilha VHT/802.11ac.

---

## 3. 🔵 Bluetooth e Integração de Hardware NV

### 🟠 BUG-007: Falha de Resolução do Ponto de Montagem `/dev/disk/by-partlabel/persist` em Boot Precoce
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-bt-mac.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-bt-mac.sh#L18-L33)
- **Gravidade:** 🟠 **Alta**
- **Sintoma:** O serviço `sanders-bt-mac.service` falha silenciosamente no boot e o Bluetooth permanece com o MAC aleatório temporário (`02:XX:XX:...`).
- **Causa Raiz:**
  ```bash
  PERSIST_DEV="/dev/disk/by-partlabel/persist"
  if [ ! -e "$PERSIST_DEV" ]; then
      echo "sanders-bt-mac: persist partition $PERSIST_DEV not found, skipping" >&2
      exit 0
  fi
  ```
  Se o serviço `sanders-bt-mac.service` for disparado antes que o `udevd` tenha terminado de criar os links simbólicos em `/dev/disk/by-partlabel/`, o teste `[ ! -e "$PERSIST_DEV" ]` retorna verdadeiro e o script encerra antecipadamente sem tentar ler a partição eMMC `/dev/mmcblk0p11` diretamente.
- **Solução Recomendada para o Futuro:** Adicionar retentativa de loop para o surgimento do nó `/dev/disk/by-partlabel/persist` ou usar fallback direto para `/dev/mmcblk0p11`.

---

### 🟡 BUG-008: Race Condition de Concorrência D-Bus entre `btmgmt` e `bluetoothd`
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service)
- **Gravidade:** 🟡 **Média**
- **Sintoma:** Erro de `Permission Denied` ou `Invalid Index` ao rodar `btmgmt public-addr`.
- **Causa Raiz:** O unit file especifica `Before=bluetooth.service`. No entanto, se o socket do BlueZ for ativado via D-Bus (`dbus-org.bluez.service`) por algum outro componente (ex: `bluetoothctl` ou `pipewire`), o daemon `bluetoothd` pode ser iniciado em paralelo com o `sanders-bt-mac.sh`. Quando o `bluetoothd` assume o controle do socket hci0, qualquer tentativa do `btmgmt` de alterar o endereço público é rejeitada.

---

## 4. 🐕 Kernel, Drivers e Hardware Watchdog

### 🟠 BUG-009: Risco de Reinício Forçado do SoC Durante o Estado de Suspensão (`s2idle`)
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf) / [`kernel/sanders.config.fragment`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/sanders.config.fragment#L206-L210)
- **Gravidade:** 🟠 **Alta (Estabilidade em Suspend)**
- **Sintoma:** O dispositivo reinicia sozinho após passar exatamente 30 segundos em modo de economia de energia (`s2idle` / suspend-to-RAM).
- **Causa Raiz:** O driver `qcom-wdt` ativa o registrador do temporizador de hardware da Qualcomm (`0x0b017000`). O `systemd` gerencia a alimentação periódica do watchdog via `RuntimeWatchdogSec=30s`. Quando o Linux entra em suspensão profunda (`s2idle`), a CPU congela a execução de todos os processos userspace (incluindo o systemd). Se o contador de hardware da Qualcomm continuar rodando no PMIC sem ser pingado durante a suspensão, o hardware dispara um reset físico do chip em 30s.
- **Solução Recomendada para o Futuro:** Implementar hook no Power Management (`PM_SUSPEND_PREPARE`) para desabilitar o temporizador de hardware antes do suspend.

---

### 🟡 BUG-010: Ausência de Driver APCS (CPU Clock Controller) Impede Operação Real do `cpufreq-dt`
- **Arquivo:** [`kernel/sanders.config.fragment`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/sanders.config.fragment#L179-L197) / [`dts/msm8953-motorola-sanders.dts`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/dts/msm8953-motorola-sanders.dts#L40)
- **Gravidade:** 🟡 **Média**
- **Sintoma:** O nó `/sys/devices/system/cpu/cpu0/cpufreq/` não é criado pelo kernel e o governor `schedutil` não tem efeito.
- **Causa Raiz:** As opções `CONFIG_CPU_FREQ_GOV_SCHEDUTIL=y` e `CONFIG_CPUFREQ_DT=y` foram habilitadas no Kconfig. Contudo, o driver de controle de clock dos núcleos Cortex-A53 (APCS do msm8953) não existe no código mainline atual. Sem um provedor de clock dinâmico amarrado aos núcleos no DT, o driver `cpufreq-dt` recusa o bind e os núcleos permanecem na frequência fixa do bootloader.

---

## 5. 🕒 Sincronismo de Tempo e Diagnósticos

### 🟡 BUG-011: Bloqueio do Script de NTP em Inicializações Lentas do DNS (`sanders-timesync.sh`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-timesync.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-timesync.sh#L10)
- **Gravidade:** 🟡 **Média**
- **Sintoma:** O relógio do sistema não sincroniza no primeiro boot se a rede demorar mais de 30s para obter resposta de DNS.
- **Causa Raiz:**
  ```bash
  if getent hosts a.st1.ntp.br >/dev/null 2>&1 || getent hosts pool.ntp.org >/dev/null 2>&1; then
  ```
  O script utiliza um loop de 30 retentativas com `getent hosts`. Se o serviço `systemd-resolved` ou o servidor DNS do roteador demorarem para responder, `getent` consome todo o loop e o script desiste de chamar `timedatectl set-ntp true`.

---

## 6. 📦 Scripts de Build e Gerenciamento de Pacotes

### 🟡 BUG-012: Risco de Partial Upgrade no Arch Linux (`sanders-server-setup.sh`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh#L79)
- **Gravidade:** 🟡 **Média (Sistema de Arquivos)**
- **Sintoma:** Mensagens de erro de bibliotecas não encontradas (`.so.X: cannot open shared object file`) ao executar utilitários recém-instalados.
- **Causa Raiz:** O script utiliza `pacman -Sy --noconfirm --needed ...`. No Arch Linux ARM, sincronizar as bases de dados remotas (`-Sy`) sem atualizar os pacotes já instalados no sistema (`-u`) pode instalar versões de softwares compiladas contra bibliotecas dinâmicas mais novas do que as presentes na imagem base.
- **Solução Recomendada para o Futuro:** Substituir por `pacman -Syu --noconfirm --needed`.

---

## 📊 Matriz Consolidada de Bugs e Gravidades

| ID | Componente | Descrição Resumida | Gravidade | Estado |
|---|---|---|:---:|:---:|
| **BUG-001** | `sanders-battery-guard.sh` | Falha de sintaxe bash se `capacity` sysfs retornar string vazia | 🟡 Média | Documentado |
| **BUG-002** | `sanders-battery-guard.sh` | Loop de log inútil em modo descarregamento (`STATUS != Charging`) | 🔵 Baixa | Documentado |
| **BUG-003** | DT / Kconfig PMI8996 | Charger e Fuel Gauge sem drivers C no kernel mainline | 🟠 Alta | Mapeado |
| **BUG-004** | `sanders-network-setup.sh` | Regex `/^[0-9]+:/` incorreta para parsing de `/proc/net/dev` | 🔴 Crítica | Documentado |
| **BUG-005** | `sanders-network-setup.sh` | Exposição de senha Wi-Fi em tabela de processos via `echo` | 🟡 Média | Documentado |
| **BUG-006** | `wcn36xx` Patch 0002 | Limitação de throughput Wi-Fi a taxas HT (802.11n) sem VHT | 🔵 Baixa | Decisão / Fix |
| **BUG-007** | `sanders-bt-mac.sh` | Falha de resolução do symlink `/dev/disk/by-partlabel/persist` | 🟠 Alta | Documentado |
| **BUG-008** | `sanders-bt-mac.service` | Race condition de D-Bus entre `btmgmt` e `bluetoothd` | 🟡 Média | Documentado |
| **BUG-009** | `qcom-wdt` / Systemd | Reset forçado do SoC durante o modo de suspensão de energia (`s2idle`) | 🟠 Alta | Documentado |
| **BUG-010** | Kconfig / DT APCS | Ausência de driver APCS impede funcionamento do `cpufreq-dt` | 🟡 Média | Mapeado |
| **BUG-011** | `sanders-timesync.sh` | Bloqueio por timeout se resolução DNS via `getent` for lenta | 🟡 Média | Documentado |
| **BUG-012** | `sanders-server-setup.sh` | Risco de *partial upgrade* no Arch Linux ao usar `pacman -Sy` | 🟡 Média | Documentado |

---
*Relatório de auditoria técnica enriquecido e revisado pelo Antigravity em 2026-09-02.*
