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

### ✅ BUG-005: Exposição de Senha em Tabela de Processos via Pipe (`wpa_passphrase`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh#L53)
- **Gravidade:** 🟡 **Média (Segurança)**
- **Status:** ✅ **Corrigido 2026-09-02.** Achado real e mais fundo do que a correção anterior desta mesma sessão cobria: eu já tinha trocado `wpa_passphrase "$ssid" "$pass"` por `echo "$pass" | wpa_passphrase "$ssid"`, mas isso só evita a senha aparecer no argv do *subprocesso* `wpa_passphrase` — a variável `$pass` continua vindo como argumento do próprio `sanders-network-setup.sh wifi <SSID> <senha>`, visível em `/proc/$PID/cmdline` enquanto o script roda.
- **Correção:** adicionado suporte a `wifi <SSID> -` — o `-` no lugar da senha faz o script pedir via `read -rs` (prompt oculto, nunca toca argv/journal). Documentado no uso/ajuda como forma recomendada; a forma antiga (senha como argumento) continua funcionando por compatibilidade, mas com aviso explícito no cabeçalho do script sobre a exposição.

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

### ✅ BUG-009: Risco de Reinício Forçado do SoC Durante o Estado de Suspensão (`s2idle`)
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf) / [`kernel/sanders.config.fragment`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/sanders.config.fragment#L206-L210)
- **Gravidade:** 🟠 **Alta (Estabilidade em Suspend)**
- **Status:** ✅ **Corrigido — confirmado AO VIVO em 2026-09-02.** Esta tabela dizia "Documentado" mas o fix já existe (`rootfs-overlay/common/etc/systemd/sleep.conf.d/10-disable-sleep.conf`, `AllowSuspend=no` e afins) e testei direto no device recém-flashado: `systemctl suspend` retorna `Call to Suspend failed: Sleep verb 'suspend' is disabled by config` (exit 1). Bloqueado na raiz — nenhum caminho pra suspend (botão de power, bateria fraca, comando manual) funciona, então o cenário do watchdog nunca chega a acontecer.
- **Causa Raiz (histórica):** O driver `qcom-wdt` ativa o registrador do temporizador de hardware da Qualcomm (`0x0b017000`). O `systemd` gerencia a alimentação periódica do watchdog via `RuntimeWatchdogSec=30s`. Quando o Linux entra em suspensão profunda (`s2idle`), a CPU congela a execução de todos os processos userspace (incluindo o systemd). Se o contador de hardware da Qualcomm continuar rodando no PMIC sem ser pingado durante a suspensão, o hardware dispara um reset físico do chip em 30s.

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

## 🧨 Bugs adicionais revalidados (não cobertos pela reindexação de 12 itens)

> A reindexação de 12 bugs acima omitiu vários achados reais da auditoria v1 (que
> registrava até 56 itens). Foi feita nova verificação contra o código atual em
> **2026-09-02** e os itens que **continuam abertos** são reincorporados aqui. Itens v1 que
> já foram corrigidos em commits recentes **não** são repetidos (estão resolvidos). Status:
> 🟠 Alta / 🟡 Média / 🔵 Baixa / 🔄 Aceito (design).

### 🟠 BUG-A1: `CONFIG_I2C`/`CONFIG_I2C_QUP` não explicitados no fragment
- **Arquivo:** `kernel/sanders.config.fragment`
- **Gravidade:** 🟠 Alta (dependência condicional)
- **Status:** Aberto
- **Descrição:** O fragment habilita `CONFIG_TOUCHSCREEN_EDT_FT5X06=y` (i2c_3 @0x38) e
  `CONFIG_LTR501=y` (i2c_7 @0x23), **mas não força `CONFIG_I2C`/`CONFIG_I2C_QUP` builtin**
  (grep confirma: ausente do arquivo). Mesmo raciocínio do `CONFIG_PHY_QCOM_QUSB2` (forçar
  `=y` porque não há modprobe no initramfs) deveria valer pro I2C. **Nota:** o `defconfig`
  arm64 usual já traz `CONFIG_I2C=y`/`CONFIG_I2C_QUP=y`, então pode já estar OK — **a
  confirmar no `.config` real do build** (ex.: `grep -E 'CONFIG_(I2C|I2C_QUP)=' build/linux/.config`).

### 🟡 BUG-A2: `mkswap`/`swapon` incondicionais após cadeia `zramctl` (`sanders-zram.sh`)
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-zram.sh:24-34`
- **Gravidade:** 🟡 Média
- **Status:** Aberto
- **Descrição:** a cadeia `zramctl ... || zramctl ... || zramctl ...` seguida de `mkswap`/
  `swapon` rodam **incondicionalmente**. Se **todos** os `zramctl` falharem, `mkswap` tenta
  em `/dev/zram0` inexistente (sob `set -euo pipefail` isso mata o script, sem mensagem
  clara). Além disso, `--algorithm` só existe em `zramctl` >= 2.39 (tem fallback sem flag,
  então mitigado). **Melhorar:** checar retorno da cadeia antes do `mkswap`.

### 🟡 BUG-A3: timesync imprime "Relógio ajustado" mesmo se sync não confirmou (`sanders-timesync.sh`)
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-timesync.sh:33`
- **Gravidade:** 🟡 Média (mensagem enganosa)
- **Status:** Aberto
- **Descrição:** o loop espera até 15s por `System clock synchronized: yes`; se não
  confirmar em 15s, **ainda** imprime `Relógio ajustado: $NOW`. O caminho de erro
  ("inalcançáveis") só cobre quando o DNS nunca resolveu. **Sugestão:** flag de sucesso do
  `SYNCED` para só imprimir sucesso de fato.

### 🔵 BUG-A4: parsing frágil de `timedatectl` no timesync
- **Arquivo:** `sanders-timesync.sh:28`
- **Gravidade:** 🔵 Baixa
- **Status:** Aberto
- **Descrição:** procura string exata `System clock synchronized: yes`; formato pode
  variar entre versões do systemd.

### 🔵 BUG-A5: `01-build-lk2nd.sh` clona completo (sem `--depth=1`) e usa fallback HEAD
- **Arquivo:** `scripts/01-build-lk2nd.sh:10,14`
- **Gravidade:** 🔵 Baixa (desempenho/robustez)
- **Status:** Aberto
- **Descrição:** `git clone` sem shallow; e se o commit `c8b47cd` não existir, `warn` mas
  continua com HEAD → build não testado. **Nota:** já avisa via `warn` (mitigado).

### 🔵 BUG-A6: `lib.sh` cmdline `earlycon` sem MMIO
- **Arquivo:** `scripts/lib.sh:73`
- **Gravidade:** 🔵 Baixa
- **Status:** Aberto
- **Descrição:** `earlycon` simples depende do console configurado; pode falhar
  silenciosamente no early boot.

### 🔵 BUG-A7: `msm8953-motorola-sanders.dts` — `ts_reset` pinctrl definido mas não referenciado
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:413`
- **Gravidade:** 🔵 Baixa
- **Status:** 🔄 Aceito (design intencional) — o reset do touchscreen foi movido para
  `gpio-hog` (sempre HIGH) porque o pulse curto do driver causava `-ETIMEDOUT` (comentado
  no DTS). O pinctrl `ts-reset-state` ficou como código morto/documentação.

### 🔵 BUG-A8: gap de numeração de patches (0001, 0002, 0004 — sem 0003)
- **Arquivo:** `kernel/`
- **Gravidade:** 🔵 Baixa (cosmético)
- **Status:** Aberto
- **Descrição:** há 0001, 0002 e 0004; `02-build-kernel.sh` itera `000[0-9]*` então não
  quebra, mas o gap confunde.

### 🔄 BUG-A9: `sanders-bt-mac.sh` — `set +e` global nunca restaurado
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-bt-mac.sh:95`
- **Gravidade:** 🟡 Média
- **Status:** 🔄 Aceito (design) — todos os paths de saída usam `exit` explícito e há
  validação final com retry; falhas em `power off`/`public-addr`/`power on` são ignoradas
  propositalmente (btmgmt às vezes reporta erro mesmo aplicando). Não quebra o fluxo.

### 🔵 BUG-A10: `09-extract-firmware.sh` — path do stock zip ainda com fallback hardcoded
- **Arquivo:** `scripts/09-extract-firmware.sh:20-24`
- **Gravidade:** 🔵 Baixa
- **Status:** Aberto (parcialmente mitigado) — suporta override via `STOCK_ZIP` env, mas o
  fallback default ainda é `/mnt/hdauxiliar/android/projeto_g5/stock/*.zip` (máquina do dev).

---

## 📊 Matriz Consolidada de Bugs e Gravidades

| ID | Componente | Descrição Resumida | Gravidade | Estado |
|---|---|---|:---:|:---:|
| **BUG-001** | `sanders-battery-guard.sh` | Falha de sintaxe bash se `capacity` sysfs retornar string vazia | 🟡 Média | ✅ **Corrigido** (2026-09-02) |
| **BUG-002** | `sanders-battery-guard.sh` | Loop de log inútil em modo descarregamento (`STATUS != Charging`) | 🔵 Baixa | ✅ **Corrigido** (2026-09-02) |
| **BUG-003** | DT / Kconfig PMI8996 | Charger e Fuel Gauge sem drivers C no kernel mainline | 🟠 Alta | Mapeado (Scaffolding) |
| **BUG-004** | `sanders-network-setup.sh` | Regex `/^[0-9]+:/` incorreta para parsing de `/proc/net/dev` | 🔴 Crítica | ✅ **Corrigido** (2026-09-02) |
| **BUG-005** | `sanders-network-setup.sh` | Exposição de senha Wi-Fi em tabela de processos via `echo` | 🟡 Média | ✅ **Corrigido** (2026-09-02) — `wifi <SSID> -` pede senha via prompt oculto |
| **BUG-006** | `wcn36xx` Patch 0002 | Limitação de throughput Wi-Fi a taxas HT (802.11n) sem VHT | 🔵 Baixa | Decisão / Fix |
| **BUG-007** | `sanders-bt-mac.sh` | Falha de resolução do symlink `/dev/disk/by-partlabel/persist` | 🟠 Alta | ✅ **Corrigido** (2026-09-02) |
| **BUG-008** | `sanders-bt-mac.service` | Race condition de D-Bus entre `btmgmt` e `bluetoothd` | 🟡 Média | Documentado |
| **BUG-009** | `qcom-wdt` / Systemd | Reset forçado do SoC durante o modo de suspensão de energia (`s2idle`) | 🟠 Alta | ✅ **Corrigido, confirmado ao vivo** (2026-09-02) |
| **BUG-010** | Kconfig / DT APCS | Ausência de driver APCS impede funcionamento do `cpufreq-dt` | 🟡 Média | Mapeado (Scaffolding) |
| **BUG-011** | `sanders-timesync.sh` | Bloqueio por timeout se resolução DNS via `getent` for lenta | 🟡 Média | Documentado |
| **BUG-012** | `sanders-server-setup.sh` | Risco de *partial upgrade* no Arch Linux ao usar `pacman -Sy` | 🟡 Média | ✅ **Corrigido** (2026-09-02) |

---

## ✅ Verificação ao vivo pós-flash (Claude, 2026-09-02)

Depois de um reflash da rootfs (corrigiu uma corrupção ext4 não relacionada a este documento) e boot real via `07-boot-kernel.sh`, testei o que dava pra testar direto no device — não só lido, rodado:

- **BUG-009 (watchdog+suspend):** confirmado corrigido — `systemctl suspend` recusa (ver acima).
- **Watchdog:** `/dev/watchdog` e `/dev/watchdog0` existem.
- **SSH hardening:** `permitrootlogin prohibit-password`, `passwordauthentication no`, `kbdinteractiveauthentication no` (nome novo da diretiva).
- **zram:** `/dev/zram0` ativo como swap.
- **Telemetria térmica:** 9 zonas lendo valores sãos.
- **LED de status:** detecta rede corretamente.
- **cpufreq:** confirmado ainda não-funcional (BUG-010) — esperado, sem regressão.
- Corrigido também: BUG-004 e BUG-007 confirmados intactos no código; BUG-005 corrigido nesta sessão (prompt oculto pra senha Wi-Fi); BUG-012 (`pacman -Syu`) confirmado nos 3 pontos do código.
- **Não testado neste boot:** Docker, restauração de MAC do Bluetooth, fonte do console (`vconsole`) — o build usado não tinha os pacotes extras instalados (docker, bluez-utils, terminus-font). Não é regressão de nenhum fix desta sessão, é ausência de pacote; revalidar quando esses pacotes forem instalados.

---
*Relatório de auditoria técnica corrigido e atualizado pelo Antigravity em 2026-09-02. Verificação ao vivo e correção do BUG-005 por Claude em 2026-09-02. Revisão adicional em 2026-09-02: reincorporação dos itens abertos (BUG-A1..A10) omitidos pela reindexação de 12 itens, com estado validado contra o código real. Nenhum fonte foi modificado nesta revisão — apenas este documento.*

