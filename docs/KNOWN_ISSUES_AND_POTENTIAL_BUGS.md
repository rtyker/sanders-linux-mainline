# ⚠️ Relatório de Revisão Técnica: Bugs Potenciais e Casos de Borda (Audit 2026-09-02)

Este documento registra a auditoria técnica de código, scripts, devicetree e configurações de kernel realizadas em **2026-09-02**. 

> ℹ️ **Nota para Agentes e Desenvolvedores:** Estes itens foram identificados durante a revisão estática de código e simulações de boot. **Eles não foram corrigidos propositalmente** nesta sessão para preservação de estado e documentação de frentes futuras.

---

## 1. 🔋 Proteção de Bateria (`sanders-battery-guard.sh`)

### Item 1.1: Truncamento ou Retorno Vazio na Leitura de Capacidade Sysfs
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh)
- **Localização:** Linhas 13-14
- **Descrição do Bug Potencial:**
  ```bash
  CAP=$(awk '{print $1}' "$BAT_SYS/capacity" 2>/dev/null || echo "0")
  ```
  Se o arquivo `/sys/class/power_supply/battery/capacity` existir no sysfs mas o driver do fuel gauge ainda não tiver inicializado no boot (retornando uma string vazia ou erro de I/O em vez de um inteiro), `CAP` pode ser atribuído como string vazia `""`.
- **Impacto:** A avaliação condicional do bash `[ "$CAP" -ge "$HIGH_LIMIT" ]` irá falhar com o erro `integer expression expected` no terminal/journalctl.
- **Sugestão de Fix Futuro:** Utilizar fallback explícito de valor padrão via `awk`:
  ```bash
  CAP=$(awk '{print ($1 ~ /^[0-9]+$/ ? $1 : 0)}' "$BAT_SYS/capacity" 2>/dev/null || echo "0")
  ```

### Item 1.2: Conflito de Status de Carga (`Status != Charging`)
- **Arquivo:** `sanders-battery-guard.sh`
- **Descrição do Bug Potencial:** Em kernels Qualcomm, o nó `/sys/class/power_supply/battery/status` pode reportar estados como `"Not charging"`, `"Full"` ou `"Discharging"`. Quando o cabo de alimentação estiver desconectado e o nível de carga cair abaixo de 70% (`LOW_LIMIT`), o script tentará escrever `1` no seletor `/sys/class/power_supply/battery/online` sem verificar se a fonte externa USB está fisicamente conectada (`/sys/class/power_supply/usb/online`).
- **Impacto:** Tentativa inútil de ativação do carregador em modo bateria.

---

## 2. 🕒 Sincronismo de Relógio (`sanders-timesync.sh`)

### Item 2.1: Dependência de Resolução DNS em Ambientes com `systemd-resolved` Retardado
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-timesync.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-timesync.sh)
- **Localização:** Linhas 10-15
- **Descrição do Bug Potencial:**
  ```bash
  if getent hosts a.st1.ntp.br >/dev/null 2>&1 || getent hosts pool.ntp.org >/dev/null 2>&1; then
  ```
  O utilitário `getent hosts` consulta os bancos do `nsswitch.conf`. Se a interface de rede obter IP via DHCP mas o serviço `systemd-resolved` ou a substituição de `/etc/resolv.conf` ainda estiver em progresso, `getent` falhará e esgotará a janela de 30 segundos, ignorando a sincronização NTP no primeiro boot.
- **Impacto:** Atraso no alinhamento do relógio no primeiro boot.

### Item 2.2: Parsing do Status de Sincronismo no `timedatectl`
- **Arquivo:** `sanders-timesync.sh`
- **Localização:** Linhas 24-27
- **Descrição do Bug Potencial:** O parsing via `awk` procura exatamente pela string `"System clock synchronized: yes"`. Em versões mais recentes do systemd ou em distribuições com suporte reduzido ao D-Bus `systemd-timedated`, essa linha pode aparecer formatada como `"NTP service: active"`.

---

## 3. 🐕 Hardware Watchdog & Suspend (`qcom-wdt` + Systemd)

### Item 3.1: Reset de Hardware Não Pretendido Durante Suspend (S2idle / Deep Sleep)
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf)
- **Configuração:** `RuntimeWatchdogSec=30s`
- **Descrição do Bug Potencial:** O driver `qcom-wdt` mapeia o temporizador físico da Qualcomm (`0x0b017000`). Quando o sistema entra em modo de suspensão de energia (`s2idle` / suspend-to-RAM), o daemon do `systemd` é pausado e para de enviar pings ao `/dev/watchdog0`. Se o temporizador de hardware da Qualcomm continuar contando no registrador de hardware durante a suspensão do SoC sem ser resetado pelo PMIC/RPM, o watchdog irá disparar um reset forçado do aparelho após 30 segundos de sleep.
- **Impacto:** O celular pode reiniciar espontaneamente toda vez que tentar entrar em estado de economia de energia.
- **Sugestão de Fix Futuro:** Desativar temporariamente o watchdog no suspend handler do PM / kernel ou usar `ShutdownWatchdogSec` em vez de `RuntimeWatchdogSec` se suspend for utilizado.

---

## 4. 🔵 Restauração de Bluetooth MAC (`sanders-bt-mac.sh` & `sanders-bt-mac.service`)

### Item 4.1: Concorrência com o Daemon `bluetoothd` (BlueZ)
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service)
- **Descrição do Bug Potencial:** O unit file possui `Before=bluetooth.service`. Porém, se o `bluetoothd` for iniciado manualmente ou via D-Bus activation (`dbus-org.bluez.service`) enquanto o `sanders-bt-mac.sh` estiver executando `btmgmt public-addr "$MAC"`, o `bluetoothd` assumirá o controle do socket de gerenciamento do BlueZ e retornará `Invalid Index` ou `Permission Denied` para o `btmgmt`, impedindo a alteração do endereço MAC.

---

## 5. 🛠️ Utilitário de Setup do Servidor (`sanders-server-setup.sh`)

### Item 5.1: Risco de Atualização Parcial no Arch Linux (`pacman -Sy`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh)
- **Localização:** Linha 79
- **Descrição do Bug Potencial:**
  ```bash
  pacman -Sy --noconfirm --needed htop tmux git curl neofetch podman docker bluez-utils
  ```
  No Arch Linux, executar `pacman -Sy` (sincronizar bases de dados sem atualizar os pacotes do sistema `-u`) é considerado uma má prática que pode resultar em *partial upgrade* (quebra de dependências `.so` se o repositório remoto contiver uma versão de biblioteca glibc/openssl mais recente do que a instalada no rootfs).
- **Sugestão de Fix Futuro:** Alterar para `pacman -Syu --noconfirm --needed`.

---

## 6. 📶 Patch Wi-Fi WPA2 (`0002-wcn36xx-force-v0-for-wcn3680.patch`)

### Item 6.1: Limitação de Velocidade 802.11ac (5GHz HT vs VHT)
- **Arquivo:** [`kernel/0002-wcn36xx-force-v0-for-wcn3680.patch`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/0002-wcn36xx-force-v0-for-wcn3680.patch)
- **Descrição do Bug Potencial / Trade-off:** O patch força todas as conexões no chip `RF_IRIS_WCN3680` a utilizar as mensagens de configuração de BSS/STA da **API v0 (802.11n / HT)**. Embora isso contorne com sucesso o erro `MEM_FAIL=5` na firmware stock `1.5.1.2`, ele impede o uso de taxas VHT (802.11ac 5GHz), limitando a velocidade máxima teórica do Wi-Fi a ~150 Mbps (HT40).

---

## 📊 Tabela Resumo de Bugs Potenciais Identificados

| ID | Componente | Descrição Resumida | Gravidade Potencial | Status (revisão 2026-09-02) |
|---|---|---|:---:|---|
| **BUG-01** | `sanders-battery-guard.sh` | Falha de sintaxe bash se `capacity` sysfs retornar string vazia | Média | Não corrigido (escopo do Battery Guard, ficou com o antigravity) |
| **BUG-02** | `sanders-timesync.sh` | Latência/falha de resolução DNS em boots com `systemd-resolved` em inicialização | Baixa | Não é bug — o script já retry por até 30s exatamente pra cobrir essa janela |
| **BUG-03** | `qcom-wdt` / `10-watchdog.conf` | Risco de reset forçado do SoC durante o modo de suspensão de energia (S2idle) | Alta (Se suspend ativo) | ✅ Corrigido 2026-09-02: `rootfs-overlay/common/etc/systemd/sleep.conf.d/10-disable-sleep.conf` desabilita suspend/hibernate/hybrid-sleep na raiz (`AllowSuspend=no` etc) — servidor headless não tem motivo pra suspender, e isso fecha o risco por completo, não só mitiga |
| **BUG-04** | `sanders-bt-mac.service` | Race condition se `bluetoothd` for ativado via D-Bus antes da alteração do MAC | Média | Não corrigido — `bluetooth.service` não está habilitado em lugar nenhum do rootfs (só ativaria via D-Bus se algo chamasse a API do BlueZ manualmente), risco baixo na prática hoje |
| **BUG-05** | `sanders-server-setup.sh` | Risco de *partial upgrade* ao utilizar `pacman -Sy` no Arch Linux | Média | ✅ Corrigido 2026-09-02: trocado `pacman -Sy` → `pacman -Syu` em `sanders-server-setup.sh` e nos dois pontos de `05-build-rootfs.sh` |
| **BUG-06** | `wcn36xx` Patch 0002 | Trade-off de throughput: limita Wi-Fi WCN3680 a taxas HT (802.11n) sem VHT | Baixa (Informativa) | Trade-off aceito, não é bug |

---
*Relatório de auditoria gerado pelo Antigravity em 2026-09-02. Itens revisados e parcialmente corrigidos por Claude em 2026-09-02.*
