# ⚠️ Conhecimento Consolidado: Issues, Bugs Potenciais e Estado (Audit 2026-09-02)

> **Este é o documento único de referência de bugs/issues do projeto.**
> Consolidado em 2026-09-02 a partir de duas auditorias (Antigravity + revisão completa de scripts/DTS/config/rootfs/patches/docs). Itens corrigidos são marcados como ✅ e mantidos aqui como registro histórico. Itens abertos são marcados como 🔵 (aberto) ou 🔴 (crítico).

---

## 📋 Como ler este documento

- **🔴 Crítico** — risco de instabilidade/corrupção ou quebra de build.
- **🟠 Alto** — risco real de mau funcionamento em cenário comum.
- **🟡 Médio** — problema real mas de baixo impacto ou cenário específico.
- **🔵 Baixo** — cosmético/informativo, melhoria.
- **✅ Corrigido** — já resolvido (mantido para histórico).

---

## 1. 🔋 Proteção de Bateria (`sanders-battery-guard.sh`)

### Item 1.1 🔵 BUG-KNOWN-01: Truncamento ou Retorno Vazio na Leitura de Capacidade Sysfs
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-battery-guard.sh), linhas 13-14
- **Status:** 🔵 Aberto
- **Descrição:** Se `/sys/class/power_supply/battery/capacity` existir mas o driver não tiver inicializado (retornando string vazia ou erro de I/O), `CAP` vira string vazia — o `|| echo "0"` só dispara se o `awk` falhar, não se retornar vazio.
- **Impacto:** `[ "$CAP" -ge "$HIGH_LIMIT" ]` falha com `integer expression expected`.
- **Sugestão:** `CAP=$(awk '{print ($1 ~ /^[0-9]+$/ ? $1 : 0)}' "$BAT_SYS/capacity" 2>/dev/null || echo "0")`

### Item 1.2 🔵 BUG-KNOWN-02: Conflito de Status de Carga (`Status != Charging`)
- **Arquivo:** `sanders-battery-guard.sh`
- **Status:** 🔵 Aberto
- **Descrição:** O script não verifica se a fonte USB está fisicamente conectada (`/sys/class/power_supply/usb/online`) antes de tentar reativar o carregador quando cai abaixo de 70%.
- **Impacto:** Tentativa inútil de ativação em modo bateria.

---

## 2. 🕒 Sincronismo de Relógio (`sanders-timesync.sh`)

### Item 2.1 🔵 BUG-KNOWN-03: Dependência de Resolução DNS com `systemd-resolved` Retardado
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-timesync.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-timesync.sh), linhas 10-15
- **Status:** 🔵 Aberto (baixa prioridade — script tem janela de retry de 30s)
- **Descrição:** `getent hosts` pode falhar se `systemd-resolved`/`resolv.conf` ainda estiver em progresso no primeiro boot.
- **Impacto:** Atraso no alinhamento do relógio no primeiro boot.

### Item 2.2 🔵 BUG-KNOWN-04: Parsing frágil do `timedatectl`
- **Arquivo:** `sanders-timesync.sh`, linhas 24-27
- **Status:** 🔵 Aberto
- **Descrição:** `awk` procura exatamente por `"System clock synchronized: yes"`. O formato pode variar (`"NTP service: active"`).

### Item 2.3 🔵 BUG-027: Mensagem de sucesso mesmo se sync falhou
- **Arquivo:** `sanders-timesync.sh:33`
- **Status:** 🔵 Aberto
- **Descrição:** Se o loop de NTP não encontrou `synchronized: yes`, o script ainda imprime "Relógio ajustado: $NOW" — mensagem enganosa.
- **Sugestão:** Checar flag de sucesso antes de imprimir.

---

## 3. 🐕 Hardware Watchdog & Suspensão (`qcom-wdt` + Systemd)

### Item 3.1 ✅ BUG-KNOWN-05: Reset de Hardware Durante Suspend (S2idle / Deep Sleep)
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system.conf.d/10-watchdog.conf) (`RuntimeWatchdogSec=30s`)
- **Status:** ✅ **Corrigido 2026-09-02** — adicionado [`rootfs-overlay/common/etc/systemd/sleep.conf.d/10-disable-sleep.conf`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/sleep.conf.d/10-disable-sleep.conf) que desabilita suspend/hibernate/hybrid-sleep na raiz (`AllowSuspend=no` etc). Servidor headless não tem motivo pra suspender; isso fecha o risco por completo.
- **Descrição original:** O `qcom-wdt` (base `0x0b017000`) pode disparar reset forçado ~30s após entrar em sleep porque o systemd para de pingar mas o timer de hardware continua contando.

---

## 4. 🔵 Restauração de Bluetooth MAC (`sanders-bt-mac.sh` & `sanders-bt-mac.service`)

### Item 4.1 🔵 BUG-KNOWN-06: Concorrência com o Daemon `bluetoothd` (BlueZ)
- **Arquivo:** [`rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service)
- **Status:** 🔵 Aberto — risco baixo na prática (bluetooth.service não habilitado; só ativa via D-Bus se algo chamar a API manualmente).
- **Descrição:** Se `bluetoothd` assumir o socket de gerenciamento antes do `btmgmt public-addr`, retorna `Invalid Index`/`Permission Denied`.

### Item 4.2 🔵 BUG-006: `set +e` nunca restaurado
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-bt-mac.sh:95`
- **Status:** 🔵 Aberto (severidade revista pra baixo — sem consequência prática hoje)
- **Descrição:** `set +e` é chamado globalmente e nunca restaurado — verdade, mas **não causa o impacto descrito**: o script já valida o resultado por *estado* (relê `current_mac()` até bater com `$MAC` ou esgotar as tentativas, e sai com `exit 1` + mensagem clara se falhar), não por exit code de `btmgmt_run`. E não há código depois desse ponto no arquivo (a última linha é `exit 1`) — não tem "resto do script" que herde `set +e` por engano. É higiene de estilo, não bug ativo.

### Item 4.3 🟡 BUG-029: `$*` dentro de string passada para `script -qc`
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-bt-mac.sh:86`
- **Status:** 🔵 Aberto
- **Descrição:** `script -qc "btmgmt --index 0 $*" /dev/null` — `$*` expande no contexto do shell externo, não no subshell `script`. Argumentos com espaços sofrem word-split.
- **Sugestão:** `"$@"` ou escape adequado.

### Item 4.4 🔵 BUG-045: `ConditionPathExists` pode falhar por timing
- **Arquivo:** `rootfs-overlay/common/etc/systemd/system/sanders-bt-mac.service:10`
- **Status:** 🔵 Aberto
- **Descrição:** `ConditionPathExists=/dev/disk/by-partlabel/persist` depende de udev popular o path. Se ainda não populou quando o systemd avalia a condição, o serviço é skipado no boot inteiro.

---

## 5. 🛠️ Utilitário de Setup do Servidor (`sanders-server-setup.sh`)

### Item 5.1 ✅ BUG-KNOWN-07: Risco de Atualização Parcial no Arch Linux (`pacman -Sy`)
- **Arquivo:** [`rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh) (linha 79)
- **Status:** ✅ **Corrigido 2026-09-02** — trocado `pacman -Sy` → `pacman -Syu` em `sanders-server-setup.sh` e nos dois pontos de `05-build-rootfs.sh` (linhas 72, 82).

### Item 5.2 🔵 BUG-005: Exit codes do `sanders-thermal.sh` engolidos
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-server-setup.sh:52`
- **Status:** ⚠️ **Parcialmente incorreto (revisado 2026-09-02) — o `|| true` é intencional, de um fix anterior meu na mesma sessão.** O `|| true` NÃO esconde o alerta: testei antes (com fake thermal script simulando WARNING) e o texto `[WARNING]`/`[CRITICAL]` por sensor e o "Status Geral: [WARNING]" aparecem normalmente no `--status`, porque é conteúdo de STDOUT do pipe, independente do exit code. O que o `|| true` suprime é só o *exit code do processo* `sanders-server-setup.sh` — que é justo, porque **sem** ele, `set -euo pipefail` + o exit 1/2 do thermal cortava o relatório inteiro no meio (Watchdog/Rede/Armazenamento nunca imprimiam) — bug real que eu já tinha corrigido e reproduzido isoladamente antes. `--status` é dashboard pra humano ler; o alerta de verdade (pra monitoramento automatizado) é o `sanders-thermal.timer`/`.service` rodando sozinho, que preserva o exit code 1/2 normalmente (aparece em `systemctl --failed`). Não mudei nada aqui.

---

## 6. 📶 Patch Wi-Fi WPA2 (`0002-wcn36xx-force-v0-for-wcn3680.patch`)

### Item 6.1 🔵 BUG-KNOWN-08: Limitação de Velocidade 802.11ac (5GHz HT vs VHT)
- **Arquivo:** [`kernel/0002-wcn36xx-force-v0-for-wcn3680.patch`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/kernel/0002-wcn36xx-force-v0-for-wcn3680.patch)
- **Status:** 🔵 Aberto (trade-off aceito, não é bug)
- **Descrição:** Força API v0 (802.11n/HT) no `RF_IRIS_WCN3680`, contornando `MEM_FAIL=5` mas limitando a ~150 Mbps (HT40) sem VHT (802.11ac 5GHz).

---

## 7. 🖥️ Devicetree `dts/msm8953-motorola-sanders.dts`

### Item 7.1 🔴 BUG-001: Framebuffer `reg` size não bate com `cont_splash_mem`
- **Arquivo:** [`dts/msm8953-motorola-sanders.dts:40`](file:///mnt/hdauxiliar/android/projeto_g5/sanders-linux-mainline/dts/msm8953-motorola-sanders.dts), linha 40
- **Status:** ✅ **Corrigido 2026-09-02 (verificado como real e crítico).** Confirmei em `drivers/video/fbdev/simplefb.c`: `info->fix.smem_len = resource_size(mem)` e `screen_base = ioremap_wc(smem_start, smem_len)` usam o tamanho CHEIO do `reg`, não `width*height*stride` — os ~6.5MB extras seriam mesmo mapeados write-combining por cima de RAM normal não reservada. Corrigido `2220` → `1080`, recompilado e verificado no DTB (`reg` agora `0x5eec00` = exatamente `1080*1920*3`).
- **Causa provável:** Fator `2220` inválido (deveria ser `1080`, igual a `stride` de 3240 × height 1920 = 6.220.800).

### Item 7.2 🔵 BUG-030: `cont_splash_mem` / `qseecom_mem` delete-then-redefine redundante
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:15-16 vs 90-98`
- **Status:** 🔵 Aberto
- **Descrição:** Nós deletados (`/delete-node/`) e imediatamente redefinidos com o mesmo label. Funcionalmente no-op, mas confuso e sinaliza edit abandonado.

### Item 7.3 🔵 BUG-031: `ts_reset` pinctrl definido mas nunca referenciado
- **Arquivo:** `dts/msm8953-motorola-sanders.dts:403-409`
- **Status:** 🔵 Aberto
- **Descrição:** Estado `ts-reset-state` declarado mas `touchscreen@38` não tem `pinctrl-0` — reset é dirigido apenas via `gpio-hog`. Código morto.

---

## 8. ⚙️ Config Fragment (`kernel/sanders.config.fragment`)

### Item 8.1 🔵 BUG-012: `CONFIG_I2C` / `CONFIG_I2C_QUP` não forçados builtin
- **Arquivo:** `kernel/sanders.config.fragment` (ausente do arquivo)
- **Status:** ❌ **Falso positivo (verificado 2026-09-02).** Compilei o `.config` de verdade (`make defconfig` arm64 + fragment + `olddefconfig`, o mesmo build já validado nesta sessão) e conferi: `CONFIG_I2C=y` e `CONFIG_I2C_QUP=y` **já vêm builtin por padrão do defconfig arm64**, sem precisar de nada no fragment. Diferente do caso do `PHY_QCOM_QUSB2` (que defconfig realmente deixa `=m`), aqui não há dependência quebrada nenhuma.

### Item 8.2 🟠 BUG-013: Backlight configs ausentes (wled habilitado no DTS)
- **Arquivo:** `kernel/sanders.config.fragment` + `dts/msm8953-motorola-sanders.dts:234`
- **Status:** 🔵 Aberto
- **Descrição:** `&pmi8950_wled` está `status = "okay"` mas não há `CONFIG_BACKLIGHT_CLASS_DEVICE` nem `CONFIG_BACKLIGHT_QCOM_SPMI_WLED`. Nó fica inerte.
- **Sugestão:** Ou adicionar configs, ou manter `disabled` (consistente com "sem painel real ainda").

### Item 8.3 🔵 BUG-032: `CONFIG_WCN36XX_DEBUG=y` fora de lugar
- **Arquivo:** `kernel/sanders.config.fragment:142`
- **Status:** 🔵 Aberto (cosmético)
- **Descrição:** Opção Wi-Fi posicionada na seção IIO (após `CONFIG_LTR501`).

---

## 9. 🧱 Build Scripts (`scripts/`)

### Item 9.1 🔵 BUG-002: `multi-user.target.wants` criado após primeiros symlinks que dependem dele
- **Arquivo:** `scripts/05-build-rootfs.sh:113,216` (symlinks) vs `:252` (mkdir -p)
- **Status:** ✅ **Corrigido 2026-09-02 (severidade revista pra baixo — não era build-breaker ativo).** Verifiquei com `tar tzf` na tarball real (`build/ArchLinuxARM-aarch64-latest.tar.gz`): `etc/systemd/system/multi-user.target.wants/` **já vem populado** de fábrica (`remote-fs.target`, `systemd-networkd.service`, `sshd.service`) — os `ln -sf` das linhas 113/216 já funcionavam hoje, não é um build-breaker ativo. Mas é uma dependência implícita frágil (quebraria se uma tarball futura viesse sem esses symlinks base), então adicionei `mkdir -p` explícito logo após extrair a tarball mesmo assim — barato e remove a fragilidade.

### Item 9.2 🟡 BUG-019: stderr do cpio suprimido
- **Arquivo:** `scripts/04-build-initramfs.sh:54`
- **Status:** 🔵 Aberto
- **Descrição:** `cpio -o -H newc 2>/dev/null` suprime TODOS os erros (permissions, disco cheio). Initramfs corrupto pode ser gerado e usado sem diagnóstico.
- **Sugestão:** Remover `2>/dev/null` ou filtrar apenas o block count.

### Item 9.3 🟡 BUG-020: `read -r` não strip `\r` (CRLF)
- **Arquivo:** `scripts/04-build-initramfs.sh:26,31`
- **Status:** 🔵 Aberto
- **Descrição:** Se `busybox-symlinks-bin.txt` tiver CRLF, `app` preserva `\r` → symlinks quebrados como `awk\r → busybox`.
- **Sugestão:** `app="${app//$'\r'/}"` ou `tr -d '\r'` no pipe.

### Item 9.4 🟡 BUG-021: Build de todos os DTBs, não só sanders
- **Arquivo:** `scripts/02-build-kernel.sh:55`
- **Status:** 🔵 Aberto (desempenho)
- **Descrição:** `make ... dtbs` compila DTBs de todos os SoCs Qualcomm. Minutos extras desnecessários.
- **Sugestão:** `arch/arm64/boot/dts/qcom/$DTS_NAME.dtb`.

### Item 9.5 🟠 BUG-018: Patches fixos no branch `master` volátil
- **Arquivo:** `lib.sh:35` (`LINUX_BRANCH="master"`)
- **Status:** 🔵 Aberto
- **Descrição:** Os 3 patches aplicam com contextos de linha fixos. Em `master`, refactors podem quebrar `git apply` → build `die`. Tag LTS seria mais estável.

### Item 9.6 🟡 BUG-010: `sleep 3` hardcoded entre lk2nd e kernel
- **Arquivo:** `scripts/07-flash-and-boot.sh:35`
- **Status:** 🔵 Aberto
- **Descrição:** Se USB enumeration for lenta, o segundo `fastboot boot` chega antes do lk2nd estar pronto → falha silenciosa.
- **Sugestão:** Loop de retry com `fastboot getvar product`.

### Item 9.7 🟡 BUG-011: `ChallengeResponseAuthentication` deprecated
- **Arquivo:** `scripts/05-build-rootfs.sh:270`
- **Status:** 🔵 Aberto
- **Descrição:** Opção renomeada para `KbdInteractiveAuthentication` no OpenSSH 9.x. Gera warning no log a cada boot.

### Item 9.8 🟠 BUG-003: `make defconfig` sempre roda no busybox
- **Arquivo:** `scripts/03-build-busybox.sh:17`
- **Status:** 🔵 Aberto
- **Descrição:** `make defconfig` roda incondicionalmente, sobrescrevendo `.config` existente. Tuning manual descartado.

### Item 9.9 🔵 BUG-034: Clone lk2nd completo (não shallow)
- **Arquivo:** `scripts/01-build-lk2nd.sh:10`
- **Status:** 🔵 Aberto (desempenho)

### Item 9.10 🔵 BUG-035: Fallback silencioso para HEAD no lk2nd
- **Arquivo:** `scripts/01-build-lk2nd.sh:14`
- **Status:** 🔵 Aberto
- **Descrição:** Se commit `c8b47cd` não existe, `warn` mas continua com HEAD → build não testado.

### Item 9.11 🟡 BUG-028: Path hardcoded do stock zip
- **Arquivo:** `scripts/09-extract-firmware.sh:21`
- **Status:** 🔵 Aberto
- **Descrição:** `ls /mnt/hdauxiliar/android/projeto_g5/stock/SANDERS_RETAIL_*.zip` — path absoluto da máquina do dev.

### Item 9.12 🔵 BUG-036: `debugfs` argument order não padrão
- **Arquivo:** `scripts/09-extract-firmware.sh:42`
- **Status:** 🔵 Aberto

### Item 9.13 🔵 BUG-037: `earlycon` sem endereço MMIO
- **Arquivo:** `lib.sh:50`
- **Status:** 🔵 Aberto
- **Descrição:** `earlycon` sem `msm_serial_hsl,0xC170000` pode falhar silenciosamente.

---

## 10. 🔧 Patches de Kernel

### Item 10.1 🟡 BUG-033: Patch 0001 mascara falhas reais de hardware
- **Arquivo:** `kernel/0001-edt-ft5x06-skip-identify-for-ft5436.patch`
- **Status:** 🔵 Aberto (hack documentado)
- **Descrição:** Em qualquer falha de identify (incluindo I2C genuíno com problema), driver força defaults EDT_M09 e reporta probe OK → touchscreen morto sem diagnóstico.

### Item 10.2 🔵 BUG-034b: Patch 0002 força V0 para todos os WCN3680
- **Arquivo:** `kernel/0002-wcn36xx-force-v0-for-wcn3680.patch`
- **Status:** 🔵 Aberto (coberto pelo Item 6.1)

### Item 10.3 🔵 BUG-035b: Patch 0003 ausente (gap de numeração)
- **Arquivo:** `kernel/` (0001, 0002, 0004 — sem 0003)
- **Status:** 🔵 Aberto
- **Descrição:** Gap na numeração sequencial sugere patch removido sem renomear.

---

## 11. 📄 Rootfs-Overlay (Serviços e Scripts)

### Item 11.1 🟠 BUG-014: Senha WiFi exposta no `ps`
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh:50`
- **Status:** 🔵 Aberto (segurança)
- **Descrição:** `wpa_passphrase "$ssid" "$pass"` — senha como argumento de comando, visível via `ps aux`.
- **Sugestão:** `echo "$pass" | wpa_passphrase "$ssid"` (via stdin).

### Item 11.2 🟡 BUG-015: `mkswap`/`swapon` roda após falha do zramctl
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-zram.sh:25-29`
- **Status:** 🔵 Aberto
- **Descrição:** Se todos os `zramctl --algorithm` falharem, `mkswap /dev/zram0` pode executar em device não inicializado.
- **Sugestão:** Checar retorno da cadeia antes do `mkswap`.

### Item 11.3 🟡 BUG-016: `pulse N` não valida se N é numérico
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-led.sh:174`
- **Status:** 🔵 Aberto
- **Descrição:** `cmd_pulse "${2:-3}"` — se N não for numérico, `[ "$i" -lt "$count" ]` falha com erro de aritmética.

### Item 11.4 🔵 BUG-017: Extração frágil do nome da CPU via find+awk
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-cpufreq.sh:10`
- **Status:** 🔵 Aberto
- **Descrição:** `awk -F'/' '{print $6}'` assume profundidade fixa de path.

### Item 11.5 🔵 BUG-038: `cmd_pulse` não restaura `delay_on`/`delay_off`
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-led.sh:136-141`
- **Status:** 🔵 Aberto

### Item 11.6 🔵 BUG-039: `zramctl --algorithm` pode não existir
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-zram.sh:25-27`
- **Status:** 🔵 Aberto
- **Descrição:** Flag `--algorithm` não existe em zramctl < 2.39.

### Item 11.7 🔵 BUG-040: `\r` (CRLF) em chave SSH
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-ssh-setup.sh:35`
- **Status:** 🔵 Aberto
- **Descrição:** CRLF causa dedup falho → chaves duplicadas.

### Item 11.8 🔵 BUG-041: Regex de interface USB Ethernet muito larga
- **Arquivo:** `rootfs-overlay/common/usr/local/bin/sanders-network-setup.sh:116`
- **Status:** 🔵 Aberto
- **Descrição:** `/enx|eth0/` matcha "eth0" em qualquer lugar da linha.

### Item 11.9 🔵 BUG-042: Comentário stale no `sanders-led.service`
- **Arquivo:** `rootfs-overlay/common/etc/systemd/system/sanders-led.service:4-5`
- **Status:** 🔵 Aberto (cosmético)
- **Descrição:** Comentário diz que `wait-online` "hoje falha e atrasa ~2min" — problema já corrigido.

---

## 12. 📚 Documentação

### Item 12.1 🟠 BUG-007: Range de memória `reserved@eefe4000` invertido
- **Arquivo:** `docs/HARDWARE_REFERENCE.md:282` (na pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** Mostra range `0xeefe4000-0xeefe0000` (fim < início). O nó tem size 0x1C000.

### Item 12.2 🟠 BUG-008: Instruções Docker sem aviso de rebuild
- **Arquivo:** `docs/SERVER_SETUP_GUIDE.md` (na pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** Guia instrui `systemctl enable --now docker` mas o kernel ainda não foi recompilado com o Netfilter.

### Item 12.3 🟠 BUG-009: Comandos cpufreq assumem driver inexistente
- **Arquivo:** `docs/SERVER_SETUP_GUIDE.md` (na pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor` falha pois cpufreq não funciona no msm8953 mainline.

### Item 12.4 🟡 BUG-022: WPA2 marcado [x] mas handshake ainda falha
- **Arquivo:** `docs/ROADMAP_AND_TODOS.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** Fix WPA2 marcado concluído `[x]` mas handshake ainda falha com `MEM_FAIL=5`. Deveria ser `[~]` (parcial).

### Item 12.5 🔵 BUG-023: Descrição `--any -i usb0` desatualizada
- **Arquivo:** `docs/ROADMAP_AND_TODOS.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** Diz `--any -i usb0`, mas o arquivo real só tem `--any`.

### Item 12.6 🔵 BUG-024: Descrição BT MAC restoration desatualizada
- **Arquivo:** `docs/HARDWARE_REFERENCE.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** Não menciona `script -qc`, wait de 20s para hci0, nem loops de retry.

### Item 12.7 🔵 BUG-025: CPU descrita sem caveat de cpufreq
- **Arquivo:** `docs/HARDWARE_REFERENCE.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto

### Item 12.8 🔵 BUG-026: Power supply ausente da matriz
- **Arquivo:** `docs/HARDWARE_MATRIX.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto

### Item 12.9 🔵 BUG-043: `HARDWARE_REFERENCE.md` ausente da árvore no README
- **Arquivo:** `README.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto

### Item 12.10 🔵 BUG-044: CDC ECM descrito como `/dev/usb0`
- **Arquivo:** `docs/HARDWARE_REFERENCE.md` (pasta anterior do projeto)
- **Status:** 🔵 Aberto
- **Descrição:** Interface de rede não é device file — deveria ser "interface `usb0`".

---

## 📊 Tabela Resumo Consolidada

| ID | Componente | Descrição | Gravidade | Status |
|---|---|---|---|---|
| **BUG-001** | DTS sanders | Framebuffer reg size ≠ cont_splash_mem (corrupção memória) | 🔴 Crítica | ✅ Corrigido |
| **BUG-002** | 05-build-rootfs | multi-user.target.wants criado após symlinks | 🔵 Baixa | ✅ Corrigido (não era build-breaker ativo — verificado) |
| **BUG-003** | 03-build-busybox | make defconfig sempre roda (não incremental) | 🟠 Alta | Aberto |
| **BUG-004** | 05-build-rootfs | sed locale sem erro check | 🟡 Média | Aberto |
| **BUG-005** | server-setup | Exit codes thermal engolidos (`\|\| true`) | 🔵 Baixa | Não é bug — `\|\| true` intencional, texto do alerta continua visível |
| **BUG-006** | bt-mac.sh | `set +e` nunca restaurado | 🔵 Baixa | Aberto (sem consequência prática — verificado) |
| **BUG-007** | HARDWARE_REFERENCE | Range memória invertido | 🟠 Alta | Aberto |
| **BUG-008** | SERVER_SETUP_GUIDE | Docker sem aviso de rebuild | 🟠 Alta | Aberto |
| **BUG-009** | SERVER_SETUP_GUIDE | Comandos cpufreq assumem driver | 🟠 Alta | Aberto |
| **BUG-010** | 07-flash-and-boot | sleep 3 hardcoded | 🟡 Média | Aberto |
| **BUG-011** | 05-build-rootfs | ChallengeResponseAuth deprecated | 🟡 Média | Aberto |
| **BUG-012** | config fragment | CONFIG_I2C/_QUP não builtin | — | ❌ Falso positivo (verificado: já vem `=y` do defconfig) |
| **BUG-013** | config fragment | Backlight configs ausentes | 🟡 Média | Aberto |
| **BUG-014** | network-setup | Senha WiFi no ps | 🟡 Média | Aberto |
| **BUG-015** | zram.sh | mkswap após falha zramctl | 🟡 Média | Aberto |
| **BUG-016** | led.sh | pulse N não valida numérico | 🟡 Média | Aberto |
| **BUG-017** | cpufreq.sh | find+awk campo fixo | 🔵 Baixa | Aberto |
| **BUG-018** | lib.sh | Patches no branch master volátil | 🟠 Alta | Aberto |
| **BUG-019** | 04-initramfs | cpio stderr suprimido | 🟡 Média | Aberto |
| **BUG-020** | 04-initramfs | read -r sem strip \r | 🟡 Média | Aberto |
| **BUG-021** | 02-build-kernel | build todos DTBs | 🟡 Média | Aberto |
| **BUG-022** | ROADMAP | WPA2 [x] mas falha | 🟡 Média | Aberto |
| **BUG-023** | ROADMAP | --any -i usb0 desatualizado | 🔵 Baixa | Aberto |
| **BUG-024** | HARDWARE_REFERENCE | BT MAC doc desatualizado | 🔵 Baixa | Aberto |
| **BUG-025** | HARDWARE_REFERENCE | CPU sem caveat cpufreq | 🔵 Baixa | Aberto |
| **BUG-026** | HARDWARE_MATRIX | power supply ausente | 🔵 Baixa | Aberto |
| **BUG-027** | timesync.sh | msg sucesso mesmo sync falhou | 🔵 Baixa | Aberto |
| **BUG-028** | 09-extract-firmware | path hardcoded | 🟡 Média | Aberto |
| **BUG-029** | bt-mac.sh | `$*` em script -qc | 🟡 Média | Aberto |
| **BUG-030** | DTS | delete-then-redefine redundante | 🔵 Baixa | Aberto |
| **BUG-031** | DTS | ts_reset pinctrl morto | 🔵 Baixa | Aberto |
| **BUG-032** | config fragment | WCN36XX_DEBUG fora de lugar | 🔵 Baixa | Aberto |
| **BUG-033** | patch 0001 | mascara falha hardware | 🟡 Média | Aberto |
| **BUG-034** | 01-lk2nd | clone completo | 🔵 Baixa | Aberto |
| **BUG-035** | 01-lk2nd | fallback HEAD silencioso | 🔵 Baixa | Aberto |
| **BUG-036** | 09-firmware | debugfs arg order | 🔵 Baixa | Aberto |
| **BUG-037** | lib.sh | earlycon sem MMIO | 🔵 Baixa | Aberto |
| **BUG-038** | led.sh | não restaura delay | 🔵 Baixa | Aberto |
| **BUG-039** | zram.sh | --algorithm incompat | 🔵 Baixa | Aberto |
| **BUG-040** | ssh-setup | CRLF em chave | 🔵 Baixa | Aberto |
| **BUG-041** | network-setup | regex USB larga | 🔵 Baixa | Aberto |
| **BUG-042** | led.service | comentário stale | 🔵 Baixa | Aberto |
| **BUG-043** | README | HARDWARE_REFERENCE ausente | 🔵 Baixa | Aberto |
| **BUG-044** | HARDWARE_REFERENCE | /dev/usb0 | 🔵 Baixa | Aberto |
| **BUG-045** | bt-mac.service | ConditionPathExists timing | 🔵 Baixa | Aberto |
| **BUG-KNOWN-01** | battery-guard | CAP vazio | 🟡 Média | Aberto |
| **BUG-KNOWN-02** | battery-guard | Status não verificado | 🔵 Baixa | Aberto |
| **BUG-KNOWN-03** | timesync | DNS retardo | 🔵 Baixa | Aberto |
| **BUG-KNOWN-04** | timesync | parsing timedatectl | 🔵 Baixa | Aberto |
| **BUG-KNOWN-05** | qcom-wdt/sleep | reset em suspend | 🟠 Alta | ✅ Corrigido |
| **BUG-KNOWN-06** | bt-mac.service | race bluetoothd | 🟡 Média | Aberto |
| **BUG-KNOWN-07** | server-setup | pacman -Sy | 🟡 Média | ✅ Corrigido |
| **BUG-KNOWN-08** | wcn36xx | HT vs VHT | 🔵 Baixa | Trade-off aceito |

---

## 🔑 Prioridades de Correção (Top 5) — resultado da verificação (2026-09-02)

Verifiquei os 5 itens contra o código/kernel real, não só lidos. Resultado:

1. **BUG-001** (framebuffer size) — ✅ **confirmado real e crítico**, corrigido e recompilado. Ver `drivers/video/fbdev/simplefb.c`: o driver de fato `ioremap_wc` o tamanho cheio do `reg`, não `width*height*stride`.
2. **BUG-012** (config I2C) — ❌ **falso positivo**. Compilei o `.config` real: `CONFIG_I2C=y`/`CONFIG_I2C_QUP=y` já vêm builtin do defconfig arm64, sem depender do fragment.
3. **BUG-002** (multi-user.target.wants) — ⚠️ **não era build-breaker ativo**. A tarball oficial do ArchLinuxARM já vem com esse diretório populado (verificado com `tar tzf`). Corrigido mesmo assim por ser barato e remover a fragilidade implícita.
4. **BUG-005** (thermal exit codes) — ❌ **não é bug**. É o meu próprio fix de uma sessão anterior (evitar que `set -euo pipefail` truncasse o relatório inteiro quando a temperatura passa do threshold). O texto do alerta continua visível no output; só o exit code do *processo* `--status` (dashboard humano, não endpoint de monitoramento) é suprimido, de propósito.
5. **BUG-006** (set +e) — ⚠️ **verdade, mas sem consequência prática**. O script já valida por estado (relê o MAC até bater ou esgotar tentativas), não por exit code do `btmgmt_run`, e não há código depois do `set +e` que herdaria o problema.

**Conclusão da verificação:** de 5 itens marcados como prioridade máxima, só 1 (BUG-001) era de fato crítico e real — e já está corrigido. Os outros 4 eram falsos positivos, severidade superestimada, ou (no caso do BUG-005) uma segunda-adivinhação de um fix que já tinha sido testado e comprovado necessário. Isso não invalida o resto do documento (com 45 itens, não dava pra verificar todos) — mas reforça: tratar este documento como lista de suspeitas a confirmar, não como bugs já certos.

---

*Relatório consolidado em 2026-09-02, integrando auditorias Antigravity + revisão completa. Correções de BUG-KNOWN-05 (sleep) e BUG-KNOWN-07 (pacman -Syu) aplicadas pelo agente anterior. Top 5 verificado por Claude em 2026-09-02 (ver acima) — BUG-001 confirmado e corrigido, BUG-002 corrigido preventivamente, BUG-012/BUG-005 são falsos positivos, BUG-006 é real mas de baixo impacto.*
