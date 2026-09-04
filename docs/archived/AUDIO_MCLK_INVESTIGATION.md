# 🔊 Investigação: Digital Codec MCLK e PCM Open EINVAL

> **Data:** 2026-09-03
> **Status:** Em investigação — bloqueador restante para áudio
> **Pré-requisito:** SPMI USID 1 já corrigido (ver `AUDIO_SPMI_USID1_INVESTIGATION.md`)

---

## 1. Estado Atual (pós-fix SPMI)

### O que FUNCIONA
- ✅ ADSP remoteproc sobe (`remote processor adsp is now up`)
- ✅ APR/GPR services registram
- ✅ **WCD analog codec probe OK**: `qcom,pm8916-wcd-spmi-codec 200f000.spmi:pmic@1:codec@f000: PMIC REV: 0 CODEC Version: 4`
- ✅ ALSA card registrada: `0 [motorolapotter]`
- ✅ PCM devices: MultiMedia1 (playback), MultiMedia2 (capture), MultiMedia3 (playback)
- ✅ Headset Jack input device: `input: motorola-potter Headset Jack`
- ✅ Mixer controls completos (RX/TX volumes, MUXes, CICs, DEC volumes)
- ✅ Clock `LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE` registrado com `enable_count=1, prepare_count=1`

### O que NÃO funciona
- ❌ **PCM open retorna EINVAL**: `aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 /dev/zero` → `audio open error: Invalid argument`
- ❌ `speaker-test`, `mpg123` — todos falham com o mesmo erro
- ❌ Nenhum som pode ser reproduzido

---

## 2. Diagnóstico do MCLK

### dmesg
```
[    0.960501] msm8916-wcd-digital-codec c0f0000.codec: failed to get mclk
[    1.743720] msm8916-wcd-digital-codec c0f0000.codec: failed to get mclk
```

Duas tentativas de `clk_get(dev, "mclk")` falharam. A primeira é no probe inicial, a segunda numa retry. O clock `LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE` (9.6 MHz) **é eventualmente registrado pelo `q6afecc`** e mostrado no `clk_summary` com `enable_count=1, prepare_count=1`.

### Clock Summary (do device)
```
LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE 1  1  0  9600000  0  0  50000  Y  c0f0000.codec  mclk
```
Formato: `name enable_count prepare_count protect_count rate accuracy phase cycle_shift spinlock hw.spinlock devname con_id`

O clock **está habilitado e preparado**. Mas o "failed to get mclk" indica que `clk_get()` falhou durante o probe.

### Hipótese: Race condition de timing
O `lpass_codec` (c0f0000) faz probe antes do `q6afecc` registrar o clock MCLK. O probe do `lpass_codec` chama `clk_get(dev, "mclk")` que falha porque o provider do clock ainda não existe. O driver faz `deferred probe`, mas o `msm8916-wcd-digital-codec` pode não ter `-EPROBE_DEFER` como retorno — pode retornar outro erro que impede o retry.

---

## 3. DTS `lpass_codec` Node

```dts
lpass_codec: codec@c0f0000 {
    compatible = "qcom,msm8916-wcd-digital-codec";
    reg = <0x0c0f0000 0x400>;
    #sound-dai-cells = <1>;
    clocks = <&q6afecc LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE
                   LPASS_CLK_ATTRIBUTE_COUPLE_NO>,
             <&xo_board>;
    clock-names = "mclk", "ahbix-clk";
    status = "okay";
};
```

O clock `mclk` referencia `&q6afecc` com `LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE`. Se `q6afecc` ainda não registrou esse clock quando `lpass_codec` faz probe, `clk_get()` retorna `-EPROBE_DEFER`.

---

## 4. Por que o PCM open dá EINVAL?

O `aplay` chama `snd_pcm_open()` → ASoC `soc_pcm_open()` → `startup` callback → `msm8916_qdsp6_startup()`.

### `msm8916_qdsp6_startup()` (apq8016_sbc.c:226)
```c
mi2s = qdsp6_dai_get_lpass_id(cpu_dai);
if (mi2s < 0)
    return mi2s;

ret = snd_soc_dai_set_sysclk(cpu_dai, qdsp6_get_bit_clk_id(data, mi2s),
                              MI2S_BCLK_RATE, 0);
if (ret)
    dev_err(card->dev, "Failed to enable LPAIF bit clk: %d\n", ret);
return ret;
```

**Possíveis causas do EINVAL:**
1. `qdsp6_dai_get_lpass_id()` retorna erro negativo
2. `snd_soc_dai_set_sysclk()` falha (clock LPAIF bit clock não disponível)
3. O `codec_dai` (WCD analog) não está pronto para operar

### `apq8016_dai_init()` (apq8016_sbc.c:62)
```c
switch (mi2s) {
case MI2S_PRIMARY:
    // IOMUX setup — OK
case MI2S_QUINARY:
    if (!pdata->quin_iomux)
        return -ENOENT;  // ← se quin_iomux não mapeado, retorna ENOENT
    ...
default:
    return -EINVAL;  // ← se mi2s não é um dos cases acima
}
```

Se o DAI link usa um MI2S diferente de PRIMARY/SECONDARY/TERTIARY/QUATERNARY/QUINARY, retorna EINVAL.

---

## 5. Debugging Recomendado

### 5.1 Habilitar verbose no ASoC
Adicionar ao kernel cmdline:
```
dyndbg="file apq8016_sbc.c +p"
dyndbg="file msm8916_qdsp6*.c +p"
dyndbg="file lpass-cpu.c +p"
```
Ou adicionar `dev_dbg` em `msm8916_qdsp6_startup()` para ver o valor de `mi2s` e o resultado de `set_sysclk`.

### 5.2 Verificar quais clocks LPAIF estão disponíveis
```bash
cat /sys/kernel/debug/clk/clk_summary | grep -i lpaif
```

### 5.3 Verificar se o MCLK clock está realmente habilitado
```bash
cat /sys/kernel/debug/clk/clk_summary | grep -i "INTERNAL_DIGITAL"
```

### 5.4 Testar com `aplay` verbose
```bash
aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 /dev/zero -d 1 --verbose 2>&1
```

### 5.5 Verificar se UCM2 está sendo aplicado
```bash
cat /proc/asound/card0/id
aplay -L | grep default
cat /usr/share/alsa/ucm2/conf.d/motorola-potter/motorola-potter.conf
```

### 5.6 Adicionar debug em `msm8916_qdsp6_startup`
No próximo build, adicionar `dev_info` para ver:
- Valor de `mi2s` (de `qdsp6_dai_get_lpass_id`)
- Resultado de `snd_soc_dai_set_sysclk`
- Se o `cpu_dai->id` confere

---

## 6. Observações Importantes

### O dmesg NÃO mostra erro na abertura
Quando `aplay` tenta abrir o device PCM, **nenhum printk novo aparece no dmesg**. Isso significa que o erro pode estar:
1. Antes do callback `startup` (no core do ASoC)
2. Na verificação de capabilities do PCM device
3. Na configuração do ALSA library/UCM2

### `aplay -D hw:0,0` vs `aplay -D default`
- `hw:0,0` abre o device ALSA diretamente
- `default` usa a configuração em `/usr/share/alsa/ambas` ou UCM2
- Ambos falham com EINVAL, então o problema é no driver/kernel, não na configuração ALSA

### A card foi probeada em 2 etapas
1. Primeiro probe: `lpass_codec` tenta `clk_get("mclk")` → falha
2. Segundo probe: provavelmente deferred → eventualmente probe OK
3. Sound card registra: `c051000.sound-card`

Mas o fato de o clock estar `enable_count=1` sugere que o codec foi habilitado em algum ponto. O problema pode ser que o **PCM open não consegue configurar os clocks necessários para a streaming path**.

---

## 7. Arquivos Relevantes

- **Machine driver:** `sound/soc/qcom/apq8016_sbc.c` (nossa versão com patch 0005)
- **Digital codec:** `sound/soc/codecs/msm8916-wcd-digital.c`
- **LPASS CPU DAI:** `sound/soc/qcom/lpass-cpu.c`
- **LPASS platform:** `sound/soc/qcom/lpass-platform.c`
- **Q6AFECC clocks:** `sound/soc/qcom/qdsp6/qdsp6dsp-lpass-clocks.c`
- **DTS lpass_codec:** `sanders-linux-mainline/dts/msm8953-motorola-sanders.dts` (~linha 694)
- **DTS sound-card:** `sanders-linux-mainline/dts/msm8953-motorola-sanders.dts` (~linha 706)
- **UCM2:** `sanders-linux-mainline/rootfs-overlay/common/usr/share/alsa/ucm2/conf.d/motorola-potter/`
- **Patch 0005:** `sanders-linux-mainline/kernel/0005-sound-qcom-apq8016-sbc-msm8953-support.patch`

---

## 8. Resumo para o Próximo Agente

O **SPMI USID 1 está corrigido** — o WCD analog codec faz probe. O **próximo bloqueador** é o PCM open que retorna EINVAL. As evidências sugerem que:

1. O clock MCLK eventualmente fica disponível (clock_summary mostra `enable_count=1`)
2. Mas o PCM open ainda falha
3. O erro pode estar no `startup` callback (`msm8916_qdsp6_startup`) ou antes dele
4. **Não há prints no dmesg quando aplay tenta abrir** — precisa de debug adicional

**Abordagem recomendada:**
1. Adicionar `dev_info` em `msm8916_qdsp6_startup()` e em `apq8016_dai_init()` para ver o fluxo de abertura
2. Verificar se o `cpu_dai->id` confere com algum case do switch
3. Verificar se `qdsp6_dai_get_lpass_id()` retorna erro
4. Checar se o clock LPAIF bit clock está disponível para `snd_soc_dai_set_sysclk`
