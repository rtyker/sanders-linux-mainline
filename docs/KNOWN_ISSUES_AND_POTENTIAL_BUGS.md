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
- **Arquivo:** [`kernel/0002-wcn36xx-wcn3680-novht-fallback.patch`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/0002-wcn36xx-wcn3680-novht-fallback.patch)
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

### ✅ BUG-008: Race Condition de Concorrência D-Bus entre `btmgmt` e `bluetoothd`
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service)
- **Gravidade:** 🟡 **Média**
- **Sintoma:** Erro de `Permission Denied` ou `Invalid Index` ao rodar `btmgmt public-addr`.
- **Causa Raiz:** O unit file especificava apenas `Before=bluetooth.service`. O BlueZ registra esse unit com `Alias=dbus-org.bluez.service` — se algo (`bluetoothctl`, `pipewire`, etc.) dispara ativação por D-Bus usando esse alias em vez do nome canônico, a resolução do systemd normalmente cai no mesmo unit, mas depender só do nome canônico deixava a ordenação implícita e sujeita a essa particularidade de resolução.
- **Status:** ✅ **Corrigido (2026-09-03):** adicionado `Before=dbus-org.bluez.service` explicitamente ao lado de `Before=bluetooth.service`, deixando a ordenação garantida independente de qual dos dois nomes disparar a ativação.

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
- **Status:** Parcialmente melhorado (2026-09-03) — a mensagem final agora reflete corretamente se o sync foi ou não confirmado (ver BUG-A3). O bloqueio de até 30s+15s continua existindo por design (é bounded, não indefinido, e o unit não é `Before=` de nada crítico do boot), mas fica mais transparente quando não conclui a tempo.

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

### ✅ BUG-A1: `CONFIG_I2C`/`CONFIG_I2C_QUP` não explicitados no fragment
- **Arquivo:** `kernel/sanders.config.fragment`
- **Gravidade:** 🟠 Alta (dependência condicional)
- **Status:** ✅ **Confirmado OK ao vivo (2026-09-03)** — `grep -E 'CONFIG_(I2C|I2C_QUP)=' build/linux/.config` retorna `CONFIG_I2C=y`/`CONFIG_I2C_QUP=y`, herdados do defconfig arm64 padrão. Não é bug real, o comportamento já era o esperado — a suspeita do relatório original foi descartada com evidência direta do `.config` do build atual.

### ✅ BUG-A2: `mkswap`/`swapon` incondicionais após cadeia `zramctl` (`sanders-zram.sh`)
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-zram.sh:24-34`
- **Gravidade:** 🟡 Média
- **Status:** ✅ **Corrigido (2026-09-03):** removido o `mkswap`/`swapon` de dentro de cada ramo (zramctl e sysfs), adicionada checagem explícita `[ -b /dev/zram0 ]` com mensagem de erro clara antes de seguir pro `mkswap`/`swapon`, em vez de depender do comportamento implícito do `set -e` no fim da cadeia `||`.

### ✅ BUG-A3: timesync imprime "Relógio ajustado" mesmo se sync não confirmou (`sanders-timesync.sh`)
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-timesync.sh:33`
- **Gravidade:** 🟡 Média (mensagem enganosa)
- **Status:** ✅ **Corrigido (2026-09-03):** adicionada flag `SYNCED` explícita; a mensagem de sucesso só é impressa se `NTPSynchronized=yes` foi de fato confirmado dentro dos 15s, caso contrário imprime um `WARN` claro em vez da mensagem de sucesso enganosa.

### ✅ BUG-A4: parsing frágil de `timedatectl` no timesync
- **Arquivo:** `sanders-timesync.sh:28`
- **Gravidade:** 🔵 Baixa
- **Status:** ✅ **Corrigido (2026-09-03):** trocado o parsing de texto de `timedatectl status` (formato pode variar entre versões do systemd) por `timedatectl show -p NTPSynchronized --value`, saída machine-readable estável (`yes`/`no`).

### ✅ BUG-A5: `01-build-lk2nd.sh` clona completo (sem `--depth=1`) e usa fallback HEAD
- **Arquivo:** `scripts/01-build-lk2nd.sh:10,14`
- **Gravidade:** 🔵 Baixa (desempenho/robustez)
- **Status:** ✅ **Melhorado (2026-09-03):** agora tenta `git fetch --depth 1 origin $LK2ND_COMMIT` (shallow fetch por SHA fixo — GitHub suporta isso) antes de cair no clone completo como fallback. O fallback com `warn` pro HEAD, se o SHA realmente não existir no remoto, foi mantido (comportamento aceitável, já sinalizado).

### 🔴 BUG-A6: `lib.sh` cmdline `earlycon` sem MMIO
- **Arquivo:** `scripts/lib.sh:73`
- **Gravidade:** 🔵 Baixa (era) → tentativa de fix causou regressão 🔴 Crítica
- **Status:** ❌ **Tentativa de fix revertida (2026-09-03) — NÃO REPETIR sem investigar a fundo antes.**
- **Descrição:** `earlycon` simples só ativa via match de `OF_EARLYCON_DECLARE` contra
  `chosen/stdout-path`; sem essa propriedade ele fica mudo no early boot (comportamento
  original, sem risco).
- **Tentativa de fix (2026-09-03):** adicionado `aliases { serial0 = &uart_0; }` +
  `chosen { stdout-path = "serial0:115200n8"; }` no DTS, apontando pro mesmo UART do
  console principal (`uart_0` @ 0x78af000, compatible `qcom,msm-uartdm`, mesmo device de
  `ttyMSM0`). DTB compilou limpo e a resolução do alias ficou correta (`dtc -O dts`
  confirmou `/soc@0/serial@78af000`). **Testado ao vivo: causou boot hang reproduzível** —
  o dmesg via tela HDMI/framebuffer parava logo após
  `[drm] Initialized simpledrm 1.0.0 for 90001000.frame`, sem UART/USB gadget subindo,
  sem SSH, sem prompt. Reproduzido em 2 boots consecutivos (incluindo um cold-boot físico
  após hard power-cycle). Causa provável: conflito de `earlycon` com o driver real
  `msm_serial` (`OF_EARLYCON_DECLARE`) disputando a mesma região MMIO do UART — mas não
  foi confirmado a fundo, só a correlação direta com a mudança (revertida = boot normal
  de novo, confirmado 2x).
- **Recuperação:** DTS revertido pro estado original (sem `aliases`/`stdout-path`),
  recompilado, gravado via `fastboot flash cache` (não dependeu do Linux estar de pé —
  crítico já que o device não respondia nem por serial nem USB gadget nesse estado).
  Boot voltou ao normal, confirmado ao vivo.
- **Para o futuro:** se alguém quiser reabrir isso, investigar primeiro se `uart_0` tem
  suporte real a `earlycon` simultâneo com o console tardio nesse SoC (alguns UARTs MSM
  exigem `no_console_suspend`/handoff explícito entre earlycon e o driver real — ver
  `Documentation/admin-guide/kernel-parameters.txt` sobre `earlycon` + conflito de
  ownership de MMIO), e testar em ambiente onde reverter não dependa de fastboot físico.

### 🔵 BUG-A7: `msm8953-motorola-sanders.dts` — `ts_reset` pinctrl definido mas não referenciado
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:413`
- **Gravidade:** 🔵 Baixa
- **Status:** 🔄 Aceito (design intencional) — o reset do touchscreen foi movido para
  `gpio-hog` (sempre HIGH) porque o pulse curto do driver causava `-ETIMEDOUT` (comentado
  no DTS). O pinctrl `ts-reset-state` ficou como código morto/documentação.

### ✅ BUG-A8: gap de numeração de patches (0001, 0002, 0004 — sem 0003)
- **Arquivo:** `kernel/`
- **Gravidade:** 🔵 Baixa (cosmético)
- **Status:** ✅ **Corrigido (2026-09-03):** `0004-pmi8950-battery-charger-nodes.patch` renomeado para `0003-pmi8950-battery-charger-nodes.patch` (`git mv`, conteúdo idêntico — a checagem de idempotência do `02-build-kernel.sh` é por conteúdo via `git apply --check --reverse`, não por nome, então renomear não afeta a aplicação num tree já com o patch aplicado). Referência atualizada em `docs/ROADMAP_AND_TODOS.md`.

### 🔄 BUG-A9: `sanders-bt-mac.sh` — `set +e` global nunca restaurado
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-bt-mac.sh:95`
- **Gravidade:** 🟡 Média
- **Status:** 🔄 Aceito (design) — todos os paths de saída usam `exit` explícito e há
  validação final com retry; falhas em `power off`/`public-addr`/`power on` são ignoradas
  propositalmente (btmgmt às vezes reporta erro mesmo aplicando). Não quebra o fluxo.

### ✅ BUG-A10: `09-extract-firmware.sh` — path do stock zip ainda com fallback hardcoded
- **Arquivo:** `scripts/09-extract-firmware.sh:20-24`
- **Gravidade:** 🔵 Baixa
- **Status:** ✅ **Corrigido (2026-09-03):** fallback trocado de path absoluto fixo (`/mnt/hdauxiliar/android/projeto_g5/stock/`) para `$REPO/../stock` (relativo ao submódulo via `lib.sh`), portável para qualquer clone do projeto que mantenha a mesma estrutura pai/submódulo. Override via `STOCK_ZIP` continua disponível.

### 🟠 BUG-013: Ciclos de ordenação systemd em todo boot — jobs apagados (`sanders-zram`) e `sanders-player` que nunca subia
- **Arquivos:** `rootfs-overlay/common/etc/systemd/system/sanders-zram.service`, `sanders-cpufreq.service` e a unit `sanders-volume-keys.service` gerada por `usr/local/bin/sanders-flavor-install.sh`
- **Gravidade:** 🟠 Alta
- **Status:** ✅ **Corrigido e confirmado ao vivo (2026-09-10, BF)**
- **Descoberta:** incidental, durante a verificação de outro item do backlog — o `dmesg` do boot do kernel novo (DTS sem o nó de LED) mostrava `Found ordering cycle` + `Job ... deleted to break ordering cycle` em **todo boot**.
- **Sintoma:** dois ciclos independentes:
  1. `local-fs.target → sanders-zram.service → swap.target → var-log.mount/tmp.mount → local-fs` — o systemd apagava o job do zram (e em alguns boots os de `tmp.mount`/`var-log.mount`) pra quebrar o anel.
  2. `multi-user.target → sanders-player.service → sanders-volume-keys.service → multi-user.target` — o job do **player GTK4 era apagado em todo boot: o serviço nunca chegou a iniciar sozinho** (ficava `inactive` para sempre).
- **Causa raiz (dois padrões distintos):**
  1. `sanders-volume-keys.service` e `sanders-cpufreq.service` combinavam `After=multi-user.target` com `[Install] WantedBy=multi-user.target`. O want cria o edge reverso (`multi-user.target` After= serviço); com o `After=` explícito na direção oposta, fecha ciclo. Como `sanders-player.service` é `After=sanders-volume-keys.service` e também wanted pelo target, o job dele era arrastado junto.
  2. `sanders-zram.service` tinha `After=local-fs.target` + `Before=swap.target`; `swap.target` é Before= dos mounts tmpfs do fstab (`/tmp`, `/var/log`), que são Before= de `local-fs.target` → anel.
- **Fix aplicado (repo + device):**
  1. `sanders-volume-keys.service`/`sanders-cpufreq.service`: `After=systemd-user-sessions.service` no lugar de `multi-user.target` (a dependência real é a sessão de usuário/PipeWire, não o target que é pai deles).
  2. `sanders-zram.service`: repensado como unidade de early-boot — `WantedBy=swap.target` (antes: `multi-user.target`), `DefaultDependencies=no`, `After=systemd-udevd.service`, `Before=swap.target` (mesmo padrão das units de swap geradas pelo próprio systemd; o `DefaultDependencies=no` é obrigatório porque o `After=sysinit.target` implícito de services fecharia novo anel com `swap.target` Before= sysinit). `systemctl reenable sanders-zram.service` no device pra trocar o symlink de wants.
- **Validação:** `systemd-analyze verify` nas 7 units envolvidas → rc=0 (antes: dezenas de linhas de ciclo); reboot ao vivo com **0** ocorrências de "ordering cycle" no dmesg; `sanders-player.service` **active** pela primeira vez; zram swap ativo (889M, prio 100); `systemctl --failed` vazio.
- **Lição pro projeto:** serviço `WantedBy=X.target` **nunca** deve declarar `After=X.target`; e unidades que preparam swap antes do sysinit precisam de `DefaultDependencies=no`.

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
| **BUG-008** | `sanders-bt-mac.service` | Race condition de D-Bus entre `btmgmt` e `bluetoothd` | 🟡 Média | ✅ **Corrigido** (2026-09-03) |
| **BUG-009** | `qcom-wdt` / Systemd | Reset forçado do SoC durante o modo de suspensão de energia (`s2idle`) | 🟠 Alta | ✅ **Corrigido, confirmado ao vivo** (2026-09-02) |
| **BUG-010** | Kconfig / DT APCS | Ausência de driver APCS impede funcionamento do `cpufreq-dt` | 🟡 Média | Mapeado (Scaffolding) |
| **BUG-011** | `sanders-timesync.sh` | Bloqueio por timeout se resolução DNS via `getent` for lenta | 🟡 Média | Melhorado (mensagem correta) — bloqueio bounded é aceito por design |
| **BUG-012** | `sanders-server-setup.sh` | Risco de *partial upgrade* no Arch Linux ao usar `pacman -Sy` | 🟡 Média | ✅ **Corrigido** (2026-09-02) |
| **BUG-013** | systemd units (`sanders-zram`/`volume-keys`/`cpufreq`) | Ciclos de ordenação apagavam jobs em todo boot — `sanders-player` nunca iniciava | 🟠 Alta | ✅ **Corrigido, confirmado ao vivo** (2026-09-10, BF) |

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
*Relatório de auditoria técnica corrigido e atualizado pelo Antigravity em 2026-09-02. Verificação ao vivo e correção do BUG-005 por Claude em 2026-09-02. Revisão adicional em 2026-09-02: reincorporação dos itens abertos (BUG-A1..A10) omitidos pela reindexação de 12 itens, com estado validado contra o código real. Descoberta, correção e validação ao vivo do BUG-013 por **BF** em 2026-09-10. Nenhum fonte foi modificado nesta revisão — apenas este documento.*

