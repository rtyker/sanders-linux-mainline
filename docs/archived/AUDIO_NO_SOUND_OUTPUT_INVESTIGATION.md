# 🔊 Investigação: PCM Funciona mas Sem Áudio Audível

> **Data:** 2026-09-03
> **Status:** RESOLVIDO por outro agente — ver `AUDIO_RESOLVIDO_SOM_AUDIVEL.md`
> **Pré-requisito:** SPMI USID 1 corrigido, PCM open EINVAL corrigido (kernel #41)

---

## 1. Estado Confirmado

### O que FUNCIONA ✅
- ALSA card `0 [motorolapotter]` registrada
- `mpg123 -a hw:0,0 teste-real.mp3` → PCM state: RUNNING, hw_ptr avançando
- `speaker-test -D hw:0,0 -t sine -f 440` → PCM state: RUNNING
- Kernel debug (pcm_native.c) mostra `err=0` nos 3 checkpoints
- WCD analog codec probe: `PMIC REV: 0 CODEC Version: 4`
- Digital codec probe: bound to `c0f0000.codec`
- Clock `LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE`: `enable_count=1, prepare_count=1`
- **Zero erros no dmesg durante reprodução**
- `PRI_MI2S_RX Audio Mixer MultiMedia1` = **ON** (rotação FE→BE ativa)

### O que NÃO funciona ❌
- **Nenhum som audível** em fones, earpiece ou speaker
- Todos os widgets DAPM do codec analógico mostram `power=0`
- Todos os widgets DAPM do codec digital mostram `power=0` (N/A nos arquivos)
- Mixer controls `RX1 MIX1 INP1`, `RX2 MIX1 INP1`, `RX3 MIX1 INP1` defaultam para `ZERO`
- `EAR_S` defaulta para `ZERO`

---

## 2. Investigação: Mixer Controls

### Controles encontrados (amixer contents)

**Codec Analógico (200f000.spmi:pmic@1:codec@f000):**
| Control | Default | Necessário para | Status |
|---------|---------|-----------------|--------|
| HPHL | Switch ✅ | Headphone left | OK |
| HPHR | Switch ✅ | Headphone right | OK |
| SPK DAC Switch | on ✅ | Speaker | OK |
| RX1 Digital | 84 (67%) | RX1 gain | Ajustado para 90% |
| RX2 Digital | 84 (67%) | RX2 gain | Ajustado para 90% |
| RX3 Digital | 84 (67%) | RX3 gain | Ajustado para 90% |
| RX1 Mute | off | RX1 unmute | ✅ setado on |
| RX2 Mute | off | RX2 unmute | ✅ setado on |
| RX3 Mute | off | RX3 unmute | ✅ setado on |
| **RX1 MIX1 INP1** | **ZERO** ❌ | Route I2S→RX1 DAC | ✅ setado RX1 |
| **RX2 MIX1 INP1** | **ZERO** ❌ | Route I2S→RX2 DAC | ✅ setado RX1 |
| **RX3 MIX1 INP1** | **ZERO** ❌ | Route I2S→RX3 DAC | ✅ setado RX1 |
| RDAC2 MUX | RX2 ✅ | HPHR DAC routing | OK |
| **EAR_S** | **ZERO** ❌ | Earpiece | ✅ setado Switch |
| Headphone Jack Switch | on ✅ | Jack detection | OK |

**Codec Digital (c0f0000.codec):**
| Control | Default | Status |
|---------|---------|--------|
| RX_MACRO RX0/1/2/3 MUX | N/A | Não existe neste codec |

**Platform/DSP (QDSP6 routing):**
| Control | Default | Status |
|---------|---------|--------|
| PRI_MI2S_RX Audio Mixer MultiMedia1 | **ON** ✅ | FE→BE routing OK |
| RX_CODEC_DMA_RX_0 Audio Mixer MultiMedia1 | off | Não utilizado neste path |
| HDMI/SLIMBUS/USB/TDM mixers | off | Não utilizados |

### ⚠️ IMPORTANTE: Mixer controls NÃO são persistentes!
Os valores de mixer são redefinidos para defaults quando a stream PCM é aberta e fechada. Mas NÓS setamos os mixers ANTES de abrir o mpg123, e mesmo assim não funcionou. Os controls foram confirmados como setados corretamente:
```
RX1 MIX1 INP1: RX1
RX2 MIX1 INP1: RX1
RX3 MIX1 INP1: RX1
HPHL: Switch
HPHR: Switch
RX1 Mute: on
RX2 Mute: on
SPK DAC: on
```

---

## 3. Investigação: DAPM Power States

### Descoberta crítica: TODOS os widgets DAPM estão OFF (power=0)

Verificados via `/sys/kernel/debug/asoc/motorola-potter/{component}/dapm/{widget}/power`:

**Codec Analógico (200f000.spmi:pmic@1:codec@f000):**
- `PDM Playback`: N/A (sem arquivo power ou power=0)
- `A_MCLK`: N/A
- `A_MCLK2`: N/A
- `EAR_HPHL_CLK`: N/A
- `EAR_HPHR_CLK`: N/A
- `RXD1_CLK`: N/A
- `RXD2_CLK`: N/A
- `RXD3_CLK`: N/A
- `RXD_PDM_CLK`: N/A
- `SPKR_CLK`: N/A
- `NCP_CLK`: N/A
- `TXD_CLK`: N/A
- `TXA_CLK25`: N/A

**Codec Digital (c0f0000.codec):**
- `MCLK`: N/A
- `CDC_CONN`: N/A
- `RX_I2S_CLK`: N/A
- `I2S RX1`: N/A
- `PDM_RX1`: N/A
- `RX1 MIX1`: N/A
- `RX1 MIX1 INP1`: N/A
- `RX1 INT`: N/A

Todos os widgets retornam `N/A` (arquivo power não encontrado ou vazio). Isso significa que **o DAPM não está energizando nenhum widget** mesmo com PCM em estado RUNNING.

---

## 4. Investigação: DAPM Route Table do Codec Digital

Do arquivo `msm8916-wcd-digital.c`, a rotação DAPM relevante para playback é:

```
MultiMedia1 PCM → I2S RX1 → RX1 MIX1 INP1 → RX1 MIX1 → RX1 INT → PDM_RX1 → (analog codec) HPHL DAC → HPHL → HPHL PA → saída
```

Supply chain:
```
I2S RX1 ← RX_I2S_CLK ← MCLK (DAPM supply)
I2S RX1 ← RX_I2S_CLK ← CDC_CONN (DAPM supply)
```

**A rota está CONFIGURADA corretamente no DAPM route table do digital codec:**
```c
{"RX1 MIX1 INP1", "RX1", "I2S RX1"},
{"RX1 MIX1", NULL, "RX1 MIX1 INP1"},
{"RX1 INT", NULL, "RX1 MIX1"},
{"PDM_RX1", NULL, "RX1 INT"},
```

**E no codec analógico:**
```c
{"HPHL DAC", NULL, "PDM_RX1"},
{"RDAC2 MUX", "RX2", "PDM_RX2"},
{"HPHL", "Switch", "HPHL DAC"},
{"HPHL PA", NULL, "HPHL"},
{"HPHL PA", NULL, "CP"},
{"HPHL PA", NULL, "RX_BIAS"},
```

---

## 5. Investigação: Drivers e DAI Links

### Machine driver (apq8016_sbc.c)
- `apq8016_dai_init()` configura IOMUX para MI2S_PRIMARY
- `apq8016_sbc_startup()` chama `snd_soc_dai_set_sysclk()` no CPU DAI
- DAI init logs (de sessão anterior):
  ```
  apq8016-sbc:dai_init:0: dai_link_wcd This is null
  apq8016-sbc:dai_init:1: dai_link_wcd This is null
  apq8016-sbc:dai_init:2: dai_link_wcd This is null
  ```
  ⚠️ **"dai_link_wcd This is null"** — o ponteiro para o WCD codec DAI é NULL durante o init!

### DAIs registrados
```
msm8916_wcd_digital_i2s_rx1
msm8916_wcd_digital_i2s_tx1
MultiMedia1, MultiMedia2, MultiMedia3, MultiMedia4
PRI_MI2S_RX, PRI_MI2S_TX
```

---

## 6. Hipóteses para o Silêncio

### Hipótese 1: DAPM não energiza o path (MAIS PROVÁVEL)
**Evidência:** Todos os widgets DAPM mostram power=0/N/A, mesmo com PCM RUNNING.
**Causa possível:** O `MCLK` supply (DAPM widget) não está sendo habilitado porque nenhum consumer está requisitando ativamente. Ou o `CDC_CONN` supply não está energizando.

**Como testar:** Verificar se habilitar manualmente o DAPM via `snd_soc_dapm_sync()` resolveria, ou se há um widget raiz que precisa de ativação.

### Hipótese 2: Clock MCLK do digital codec não está realmente habilitado
**Evidência:** dmesg mostra `failed to get mclk` duas vezes. O clock_summary mostra `enable_count=1` mas isso pode ser do probe inicial, não do streaming.

### Hipótese 3: LPASS DMA não está escrevendo dados reais no I2S
**Evidência:** hw_ptr avança (DMA funciona), mas pode ser DMA para um buffer vazio. O LPASS pode não estar conectado ao codec via I2S.

### Hipótese 4: Machine driver não conectou o WCD codec ao DAI link
**Evidência:** "dai_link_wcd This is null" — se o codec DAI não está no DAI link, o ASoC não cria o path entre CPU DAI e Codec DAI.

### Hipótese 5: CP (charge pump) não está habilitado
**Evidência:** No DAPM route do analog codec: `{"HPHL PA", NULL, "CP"}` — o charge pump é supply obrigatório para os PAs de headphone.

---

## 7. Ações Já Realizadas Nesta Sessão

| # | Ação | Resultado |
|---|------|-----------|
| 1 | SSH para device e verificar mixer controls | Device acessível, card presente |
| 2 | Killall mpg123/speaker-test antigos | OK |
| 3 | Set HPHL=Switch, HPHR=Switch, SPK DAC=on | OK, confirmado |
| 4 | Set RX1/RX2/RX3 Digital = 80% | OK |
| 5 | Play speaker-test 440Hz | PCM RUNNING, hw_ptr advancing, **sem som** |
| 6 | Dump amixer contents completo | Encontrado RX1 MIX1 INP1 = ZERO (problema!) |
| 7 | Set RX1 MIX1 INP1=RX1, RX2 MIX1 INP1=RX1, RX3 MIX1 INP1=RX1 | OK, confirmado |
| 8 | Set RX1/RX2/RX3 Mute=on | OK |
| 9 | Play mpg123 com mixers configurados | PCM RUNNING, **sem som** |
| 10 | Check DAPM power states - analog codec | **Todos OFF** |
| 11 | Check DAPM power states - digital codec | **Todos N/A/OFF** |
| 12 | Check LPASS/routing DAPM | **Todos OFF** |
| 13 | List DAPM widgets do digital codec | 52 widgets listados, todos com power=0 ou N/A |
| 14 | List DAPM widgets do analog codec | ~40 widgets listados, todos com power=0 ou N/A |
| 15 | Verificar DAIs registrados | `msm8916_wcd_digital_i2s_rx1` OK, `PRI_MI2S_RX` OK |
| 16 | Verificar PRI_MI2S_RX Audio Mixer MultiMedia1 | **ON** ✅ — FE→BE routing OK |

---

## 8. O Que o Próximo Agente Deve Fazer

### PASSO 1: Entender por que DAPM está todo OFF
O DAPM deveria energizar os widgets automaticamente quando uma stream PCM é aberta e o path está completo. O fato de todos estarem OFF indica que:
- Ou a stream não está ativando o DAPM (impossível — PCM open funciona)
- Ou há um supply/widget raiz que não está sendo habilitado

**Comando de diagnóstico:**
```bash
# Listar TODOS os widgets DAPM do codec digital com power
ssh root@10.42.0.2 'for w in $(ls /sys/kernel/debug/asoc/motorola-potter/c0f0000.codec/dapm/); do p=$(cat "/sys/kernel/debug/asoc/motorola-potter/c0f0000.codec/dapm/$w/power" 2>/dev/null); echo "$w: $p"; done'

# Listar TODOS os widgets do codec analógico
ssh root@10.42.0.2 'for w in $(ls /sys/kernel/debug/asoc/motorola-potter/200f000.spmi:pmic@1:codec@f000/dapm/); do p=$(cat "/sys/kernel/debug/asoc/motorola-potter/200f000.spmi:pmic@1:codec@f000/dapm/$w/power" 2>/dev/null); echo "$w: $p"; done'

# Verificar bias_level do card
ssh root@10.42.0.2 'cat /sys/kernel/debug/asoc/motorola-potter/*/bias_level 2>/dev/null'
```

### PASSO 2: Investigar "dai_link_wcd This is null"
Esse log indica que o codec DAI não está sendo encontrado durante `apq8016_dai_init()`. Pode ser que o DAI link não está referenciando o WCD codec corretamente.

**Arquivo:** `sound/soc/qcom/apq8016_sbc.c` — função `apq8016_sbc_platform_probe()`

### PASSO 3: Habilitar DAPM verbose logging
```bash
# No kernel cmdline ou via tracefs:
echo 1 > /sys/kernel/debug/tracing/events/asoc/enable
```
Ou adicionar `dev_dbg` no `msm8916_wcd_digital_enable_mclk()` e `msm8916_wcd_digital_enable_cdc_clk()`.

### PASSO 4: Considerar UCM2
Não há UCM2 profile para `motorolapotter`. O Arch Linux não tem perfis UCM para MSM8953. Pode ser necessário criar um ou testar sem UCM com mixer settings manuais.

### PASSO 5: Verificar se o problema é cross-component DAPM
O path `PDM_RX1` é um OUTPUT do codec digital e INPUT do codec analógico. DAPM cross-component pode não estar linkando corretamente. Verificar se os nomes dos DAPM endpoints coincidem exatamente.

---

## 9. Referências

- **Patch 0005:** `kernel/0005-sound-qcom-apq8016-sbc-msm8953-support.patch` — machine driver custom
- **Patch 0006:** `kernel/0006-mfd-qcom-spmi-pmic-restore-pm8953-match.patch` — SPMI fix
- **Codec Digital:** `sound/soc/codecs/msm8916-wcd-digital.c`
- **Codec Analógico:** `sound/soc/codecs/msm8916-wcd-analog.c`
- **Machine Driver:** `sound/soc/qcom/apq8016_sbc.c`
- **LPASS CPU:** `sound/soc/qcom/lpass-cpu.c`
- **DTS:** `dts/msm8953-motorola-sanders.dts` (~linha 694-720)
- **postmarketOS issue #2679:** Confirma PMIC Arbiter v2 em Moto G6 (mesmo SoC)
- **postmarketOS msm8916 audio:** Funciona com machine driver msm8916_qdsp6 (não apq8016_sbc)
