# 🔍 Investigação: Falha de Enumeração USID 1 — SPMI PMIC Arbiter (Bloqueador de Áudio)

> **Data de criação:** 2026-09-03
> **Status:** Em investigação
> **Autor:** Buffy (Codebuff) + sessão de debug
> **Bloqueador:** Áudio real impossível sem o codec analógico WCD (PM8953 USID 1)

---

## 1. Resumo do Problema

O codec de áudio analógico WCD do PM8953 vive na **USID 1** do barramento SPMI (`pmic@1 @ 0xf000`), confirmado correto contra o DTB de fábrica real. No kernel mainline via boot `lk2nd`, **essa USID não enumera** — o driver `spmi-pmic-arb` não binda na USID 1, sem mensagem de erro, sem deferred-probe. Sem o codec analógico, não há saída de áudio para speaker, headphone, earpiece ou microfones.

**O que funciona:**
- ✅ ADSP remoteproc sobe (`remote processor adsp is now up`)
- ✅ ALSA card registra (`/proc/asound/cards` → `0 [motorolapotter]`)
- ✅ Mixers/controles existem (`amixer -c 0 controls`)
- ✅ USID 0 (pmic@0) enumera normalmente (reguladores, GPIO, PON, ADC)

**O que NÃO funciona:**
- ❌ USID 1 (pmic@1) não enumera — `wcd_codec` não faz bind
- ❌ `speaker-test` / `aplay` falham com `Playback open error: -22 (EINVAL)`
- ❌ Sem codec analógico → sem caminho de áudio real

---

## 2. Arquitetura do SPMI PMIC Arbiter (MSM8953)

### 2.1 Visão Geral

O SPMI (System Power Management Interface) é um barramento de 2 fios entre o SoC e os PMICs. No MSM8953, há 4 PMICs no barramento:

| USID | Chip | Função |
|------|------|--------|
| 0 | PM8953 | PMIC principal (reguladores, GPIO, ADC, PON) |
| 1 | PM8953 | Codec de áudio WCD (headphone, speaker, earpiece, mics) |
| 2 | PMI8950 | Charger, fuel gauge, USB |
| 3 | PMI8950 | Reguladores, LAB/IBB |

O **SPMI PMIC Arbiter** (`spmi@200f000`) é o controlador que roteia transações SPMI entre o AP (Application Processor) e os PMICs. Ele usa uma **tabela de mapeamento APID → PPID** armazenada em registradores de hardware.

### 2.2 Tabela de Mapeamento APID → PPID

A tabela é a chave para entender o bug. Cada entrada mapeia um **APID** (Arbiter Peripheral ID) para um **PPID** (Peripheral Port ID = `SID << 8 | PID`).

**Registradores da tabela (v2/v3 arbiter):**
- Offset da tabela: `core + 0x800 + 4 * APID`
- Cada registro contém o PPID nos bits `20:8`
- Tabela de ownership: `core + 0x700 + 4 * APID` (quem pode escrever/interrupt)

**Quem popula a tabela:**
- **Android boot normal:** TrustZone/RPM popula a tabela antes do kernel bootar
- **lk2nd boot:** A tabela deve ser preservada pelo hardware (registradores persistentes)

### 2.3 Fluxo de Probe no Kernel

```
spmi_pmic_arb_probe()
  → lê hw_ver do core + 0x0000
  → seleciona ver_ops (v2/v3/v5/v7)
  → spmi_pmic_arb_register_buses()
    → spmi_pmic_arb_bus_init()
      → pmic_arb_init_apid_v1()  [para v2/v3]
        → aloca mapping_table, ppid_to_apid[]
      → irq_domain_add_tree()
      → devm_spmi_controller_add()
        → of_spmi_register_devices()  [cria devices do DT]
          → pmic@0 → spmi_device (USID 0) ✅
          → pmic@1 → spmi_device (USID 1) ← DEVERIA criar aqui
          → pmic@2 → spmi_device (USID 2) ✅
          → pmic@3 → spmi_device (USID 3) ✅
```

**Nota importante:** `of_spmi_register_devices()` cria os SPMI devices **diretamente do device tree**, não da tabela APID. Então os devices `pmic@0` e `pmic@1` são criados independentemente da tabela.

**Onde a tabela APID é usada:**
1. `qpnpint_irq_domain_translate()` → `ppid_to_apid()` — traduz interrupts do DT para hwirq
2. `pmic_arb_offset_v2()` → `ppid_to_apid()` — calcula offset de registro para transações SPMI

**Se a tabela não tem entrada para PPID 0x1F0 (USID 1, PID 0xF0):**
- `pmic_arb_find_apid()` retorna `-ENODEV`
- `ppid_to_apid()` retorna `-ENODEV`
- `qpnpint_irq_domain_translate()` falha → `wcd_codec` não consegue registrar interrupts
- `qcom-spmi-pmic` (MFD) pode falhar no probe dos sub-dispositivos

---

## 3. Análise do Código Fonte

### 3.1 `pmic_arb_find_apid()` (spmi-pmic-arb.c:1089)

```c
static u16 pmic_arb_find_apid(struct spmi_pmic_arb_bus *bus, u16 ppid)
{
    struct spmi_pmic_arb *pmic_arb = bus->pmic_arb;
    struct apid_data *apidd = &bus->apid_data[bus->last_apid];
    u32 regval, offset;
    u16 id, apid;

    for (apid = bus->last_apid; ; apid++, apidd++) {
        offset = pmic_arb->ver_ops->apid_map_offset(apid);
        if (offset >= pmic_arb->core_size)  // ← CORE_SIZE CHECK
            break;

        regval = readl_relaxed(pmic_arb->ver_ops->apid_owner(bus, apid));
        apidd->irq_ee = SPMI_OWNERSHIP_PERIPH2OWNER(regval);
        apidd->write_ee = apidd->irq_ee;

        regval = readl_relaxed(pmic_arb->core + offset);  // ← Lê PMIC_ARB_REG_CHNLn
        if (!regval)      // ← Se registro = 0, pula
            continue;

        id = (regval >> 8) & PMIC_ARB_PPID_MASK;  // ← Extrai PPID do registro
        bus->ppid_to_apid[id] = apid | PMIC_ARB_APID_VALID;
        apidd->ppid = id;
        if (id == ppid) {   // ← Se PPID confere, retorna APID
            apid |= PMIC_ARB_APID_VALID;
            break;
        }
    }
    bus->last_apid = apid & ~PMIC_ARB_APID_VALID;
    return apid;  // ← Retorna 0 (sem APID_VALID) se não encontrou
}
```

**O que verifica:** Lê cada registro `PMIC_ARB_REG_CHNLn` no endereço `core + 0x800 + 4*APID`. Se o registro for zero ou o PPID não conferir, continua. Se `offset >= core_size`, para.

**Para USID 1 (PPID 0x1F0):** Precisa encontrar um registro onde `(regval >> 8) & 0xFFF == 0x1F0`.

### 3.2 Versão do Arbiter

O MSM8953 usa arbiter **v2 ou v3** (baseado em `PMIC_ARB_VERSION` no registro `core + 0x0000`):

| Versão | Faixa | ops |
|--------|-------|-----|
| v1 | `< 0x20010000` | `pmic_arb_v1` |
| v2 | `0x20010000 - 0x2FFFFFFF` | `pmic_arb_v2` |
| v3 | `0x30000000 - 0x4FFFFFFF` | `pmic_arb_v3` |

**v2 e v3 usam a mesma `ppid_to_apid` (`pmic_arb_ppid_to_apid_v2`)** que chama `pmic_arb_find_apid`.

### 3.3 DTS Configuração

**Upstream `msm8953.dtsi`:**
```dts
spmi_bus: spmi@200f000 {
    compatible = "qcom,spmi-pmic-arb";
    reg = <0x0200f000 0x1000>,    // core (4KB)
          <0x02400000 0x800000>,  // chnls
          <0x02c00000 0x800000>,  // obsrvr
          <0x03800000 0x200000>,  // intr
          <0x0200a000 0x2100>;    // cnfg
    reg-names = "core", "chnls", "obsrvr", "intr", "cnfg";
    qcom,ee = <0>;
    qcom,channel = <0>;
};
```

**`core_size` = 0x1000 (4KB)** → APID mapping table vai de `core + 0x800` a `core + 0xFFF` → **512 APIDs máximo** (0x800/4 = 512 offsets restantes).

**Upstream `pm8953.dtsi`:**
```dts
&spmi_bus {
    pmic@0 {
        compatible = "qcom,pm8953", "qcom,spmi-pmic";
        reg = <0 SPMI_USID>;  // USID 0
        // ... pon, vadc, temp-alarm, gpio
    };
    pmic@1 {
        compatible = "qcom,pm8953", "qcom,spmi-pmic";
        reg = <1 SPMI_USID>;  // USID 1
        wcd_codec: codec@f000 {  // ← status = "disabled" por padrão
            compatible = "qcom,pm8916-wcd-analog-codec";
            // ...
        };
    };
};
```

**Nosso DTS:** Habilita `pmic@1` e `wcd_codec` com `status = "okay"`.

---

## 4. Hipóteses da Causa Raiz

### Hipótese 1: Tabela APID não tem entrada para USID 1 (MAIS PROVÁVEL)

**Mecanismo:** A tabela APID → PPID nos registradores `core + 0x800 + 4*N` não contém uma entrada com PPID 0x1F0. `pmic_arb_find_apid()` faz scan linear e não encontra match → retorna sem `APID_VALID`.

**Por que poderia acontecer:**
- TrustZone/RPM do Android popula a tabela antes do kernel
- `lk2nd` pula a inicialização do TrustZone/RPM
- Registradores de hardware perdem o conteúdo após reset do SPMI arbiter
- Ou: a tabela **é** populada mas a entrada para USID 1 está em APID alto (>512) que o scan não alcança

**Como verificar (no device):**
```bash
# 1. Verificar versão do arbiter
dmesg | grep "PMIC arbiter version"
# Deve mostrar "v2" ou "v3" com um hex value

# 2. Dump da tabela APID (via devmem se disponível)
# Cada entry: core + 0x800 + 4*N, PPID = (val >> 8) & 0xFFF
for i in $(seq 0 511); do
    val=$(devmem $((0x0200f800 + i*4)) 32 2>/dev/null)
    if [ "$val" != "0x00000000" ] && [ "$val" != "0x00000000" ]; then
        ppid=$(( (val >> 8) & 0xFFF ))
        sid=$((ppid >> 8))
        pid=$((ppid & 0xFF))
        echo "APID=$i PPID=0x$(printf '%03X' $ppid) SID=$sid PID=0x$(printf '%02X' $pid) raw=$val"
    fi
done
```

### Hipótese 2: Size check (`offset >= core_size`) corta o scan

**Mecanismo:** `pmic_arb_find_apid()` compara `offset` com `core_size`. Se `core_size` for muito pequeno, o scan para antes de chegar ao APID da USID 1.

**Por que poderia acontecer:**
- `core_size` = `resource_size(res)` do registro "core" no DT
- DT define `"core"` como `<0x0200f000 0x1000>` → `core_size` = 0x1000
- APID mapping table começa em offset 0x800 → só 512 bytes restantes → 512 APIDs
- Se USID 1 estiver mapeada em APID > 511, o scan não a alcança

**Como verificar:** O dump da tabela (hipótese 1) também responde isso — se USID 1 existir em APID alto, veremos.

### Hipótese 3: Ownership table indica EE errado

**Mecanismo:** A entry na tabela APID existe, mas `SPMI_OWNERSHIP_PERIPH2OWNER(regval)` retorna um EE diferente de 0 (o nosso `qcom,ee = <0>`). Nesse caso, o `write_ee` não confere e a transação SPMI falha silenciosamente.

**Como verificar:** No dump da tabela, verificar o registro de ownership (`core + 0x700 + 4*APID`).

### Hipótese 4: A entry existe mas está em USID errado (encoding diferente)

**Mecanismo:** O PPID na tabela pode usar um encoding diferente do esperado. Por exemplo, SID=1 pode ser mapeado como PPID com bits diferentes.

**Como verificar:** O dump da tabela mostra o PPID real — comparar com o esperado (0x1F0 para SID=1, PID=0xF0).

---

## 5. Plano de Ação

### Passo 1: Diagnóstico no Device (SEM modificar nada)

```bash
# 1a. Versão do arbiter
dmesg | grep -i "PMIC arbiter"

# 1b. Status SPMI devices
ls /sys/bus/spmi/devices/
# Esperado: 0-00 (USID 0), 0-01 (USID 1), 0-02 (USID 2), 0-03 (USID 3)
# Se 0-01 não existe: o SPMI core não criou o device

# 1c. Devices deferred
cat /sys/bus/spmi/drivers/*/bind 2>/dev/null
ls /sys/bus/spmi/devices/*/driver 2>/dev/null
# Verificar se pmic@1 tem driver bindado

# 1d. Dump da tabela APID (via /dev/mem ou debugfs)
# Precisa de CONFIG_STRICT_DEVMEM=n ou acesso root
```

### Passo 2: Investigar com Dynamic Debug

Habilitar debug no `spmi-pmic-arb.c` antes do boot:
```bash
# No kernel cmdline (extlinux.conf):
dyndbg="file spmi-pmic-arb.c +p"
```

Ou via `/sys/kernel/debug/dynamic_debug/control` (se `CONFIG_DYNAMIC_DEBUG=y`):
```bash
echo 'file spmi-pmic-arb.c +p' > /sys/kernel/debug/dynamic_debug/control
# Reboot para capturar o probe
```

Isso vai mostrar:
- Versão do arbiter detectada
- Entradas da tabela APID que são lidas
- Se `ppid_to_apid` falha para algum PPID

### Passo 3: Comparar com Driver Downstream

O driver `spmi-pmic-arb.c` do Android 3.18 (MSM kernel) pode ter:
- Inicialização adicional de canais
- Configuração de EE diferente
- Quirks específicos para MSM8953

**Fonte:** `https://android.googlesource.com/kernel/msm/+/refs/heads/android-msm-mako-3.4-jb-mr1/drivers/spmi/spmi-pmic-arb.c`

### Passo 4: Solução Potencial

Se a tabela APID estiver vazia/incompleta, as opções são:

**Opção A: Patch no DTS para forçar criação do device**
- Não funciona diretamente — o SPMI core cria devices do DT, mas o access ao hardware depende da tabela APID

**Opção B: Patch no spmi-pmic-arb para bypass da tabela**
- Adicionar fallback: se `ppid_to_apid` falhar, tentar acesso direto usando SID como APID
- Arriscado — pode causar bus hangs se o APID não existir

**Opção C: Inicializar a tabela APID no lk2nd**
- Modificar lk2nd para popular a tabela antes de chainload para o kernel
- Mais robusto, mas requer modificar o bootloader

**Opção D: Usar workaround de pmic@0 (descartado)**
- Mover codec para pmic@0 funciona para criar o card ALSA mas nunca produziu áudio real (valores de registro suspeitos)

---

## 6. Referências

- **Kernel source:** `arch-sanders/kernel/msm8953-mainline/drivers/spmi/spmi-pmic-arb.c`
- **DT binding:** `Documentation/devicetree/bindings/spmi/qcom,spmi-pmic-arb.txt`
- **Upstream msm8953.dtsi:** `arch/arm64/boot/dts/qcom/msm8953.dtsi` (linha 2179)
- **Upstream pm8953.dtsi:** `arch/arm64/boot/dts/qcom/pm8953.dtsi` (linha 37-187)
- **Nosso DTS:** `sanders-linux-mainline/dts/msm8953-motorola-sanders.dts` (linha 620+)
- **Áudio plan:** `docs/AUDIO_SUBSYSTEM_PLAN.md` (seção 4)
- **Downstream Android driver:** `https://android.googlesource.com/kernel/msm/+/refs/heads/android-msm-mako-3.4-jb-mr1/drivers/spmi/spmi-pmic-arb.c`

---

## 7. Achados Adicionais (2026-09-03, sessão de build 7.2)

### 7.1 Versão do Arbiter Confirmada
PostmarketOS issue #2679 (Moto G6, também MSM8953) confirma:
```
spmi: PMIC Arb Version-2 0x20010000
```
Isso significa **arbiter v2** no nosso hardware — usa `pmic_arb_v2` ops.

### 7.2 Comparação com Upstream 7.2
- **`spmi-pmic-arb.c`**: Nenhuma mudança funcional para v2/v3 desde 6.11. A adição em 7.2 é suporte a v8.5 (irrelevante para MSM8953).
- **`apq8016_sbc.c`**: Upstream continua sem suporte a `MI2S_QUINARY`/`use_ibit_clk`/`quin_iomux`. Nosso patch `0005` é necessário e deve aplicar limpo em 7.2.
- **`msm8953.dtsi`**: GPU e IOMMU do MSM8953 adicionados em 7.2 (não afeta áudio).
- **CVE-2026-72257**: Vulnerabilidade em `q6apm.c` (QDSP6 APM) — verificar se afeta nosso stack de áudio.

### 7.3 Resultado dos Diagnósticos no Device (2026-09-03, kernel 7.2)

#### ✅ Tabela APID COMPLETAMENTE CORRETA!
**Hipótese original (tabela APID incompleta) foi REFUTADA.** Dump via mmap de `/dev/mem` em `0x0200f800` mostrou:

```
APID= 43 raw=0x0001f000 SID=1 PID=0xf0 PPID=0x1f0  ← WCD CODEC!
APID= 48 raw=0x0001f100 SID=1 PID=0xf1 PPID=0x1f1
APID= 27 raw=0x00012200 SID=1 PID=0x22 PPID=0x122
APID= 28 raw=0x00012100 SID=1 PID=0x21 PPID=0x121
APID= 41 raw=0x0001f200 SID=1 PID=0xf2 PPID=0x1f2
APID= 42 raw=0x0001f300 SID=1 PID=0xf3 PPID=0x1f3
APID= 44 raw=0x00000800 SID=0 PID=0x08 PPID=0x008  ← PM8953 PON/GPIO
...
(64 entradas não-zero no total)
```

**PPID 0x1F0 (SID=1, PID=0xF0) está mapeado em APID=43.** A tabela de ownership está toda zero (esperado — lk2nd não popula, e EE=0 bate com nosso DT).

#### ✅ SPMI Devices Criados Corretamente
```
/sys/bus/spmi/devices/:
  0-00  → pmic@0  → compatible=qcom,pm8953  → DRIVER=pmic-spmi ✅
  0-01  → pmic@1  → compatible=qcom,pm8953  → NO DRIVER ❌
  0-02  → pmic@2  → compatible=qcom,pmi8950 → DRIVER=pmic-spmi ✅
  0-03  → pmic@3  → compatible=qcom,pmi8950 → DRIVER=pmic-spmi ✅
```

- Todos os 4 devices existem, com of_node correto e compatibles idênticos
- `0-01` NÃO tem `driver_override`, NÃO está em `devices_deferred`
- `0-01` NÃO tem child devices (MFD driver não chamou `devm_of_platform_populate`)

#### ❌ Causa Raiz: MFD Driver `pmic-spmi` Falha Silenciosamente para USID 1

O driver `qcom-spmi-pmic` (MFD, `CONFIG_MFD_SPMI_PMIC=y`) binda em 0-00, 0-02, 0-03 mas **NÃO binda em 0-01**. Tentativa de bind manual:
```
echo '0-01' > /sys/bus/spmi/drivers/pmic-spmi/bind  → exit=1, ZERO saída em dmesg
```

**O bind retorna erro mas não há nenhum printk.** A função `pmic_spmi_probe()`:
1. `devm_regmap_init_spmi_ext()` — se falha, retorna SEM printk
2. `devm_kzalloc()` — se falha, retorna SEM printk
3. Para USID 1: `pmic_spmi_get_base_revid()` → `qcom_pmic_get_base_usid()` → procura pmic@0
4. Se pmic@0 não terminou probe → `-EPROBE_DEFER` → deveria retry → mas NÃO está na deferred list
5. Se `devm_of_platform_populate()` falha → retorna SEM printk

**Não há `CONFIG_DYNAMIC_DEBUG` no kernel** — impossível habilitar debug dinâmico.

#### 🔍 Hipótese Mais Provável (atual)

A probe é chamada, mas falha antes de qualquer printk. Os suspeitos são:
1. **`devm_regmap_init_spmi_ext()` falha** — apesar de ser "só" alocação de memória, pode haver issue com o regmap bus SPMI para devices na USID errada
2. **Race condition na probe**: pmic@1 tenta probe antes de pmic@0 terminar → `-EPROBE_DEFER` → retry falha silenciosamente por outra razão
3. **`qcom_pmic_get_base_usid()` não encontra pmic@0** — mas o DT está correto e ambos devices existem

### 8. CAUSA RAIZ IDENTIFICADA E FIX APLICADO (2026-09-03)

#### 🔍 Causa Raiz: `qcom,pm8953` removido da match table em kernel 7.2

O upstream removeu a entrada `qcom,pm8953` da tabela `pmic_spmi_id_table` em `drivers/mfd/qcom-spmi-pmic.c`. Isso fez com que o match caísse no fallback `qcom,spmi-pmic` com `N_USIDS(1)`.

**Consequência:**
- Com `num_usids=1`, TODO USID é tratado como base (`usid % 1 == 0`)
- pmic@0 (USID 0): `0 % 1 = 0` → `load_revid` direto → **funciona** (é o base)
- pmic@1 (USID 1): `1 % 1 = 0` → `load_revid` direto → **falha ENODEV** (PPID 0x110 não existe na tabela APID)
- pmic@2 (USID 2): `2 % 2 = 0` → funciona corretamente (N_USIDS=2 para pmi8950)
- pmic@3 (USID 3): `3 % 2 = 1` → busca base de pmic@2 → funciona

#### ✅ Fix Aplicado

Patch `0006-mfd-qcom-spmi-pmic-restore-pm8953-match.patch`: adiciona de volta a entrada `{ .compatible = "qcom,pm8953", .data = N_USIDS(2) }` na tabela de match.

**Resultado pós-fix:**
```
[  0.345520] pmic-spmi 0-00: probe entered, usid=0, num_usids=1 → funciona (base)
[  0.364249] pmic-spmi 0-01: probe entered, usid=1, num_usids=1 → load_revid FAIL (-19)
```  
**ANTES do fix (com num_usids=1):** pmic@1 falha silenciosamente.

**DEPOIS do fix (com num_usids=2):** pmic@1 busca base revid de pmic@0 → funciona!

```
[  2.500593] qcom,pm8916-wcd-spmi-codec 200f000.spmi:pmic@1:codec@f000: PMIC REV: 0  CODEC Version: 4
[  4.514147] input: motorola-potter Headset Jack as .../sound/card0/input1
```

✅ **WCD analog codec probeou com sucesso!**
✅ **ALSA card registrada: `0 [motorolapotter]`**
✅ **Playback devices: MultiMedia1, MultiMedia3**
✅ **Capture device: MultiMedia2**
✅ **Headset Jack input device registrado**

#### ⚠️ Próximo Bloqueador: Digital Codec MCLK

```
msm8916-wcd-digital-codec c0f0000.codec: failed to get mclk
```

O `lpass_codec` (c0f0000) precisa do clock MCLK do `q6afecc` (QDSP6 AFE clock controller). Esse clock provavelmente não está disponível no momento do probe porque o ADSP ainda não registrou os clocks. Isso impede o PCM open (aplay/speaker-test retornam EINVAL).

**Próximo passo:** investigar o timing do clock `q6afecc` e garantir que o MCLK está disponível quando o `lpass_codec` faz probe.

**Confirmado ao vivo 2026-09-03 (Claude, pós build+deploy do fix 0006):** testado com arquivo MP3 real (5MB, não o tom sintético de 440Hz) via `aplay` e `mpg123 -a hw:0,0` — mesmo resultado em ambos: o dispositivo ALSA nem abre (`ALSA: Couldn't open audio device: Invalid argument` / `audio open error: Invalid argument`). Confirma que o bloqueador é no **open do PCM device** (kernel/driver, MCLK), não em decodificação/formato — um arquivo real se comporta identico ao teste sintético.

### 7.4 Kernel Config Relevantes
```
CONFIG_SPMI=y
CONFIG_SPMI_MSM_PMIC_ARB=y  ← SPMI arbiter (probes OK)
CONFIG_MFD_SPMI_PMIC=y      ← MFD driver (não binda em 0-01)
CONFIG_REGMAP_SPMI=y         ← Regmap SPMI backend
CONFIG_PINCTRL_QCOM_SPMI_PMIC=y
CONFIG_REGULATOR_QCOM_SPMI=y
# CONFIG_DYNAMIC_DEBUG is not set  ← sem debug dinâmico!
```

---

## 8. Histórico de Tentativas

| Data | Tentativa | Resultado | Observação |
|------|-----------|-----------|------------|
| 2026-09-03 | Mover wcd_codec para pmic@0 (USID 0) | ❌ Card ALSA registrava mas áudio real impossível | Valores de registro suspeitos (0xFFED), nunca teria produzido áudio |
| 2026-09-03 | Manter wcd_codec em pmic@1 (USID 1, correto) | ❌ USID 1 não enumera | Driver spmi-pmic-arb não binda, sem erro, sem deferred |
| 2026-09-03 | Decompilar DTB de fábrica para confirmar USID | ✅ Confirmado: codec real vive em pm8953@1 | IRQ names batem 1:1 com nosso DTS |
| 2026-09-03 | Verificar devices_deferred | ✅ pmic@1 não aparece | Não é deferred — simplesmente não existe como device |
| 2026-09-03 | Pesquisa upstream 7.2 + postmarketOS | ✅ Nenhuma mudança funcional para SPMI v2 | Patch 0005 deve aplicar limpo |
| 2026-09-03 | Dump APID table via mmap /dev/mem | ✅ **Tabela CORRETA!** PPID 0x1f0 em APID=43 | Hipótese original REFUTADA |
| 2026-09-03 | Verificar SPMI devices | ✅ 0-01 existe com DT correto | Mas pmic-spmi não binda |
| 2026-09-03 | Manual bind echo > bind | ❌ exit=1, ZERO printk | Probe falha silenciosamente |
| 2026-09-03 | Verificar devices_deferred | ✅ 0-01 NÃO está deferred | Nem defer nem success |
| 2026-09-03 | Verificar driver_override / DT | ✅ DT idêntico para 0-00 e 0-01 | Mesmo compatible, mesmo result diferente |
| 2026-09-03 | Debug patch com dev_info em pmic_spmi_probe | ✅ **CAUSA RAIZ: num_usids=1 para pm8953** | qcom,pm8953 removido da match table em 7.2 |
| 2026-09-03 | Fix: restaurar qcom,pm8953 N_USIDS(2) | ✅ **WCD codec probe OK!** ALSA card registrada | Próximo: digital codec MCLK |

---

*Este documento será atualizado conforme a investigação progride.*
