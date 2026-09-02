# ⚠️ Conhecimento Consolidado: Issues, Bugs Potenciais e Estado (Audit 2026-09-02)

> **Documento único de referência de bugs/issues do projeto.**
> Re-revisado em 2026-09-02 contra o estado real dos fontes **após** as correções de um agente anterior. Cada item foi re-validado lendo o código atual (não só o histórico). Estado marcado como: ✅ corrigido / 🔄 aceito (design intencional) / 🔴 aberto crítico / 🟠 aberto alto / 🟡 aberto médio / 🔵 aberto baixo.

---

## 📋 Como ler este documento

- **🔴 Crítico** — risco de instabilidade/corrupção ou quebra de build.
- **🟠 Alto** — risco real de mau funcionamento em cenário comum.
- **🟡 Médio** — problema real mas de baixo impacto ou cenário específico.
- **🔵 Baixo** — cosmético/informativo, melhoria.
- **✅ Corrigido** — já resolvido pelo agente anterior (mantido para histórico).
- **🔄 Aceito** — não é bug; comportamento intencional/documentado.

---

## 1. 🔋 Proteção de Bateria (`sanders-battery-guard.sh`)

### Item 1.1 🟡 BUG-001: Truncamento ou Retorno Vazio na Leitura de Capacidade Sysfs
- **Arquivo:** `sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh:13`
- **Status:** 🟡 Aberto
- **Descrição:** `CAP=$(awk '{print $1}' "$BAT_SYS/capacity" 2>/dev/null || echo "0")`. Se o arquivo `capacity` existir mas o driver não tiver inicializado (string vazia / I/O), `awk` retorna vazio com exit 0 — o `|| echo "0"` não dispara e `$CAP` fica vazio.
- **Impacto:** `[ "$CAP" -ge "$HIGH_LIMIT" ]` → `integer expression expected`.
- **Fix sugerido (não aplicado):** `CAP=$(awk '{print ($1 ~ /^[0-9]+$/ ? $1 : 0)}' ...)`.

### Item 1.2 🔵 BUG-002: Conflito de Status de Carga (`Status != Charging`)
- **Arquivo:** `sanders-battery-guard.sh`
- **Status:** 🔵 Aberto
- **Descrição:** Não verifica se `/sys/class/power_supply/usb/online` indica fonte USB fisicamente conectada antes de tentar reativar o carregador em baixa carga.
- **Impacto:** Tentativa inútil de ativação do carregador em modo bateria.

---

## 2. 🕒 Sincronismo de Relógio (`sanders-timesync.sh`)

### Item 2.1 🔵 BUG-003: Dependência de Resolução DNS com `systemd-resolved` Retardado
- **Arquivo:** `sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-timesync.sh:10-15`
- **Status:** 🔵 Aberto (baixa prioridade — script tem janela de retry)
- **Descrição:** `getent hosts` pode falhar se `systemd-resolved`/`resolv.conf` ainda estiver em progresso no primeiro boot.

### Item 2.2 🔵 BUG-004: Parsing frágil do `timedatectl`
- **Arquivo:** `sanders-timesync.sh:24-27`
- **Status:** 🔵 Aberto
- **Descrição:** `awk` procura exatamente `"System clock synchronized: yes"`; formato pode variar.

### Item 2.3 🔵 BUG-005: Mensagem de "relógio ajustado" mesmo se sync falhou
- **Arquivo:** `sanders-timesync.sh:33`
- **Status:** 🔵 Aberto
- **Descrição:** Imprime "Relógio ajustado: $NOW" mesmo se o loop de sync não confirmou `synchronized: yes`.

---

## 3. 🐕 Hardware Watchdog & Suspensão (`qcom-wdt` + Systemd)

### Item 3.1 ✅ BUG-006: Reset de Hardware Durante Suspend (S2idle / Deep Sleep)
- **Arquivo:** `rootfs-overlay/common/etc/systemd/sleep.conf.d/10-disable-sleep.conf`
- **Status:** ✅ **Corrigido 2026-09-02**
- **Descrição:** `RuntimeWatchdogSec=30s` + driver `qcom-wdt` (base `0x0b017000`) poderia resetar ~30s após sleep. Resolvido bloqueando suspend/hibernate na raiz (`AllowSuspend=no`, etc.) via `sleep.conf.d/10-disable-sleep.conf`. Servidor headless não tem motivo pra suspender.

---

## 4. 🔵 Restauração de Bluetooth MAC (`sanders-bt-mac.sh`)

### Item 4.1 🔵 BUG-007: Concorrência com Daemon `bluetoothd`
- **Arquivo:** `rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service`
- **Status:** 🔵 Aberto (risco baixo — bluetooth.service não habilitado)
- **Descrição:** Se `bluetoothd` assumir o socket de gerenciamento antes do `btmgmt public-addr`, retorna `Invalid Index`/`Permission Denied`.

### Item 4.2 🔄 BUG-008: `set +e` global nunca restaurado
- **Arquivo:** `sanders-bt-mac.sh:95`
- **Status:** 🔄 **Aceito (design intencional)** — revisado em 2026-09-02
- **Descrição:** `set +e` é global e não volta a `set -e`. Porém **todos** os paths de saída usam `exit` explícito (0 ou 1) e há validação final com retry que reporta `exit 1`. Falhas em `power off`/`public-addr`/`power on` são ignoradas propositalmente (btmgmt às vezes dita erro mesmo aplicando). Não quebra o fluxo.

### Item 4.3 ✅ BUG-009: `$*` em string passada para `script -qc`
- **Arquivo:** `sanders-bt-mac.sh:90-92`
- **Status:** ✅ Corrigido 2026-09-02 — agora usa `args=$(printf ' %q' "$@")` seguido de `script -qc "btmgmt --index 0$args" /dev/null` (escape correto de cada argumento).

### Item 4.4 ✅ BUG-010: `ConditionPathExists` pode falhar por timing
- **Arquivo:** `sanders-bt-mac.service`
- **Status:** ✅ Corrigido 2026-09-02 — adicionados `After=`/`Wants=` nos device units (`dev-disk-by\x2dpartlabel-persist.device`, `sys-subsystem-bluetooth-devices-hci0.device`) + comentário documentando que `Condition*` não é reavaliada. O serviço também tem retry interno de 20s.

### Item 4.5 🔵 BUG-011: `TIMEOUT_START` — timeout 60s poderia ser curto em boot extremamente lento
- **Arquivo:** `sanders-bt-mac.service` (`TimeoutStartSec=60`)
- **Status:** 🔵 Aberto (mitigado — o script aguarda até ~40s internamente, 60s de margem)
- **Descrição:** Pior caso real (20s hci0 + 10s public-addr + 10s validação) = atinge 40s; 60s é adequado, mas sem folga grande.

---

## 5. 🛠️ Utilitário de Setup do Servidor (`sanders-server-setup.sh`)

### Item 5.1 ✅ BUG-012: Risco de Atualização Parcial (`pacman -Sy`)
- **Status:** ✅ **Corrigido 2026-09-02** — `pacman -Sy` → `pacman -Syu` em `sanders-server-setup.sh:84` e nos dois pontos de `05-build-rootfs.sh` (linhas 72, 82).

### Item 5.2 🔄 BUG-013: Exit codes de `sanders-thermal.sh` engolidos (`|| true`)
- **Arquivo:** `sanders-server-setup.sh:52`
- **Status:** 🔄 **Aceito (design intencional)** — revisado em 2026-09-02
- **Descrição:** `|| true` é INTENCIONAL e comentado: o relatório roda sob `set -euo pipefail`, e se `sanders-thermal.sh` sair com 1/2 (WARNING/CRITICAL), mataria o script no meio do diagnóstico — justo no caso em que mais precisamos ver Watchdog/Rede/Armazenamento. Não é bug.

---

## 6. 📶 Patch Wi-Fi WPA2 (`0002-wcn36xx-force-v0-for-wcn3680.patch`)

### Item 6.1 🔄 BUG-014: Limitação de Velocidade 802.11ac (HT vs VHT)
- **Arquivo:** `kernel/0002-wcn36xx-force-v0-for-wcn3680.patch`
- **Status:** 🔄 **Aceito (trade-off)** — dividido com ROADMAP
- **Descrição:** Força API v0 (HT/802.11n) no `WCN3680`, limitando a ~150 Mbps (HT40), sem VHT (802.11ac 5GHz). Contorna o `MEM_FAIL=5`.

---

## 7. 🖥️ Devicetree `dts/msm8953-motorola-sanders.dts`

### Item 7.1 ✅ BUG-015: Framebuffer `reg` size não batia com `cont_splash_mem`
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:40`
- **Status:** ✅ **Corrigido 2026-09-02** — `reg = <0 0x90001000 0 (2220 * 1920 * 3)>` **→** `(1080 * 1920 * 3)`. Comentário no próprio DTS (linha 40-49) explica que `2220` era quase o dobro do que `cont_splash_mem` (1080×1920×3=6.220.800 bytes) reservava, sobrepondo RAM normal. `cont_splash_mem` na linha 106 também está `(1080 * 1920 * 3)`.

### Item 7.2 🔵 BUG-016: `delete-then-redefine` redundante
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:15-16 vs 90-98`
- **Status:** 🔵 Aberto
- **Descrição:** `/delete-node/` seguido de redefine do mesmo label. Funcional no-op, mas confuso.

### Item 7.3 🔄 BUG-017: `ts_reset` pinctrl definido mas nunca referenciado
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:413`
- **Status:** 🔄 **Aceito (design intencional)** — revisado em 2026-09-02
- **Descrição:** O reset do touchscreen foi movido para `gpio-hog` (sempre HIGH) porque o pulse curto do driver (~5ms) deixava o chip num estado em que `regmap_bulk_read` dava `-ETIMEDOUT` (comentado no DTS, linhas 184-187). O pinctrl `ts-reset-state` ficou como código morto/documentação, intencional.

---

## 8. ⚙️ Config Fragment (`kernel/sanders.config.fragment`)

### Item 8.1 🟠 BUG-018: `CONFIG_I2C`/`CONFIG_I2C_QUP` não explicitados no fragment
- **Arquivo:** `kernel/sanders.config.fragment`
- **Status:** 🟠 Aberto (depende do defconfig arm64)
- **Descrição:** O fragment habilitou `CONFIG_TOUCHSCREEN_EDT_FT5X06=y` (i2c_3 @0x38) e `CONFIG_LTR501=y` (i2c_7 @0x23), mas **não** força `CONFIG_I2C`/`CONFIG_I2C_QUP` builtin. O mesmo raciocínio do `CONFIG_PHY_QCOM_QUSB2` (forçar builtin porque não há modprobe no initramfs) deveria valer pro I2C. **Nota:** o defconfig arm64 usual já traz `CONFIG_I2C=y`/`CONFIG_I2C_QUP=y`, então pode já estar OK — precisa confirmar no `.config` real gerado (não confirmável nesta revisão).
- **Fix sugerido (não aplicado):** adicionar `CONFIG_I2C=y` e `CONFIG_I2C_QUP=y` explicitamente.

### Item 8.2 🔵 BUG-019: Backlight configs ausentes (wled `okay` mas inerte)
- **Arquivo:** `kernel/sanders.config.fragment`
- **Status:** 🔵 Aberto
- **Descrição:** `&pmi8950_wled` está `status="okay"` no DTS, mas não há `CONFIG_BACKLIGHT_CLASS_DEVICE`/`CONFIG_BACKLIGHT_QCOM_SPMI_WLED` no fragment. Nó fica inerte.

### Item 8.3 🔵 BUG-020: `CONFIG_WCN36XX_DEBUG=y` fora de lugar
- **Arquivo:** `kernel/sanders.config.fragment:142`
- **Status:** 🔵 Aberto (cosmético) — opção Wi-Fi posicionada na seção IIO.

### Item 8.4 ✅ BUG-021: Netfilter/NAT faltando (quebrava Docker)
- **Arquivo:** `kernel/sanders.config.fragment`
- **Status:** ✅ Corrigido 2026-09-02 — adicionado bloco Netfilter/NAT/bridge (`CONFIG_NF_TABLES`, `CONFIG_NF_NAT`, `CONFIG_IP_NF_NAT`, `CONFIG_IP_NF_TARGET_MASQUERADE`, `CONFIG_BRIDGE`, `CONFIG_VETH` etc., linhas 159-178). **Ainda falta** recompilar kernel para validar Docker ao vivo.

### Item 8.5 🔵 BUG-022: Bateria/charger sem driver mainline (nós inertes)
- **Arquivo:** `kernel/sanders.config.fragment:212-228`
- **Status:** 🔵 Aberto (documentado) — não existe driver mainline para `qcom,pmi8996-smbchg`/`qcom,pmi8996-fg`. `CONFIG_POWER_SUPPLY=y` mantido; nós ficam `okay` mas inertes até alguém portar o driver.

---

## 9. 🧱 Build Scripts

### Item 9.1 ✅ BUG-023: `multi-user.target.wants` criado após symlinks
- **Arquivo:** `scripts/05-build-rootfs.sh`
- **Status:** ✅ Corrigido 2026-09-02 — `mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"` movido para linha 47, **antes** dos primeiros symlinks (pacman-keyring linha 129, wpa_supplicant linha 232). O `mkdir` redundante da linha 268 é agora inofensivo (idempotente).

### Item 9.2 ✅ BUG-024: `make defconfig` sempre rodava no busybox
- **Arquivo:** `scripts/03-build-busybox.sh:17`
- **Status:** ✅ Corrigido 2026-09-02 — `[ -f .config ] || make defconfig` (só gera do zero se não existir; preserva tuning manual).

### Item 9.3 ✅ BUG-025: stderr do cpio suprimido
- **Arquivo:** `scripts/04-build-initramfs.sh:56-64`
- **Status:** ✅ Corrigido 2026-09-02 — não suprime mais o stderr (redireciona `.config`-style para log com `die "cpio falhou..."` em caso de erro). Comentário explica que `cpio -o` imprime "N blocks" no stderr que antes era descartado junto com erros reais.

### Item 9.4 ✅ BUG-026: `read -r` sem strip de `\r` (CRLF)
- **Arquivo:** `scripts/04-build-initramfs.sh:26,31`
- **Status:** ✅ Corrigido 2026-09-02 — `app="${app%$'\r'}"` antes de criar symlink.

### Item 9.5 ✅ BUG-027: Build de todos os DTBs, não só o sanders
- **Arquivo:** `scripts/02-build-kernel.sh`
- **Status:** ✅ Corrigido 2026-09-02 — compila apenas `qcom/$DTS_NAME.dtb` (linhas 56-66), não `dtbs` (que compilaria os ~30 DTBs qcom).

### Item 9.6 🟠 BUG-028: Patches fixos no branch `master` volátil
- **Arquivo:** `scripts/lib.sh:35` (`LINUX_BRANCH="master"`)
- **Status:** 🟠 Aberto
- **Descrição:** Os 4 patches aplicam com linha de contexto fixa. Em `master`, refactors podem quebrar `git apply`. Tag LTS seria mais estável.

### Item 9.7 🔵 BUG-029: `sleep 3` hardcoded entre lk2nd e kernel
- **Arquivo:** `scripts/07-boot-kernel.sh`
- **Status:** 🔵 Aberto — se a enumeração USB for lenta, o 2º `fastboot boot` pode chegar cedo.

### Item 9.8 🔵 BUG-030: `ChallengeResponseAuthentication` deprecated (`05-build-rootfs.sh:270`)
- **Status:** 🔵 Aberto — renomeado para `KbdInteractiveAuthentication` no OpenSSH 9.x.

### Item 9.9 🔵 BUG-031: `earlycon` sem MMIO na cmdline
- **Arquivo:** `scripts/lib.sh:73`
- **Status:** 🔵 Aberto (baixo) — `earlycon` simples depende do console configurado; pode falhar silenciosamente no early boot.

### Item 9.10 🔵 BUG-032: clone lk2nd completo (sem `--depth`)
- **Arquivo:** `scripts/01-build-lk2nd.sh`
- **Status:** 🔵 Aberto (desempenho).

### Item 9.11 🔵 BUG-033: fallback para HEAD no lk2nd
- **Arquivo:** `scripts/01-build-lk2nd.sh`
- **Status:** 🔵 Aberto (mitigado — agora usa `warn` para avisar). Se commit `c8b47cd` não existir, `warn` mas continua com HEAD.

### Item 9.12 🔵 BUG-034: path hardcoded do stock zip
- **Arquivo:** `scripts/09-extract-firmware.sh:20-24`
- **Status:** 🔵 Aberto, **parcialmente mitigado** — agora suporta override via `STOCK_ZIP` env, mas o fallback default ainda é `/mnt/hdauxiliar/android/projeto_g5/stock/`.

### Item 9.13 🔄 BUG-035: ordem de args do `debugfs`
- **Arquivo:** `scripts/09-extract-firmware.sh:42`
- **Status:** 🔄 **Não é bug** — `debugfs -R "dump ..." device` está correto (`-R` comando antes, device depois).

---

## 10. 🔧 Patches de Kernel

### Item 10.1 🔄 BUG-036: Patch 0001 mascara falha real de hardware
- **Arquivo:** `kernel/0001-edt-ft5x06-skip-identify-for-ft5436.patch`
- **Status:** 🔄 **Aceito (HACK documentado)** — revisado em 2026-09-02
- **Descrição:** Em qualquer falha de identify (incluindo I2C genuíno com problema), assume defaults `EDT_M09` e `dev_warn` em vez de `dev_err`+return error. **Risco real**: touchscreen morto sem diagnóstico. Mas é intencional, comentado `HACK sanders`, e o FT5436 realmente não expõe o name no reg 0xBB.

### Item 10.2 🔵 BUG-037: Gap de numeração 0003 (0001, 0002, 0004)
- **Arquivo:** `kernel/`
- **Status:** 🔵 Aberto (cosmético)
- **Descrição:** Há 0001, 0002 e 0004 — sem 0003. Sugere patch removido sem renomear; `02-build-kernel.sh` itera `000[0-9]*` então não quebra, mas o gap confunde.

---

## 11. 📄 Rootfs-Overlay (Serviços e Scripts)

### Item 11.1 ✅ BUG-038: Senha WiFi exposta no `ps`
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh:53`
- **Status:** ✅ Corrigido 2026-09-02 — `echo "$pass" | wpa_passphrase "$ssid"` (senha via stdin, não como argumento de comando).

### Item 11.2 🟡 BUG-039: `mkswap`/`swapon` podem rodar mesmo se cadeia `zramctl` falhar
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-zram.sh:25-31`
- **Status:** 🟡 Aberto (mitigado por `set -euo pipefail`; se `mkswap` falhar o script morre)
- **Descrição:** A cadeia `a || b || c` e a seguir `mkswap`/`swapon` rodam incondicionalmente. O fallback de `--algorithm` foi melhorado (2 algos + sem algo), mas se todos falharem `mkswap` tenta em device inexistente.

### Item 11.3 ✅ BUG-040: `pulse N` não validava numérico
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-led.sh:116-120`
- **Status:** ✅ Corrigido 2026-09-02 — validação `case "$count" in ''|*[!0-9]*) die ...` .

### Item 11.4 🔄 BUG-041: `find+awk` campo fixo no cpufreq
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-cpufreq.sh:10`
- **Status:** 🔄 **Não é bug** — revisado em 2026-09-02: o path é `/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor` e `awk -F'/' '{print $6}'` = `cpu0`, **correto**. Script também trata o caso de cpufreq ausente (sai limpo, comentado).

### Item 11.5 ✅ BUG-042: `cmd_pulse` não restaurava estado anterior
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-led.sh`
- **Status:** ✅ Corrigido 2026-09-02 — `cmd_pulse` agora salva `prev_brightness`/`prev_trigger` e restaura no fim.

### Item 11.6 🔵 BUG-043: `zramctl --algorithm` pode não existir
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-zram.sh:25-27`
- **Status:** 🔵 Aberto (mitigado por fallback sem algorithm).

### Item 11.7 ✅ BUG-044: Regex de interface USB Ethernet muito larga
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh:125-129`
- **Status:** ✅ Corrigido 2026-09-02 — `$2 ~ /^enx/ || $2 ~ /^eth[0-9]+$/` (ancorado) com comentário.

### Item 11.8 🔵 BUG-045: CRLF em chave SSH (dedup falho)
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-ssh-setup.sh`
- **Status:** 🔵 Aberto — CRLF faria dedup falhar (chaves duplicadas).

### Item 11.9 ✅ BUG-046: Comentário stale no `sanders-led.service`
- **Arquivo:** `rootfs-overlay/common/etc/systemd/system/sanders-led.service`
- **Status:** ✅ Corrigido 2026-09-02 — comentário atualizado (referencia o override de wait-online já corrigido).

---

## 12. 📚 Documentação (`docs/`)

### Item 12.1 ✅ BUG-047: Range de memória invertido
- **Arquivo:** `docs/HARDWARE_REFERENCE.md:282`
- **Status:** ✅ Corrigido 2026-09-02 — agora `0xeefe4000-0xef000000` (112KB, correto).

### Item 12.2 ✅ BUG-048: Docker sem aviso de rebuild
- **Arquivo:** `docs/ROADMAP_AND_TODOS.md`
- **Status:** ✅ Corrigido 2026-09-02 — ROADMAP linha 62-63 documenta causa raiz (nftables off) e que falta recompilar.

### Item 12.3 ✅ BUG-049: Comandos cpufreq assumiam driver
- **Arquivo:** `docs/ROADMAP_AND_TODOS.md`
- **Status:** ✅ Corrigido 2026-09-02 — ROADMAP linha 55 documenta que não há driver cpufreq e que o script sai silenciosamente.

### Item 12.4 ✅ BUG-050: WPA2 marcado `[x]` mas handshake ainda falhava
- **Arquivo:** `docs/ROADMAP_AND_TODOS.md`
- **Status:** ✅ Corrigido 2026-09-02 — agora `[~]` (parcial), "ainda não validado ao vivo".

### Item 12.5 ✅ BUG-051: `--any -i usb0` desatualizado
- **Arquivo:** `docs/ROADMAP_AND_TODOS.md`
- **Status:** ✅ Corrigido 2026-09-02 — agora só `--any`.

### Item 12.6 🔵 BUG-052: HARDWARE_REFERENCE BT MAC não detalha `script -qc`/retry
- **Arquivo:** `docs/HARDWARE_REFERENCE.md:169-178`
- **Status:** 🔵 Aberto (menor) — doc descreve o fluxo e MAC resultante, mas não menciona explicitamente o uso de `script -qc` nem os retrys internos de 20s.

### Item 12.7 ✅ BUG-053: CPU sem caveat de cpufreq
- **Arquivo:** `docs/HARDWARE_REFERENCE.md:8`
- **Status:** ✅ Corrigido 2026-09-02 — caveat explícito de cpufreq adicionado.

### Item 12.8 ✅ BUG-054: Power supply ausente da matriz
- **Arquivo:** `docs/HARDWARE_MATRIX.md:17`
- **Status:** ✅ Corrigido 2026-09-02 — linha de Bateria/Carregador adicionada.

### Item 12.9 ✅ BUG-055: HARDWARE_REFERENCE ausente do README
- **Arquivo:** `README.md`
- **Status:** ✅ Corrigido 2026-09-02 — README lista HARDWARE_MATRIX e HARDWARE_REFERENCE.

### Item 12.10 ✅ BUG-056: CDC ECM descrito como device file
- **Arquivo:** `docs/HARDWARE_REFERENCE.md:121`
- **Status:** ✅ Corrigido 2026-09-02 — texto esclarece que `usb0` é nome de interface, não `/dev/usb0`.

---

## 📊 Tabela Resumo Consolidada (estado verificado 2026-09-02)

| ID | Componente | Descrição | Gravidade | Status |
|---|---|---|---|---|
| BUG-001 | battery-guard | CAP vazio → `integer expression expected` | 🟡 Média | Aberto |
| BUG-002 | battery-guard | Status sem checar fonte USB | 🔵 Baixa | Aberto |
| BUG-003 | timesync | DNS retardado no 1º boot | 🔵 Baixa | Aberto |
| BUG-004 | timesync | parsing timedatectl frágil | 🔵 Baixa | Aberto |
| BUG-005 | timesync | msg sucesso mesmo sync falhou | 🔵 Baixa | Aberto |
| BUG-006 | qcom-wdt/sleep | reset em suspend | 🟠 Alta | ✅ Corrigido |
| BUG-007 | bt-mac.service | race bluetoothd | 🔵 Baixa | Aberto |
| BUG-008 | bt-mac.sh | `set +e` global | 🟡 Média | 🔄 Aceito |
| BUG-009 | bt-mac.sh | `$*` em script -qc | 🟡 Média | ✅ Corrigido |
| BUG-010 | bt-mac.service | ConditionPathExists timing | 🟡 Média | ✅ Corrigido |
| BUG-011 | bt-mac.service | Timeout 60s | 🔵 Baixa | Aberto |
| BUG-012 | server-setup | pacman -Sy | 🟡 Média | ✅ Corrigido |
| BUG-013 | server-setup | thermal exit `\|\| true` | — | 🔄 Aceito |
| BUG-014 | patch 0002 | HT vs VHT | 🔵 Baixa | 🔄 Trade-off |
| BUG-015 | DTS sanders | framebuffer size ≠ cont_splash | 🔴 Crítica | ✅ Corrigido |
| BUG-016 | DTS | delete-then-redefine | 🔵 Baixa | Aberto |
| BUG-017 | DTS | ts_reset pinctrl morto | 🔵 Baixa | 🔄 Aceito |
| BUG-018 | config fragment | I2C/QUP não explícitos | 🟠 Alta | Aberto (a confirmar) |
| BUG-019 | config fragment | backlight inocupado | 🔵 Baixa | Aberto |
| BUG-020 | config fragment | WCN36XX_DEBUG fora de lugar | 🔵 Baixa | Aberto |
| BUG-021 | config fragment | netfilter fazia Docker falhar | 🟠 Alta | ✅ Corrigido |
| BUG-022 | config fragment | charger/fg sem driver | 🔵 Baixa | Aberto |
| BUG-023 | 05-rootfs | multi-user.target.wants ordem | 🟠 Alta | ✅ Corrigido |
| BUG-024 | 03-busybox | defconfig não incremental | 🟠 Alta | ✅ Corrigido |
| BUG-025 | 04-initramfs | cpio stderr suprimido | 🟡 Média | ✅ Corrigido |
| BUG-026 | 04-initramfs | read -r sem strip \r | 🟡 Média | ✅ Corrigido |
| BUG-027 | 02-kernel | compilava todos DTBs | 🟡 Média | ✅ Corrigido |
| BUG-028 | lib.sh | patches no branch master | 🟠 Alta | Aberto |
| BUG-029 | 07-boot-kernel | sleep 3 hardcoded | 🔵 Baixa | Aberto |
| BUG-030 | 05-rootfs | ChallengeResponse deprecated | 🔵 Baixa | Aberto |
| BUG-031 | lib.sh | earlycon sem MMIO | 🔵 Baixa | Aberto |
| BUG-032 | 01-lk2nd | clone completo | 🔵 Baixa | Aberto |
| BUG-033 | 01-lk2nd | fallback HEAD silencioso | 🔵 Baixa | Aberto |
| BUG-034 | 09-firmware | path stock hardcoded | 🔵 Baixa | Aberto (parcial) |
| BUG-035 | 09-firmware | debugfs arg order | — | 🔄 Não é bug |
| BUG-036 | patch 0001 | mascara falha I2C | 🟡 Média | 🔄 Aceito (HACK) |
| BUG-037 | kernel/ | gap numeração 0003 | 🔵 Baixa | Aberto |
| BUG-038 | network-setup | senha no ps | 🟡 Média | ✅ Corrigido |
| BUG-039 | zram.sh | mkswap pós falha zramctl | 🟡 Média | Aberto |
| BUG-040 | led.sh | pulse N não valida | 🟡 Média | ✅ Corrigido |
| BUG-041 | cpufreq.sh | find+awk campo fixo | — | 🔄 Não é bug |
| BUG-042 | led.sh | não restaura estado | 🔵 Baixa | ✅ Corrigido |
| BUG-043 | zram.sh | --algorithm incompat | 🔵 Baixa | Aberto |
| BUG-044 | network-setup | regex USB larga | 🔵 Baixa | ✅ Corrigido |
| BUG-045 | ssh-setup | CRLF em chave | 🔵 Baixa | Aberto |
| BUG-046 | led.service | comentário stale | 🔵 Baixa | ✅ Corrigido |
| BUG-047 | HARDWARE_REF | range invertido | 🟠 Alta | ✅ Corrigido |
| BUG-048 | ROADMAP | Docker sem aviso | 🟠 Alta | ✅ Corrigido |
| BUG-049 | ROADMAP | cpufreq doc | 🟠 Alta | ✅ Corrigido |
| BUG-050 | ROADMAP | WPA2 [x] errado | 🟡 Média | ✅ Corrigido |
| BUG-051 | ROADMAP | --any -i usb0 | 🔵 Baixa | ✅ Corrigido |
| BUG-052 | HARDWARE_REF | BT MAC doc incompleta | 🔵 Baixa | Aberto |
| BUG-053 | HARDWARE_REF | CPU sem caveat | 🔵 Baixa | ✅ Corrigido |
| BUG-054 | HARDWARE_MATRIX | power supply ausente | 🔵 Baixa | ✅ Corrigido |
| BUG-055 | README | HARDWARE_REF ausente | 🔵 Baixa | ✅ Corrigido |
| BUG-056 | HARDWARE_REF | /dev/usb0 | 🔵 Baixa | ✅ Corrigido |

**Resumo geral (verificado):** de 56 itens — **27 corrigidos** pelo agente anterior, **7 aceitos** (design intencional / não-bug), **22 abertos** (1 alta: BUG-028 + BUG-018 a confirmar; restante baixo/médio).

---

## 🔑 Prioridades de correção (itens realmente abertos)

1. **BUG-018** (config I2C/QUP) — confirmar no `.config` real do build se `CONFIG_I2C_QUP=y`; se `=m`, touchscreen/LTR559 podem não bindar (sem modprobe no initramfs).
2. **BUG-028** (patches no branch `master`) — pinar tag LTS do kernel para aplicação estável dos 4 patches.
3. **BUG-001** (battery-guard CAP vazio) — fallback numérico no `awk`.
4. **BUG-039/043** (zram) — proteger `mkswap`/`swapon` com checagem de sucesso da cadeia `zramctl`.
5. **BUG-045** (CRLF em chave SSH) — strip `\r` no dedup.

---

*Revisão re-validada contra os fontes em 2026-09-02 (após as correções de um agente anterior). Documentação apenas — nenhum fonte foi modificado nesta re-revisão.*
