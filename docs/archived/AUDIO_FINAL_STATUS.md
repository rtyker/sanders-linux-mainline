# 🔊 Status Final do Subsistema de Áudio — Sessão 2026-09-03

> **Data:** 2026-09-03
> **Kernel:** 7.2.0-dirty #41
> **Agentes:** Buffy (debug SPMI/MFD) + Agente anterior (patches drm/wifi/touchscreen/debug)

---

## 1. Correções Implementadas (nesta sessão)

### Patch 0005: Machine Driver MSM8953 QDSP6
`sanders-linux-mainline/kernel/0005-sound-qcom-apq8016-sbc-msm8953-support.patch`

Modificações em `sound/soc/qcom/apq8016_sbc.c`:
- `MI2S_COUNT` expandido de `MI2S_QUATERNARY+1` para `MI2S_QUINARY+1`
- `quin_iomux` mapeado no probe
- `use_ibit_clk` flag para MSM8953 (usa clocks IBIT em vez de LPAIF_BIT_CLK)
- `msm8953_qdsp6_add_ops()` com entry na of_device_id table
- `MI2S_QUINARY` case no `apq8016_dai_init()`
- `QUINARY_MI2S_RX/TX` no `qdsp6_dai_get_lpass_id()`
- `qdsp6_get_bit_clk_id()` com IBIT mapping
- HACK para codecs externos: `SND_SOC_DAIFMT_BC_FC | SND_SOC_DAIFMT_I2S` no Quinary

### Patch 0006: Restaurar qcom,pm8953 na Match Table
`sanders-linux-mainline/kernel/0006-mfd-qcom-spmi-pmic-restore-pm8953-match.patch`

Modificação em `drivers/mfd/qcom-spmi-pmic.c`:
- Adicionado `{ .compatible = "qcom,pm8953", .data = N_USIDS(2) }` na `pmic_spmi_id_table`
- **Resolução:** Sem esta entrada, o fallback `qcom,spmi-pmic` (N_USIDS=1) causava USID 1 ser tratada como base, falhando com ENODEV

### Debug Prints (kernel #41, adicionados pelo agente anterior)
`sound/core/pcm_native.c`:
- 3x `pr_info("SANDERS-DEBUG: ...")` em `snd_pcm_open_substream()`
- Resultado: todos retornam `err=0` → PCM open funciona no kernel

---

## 2. Outros Patches Aplicados (agente anterior)

| Arquivo | Mudança |
|---------|---------|
| `drivers/input/touchscreen/edt-ft5x06.c` | HACK: FT5436 assume defaults se identify falha |
| `drivers/net/wireless/ath/wcn36xx/smd.c` | Fix: NOVHT fallback se firmware não grant DOT11AC |
| `drivers/gpu/drm/panel/` | Novos drivers: BOE BS052FHM + Tianma TL052VDXP02 |
| `arch/arm64/boot/dts/qcom/pmi8950.dtsi` | Charger + fuel-gauge nodes (disabled) |

---

## 3. Estado Atual do Áudio

### ✅ O que funciona
- ADSP remoteproc sobe
- WCD analog codec probe OK: `PMIC REV: 0  CODEC Version: 4`
- WCD digital codec probe OK (MCLK eventualmente disponível)
- ALSA card `motorolapotter` registrada
- PCM devices: MultiMedia1 (playback), MultiMedia2 (capture), MultiMedia3 (playback)
- Headset Jack input device
- **mpg123 pode abrir o device PCM e tocar** ( PCM state: RUNNING )
- Mixer controls completos (HPHL, HPHR, RX volumes, routing mixers)

### ❌ O que NÃO funciona (ainda)
- `aplay -D hw:0,0` retorna `Device or resource busy` quando mpg123 está rodando (esperado)
- **Audio pode não ser audível** — precisa testar com fones de ouvido ou speaker
- UCM2 profiles não estão sendo aplicados automaticamente (mpg123 usa `hw:0,0` direto)

### ⚠️ Configuração de Mixer Manual Necessária
Para ouvir áudio via `hw:0,0` (sem UCM2), precisa configurar manualmente:
```bash
# Roteamento: MultiMedia1 → Primary MI2S RX
amixer -c 0 sset 'PRI_MI2S_RX Audio Mixer MultiMedia1' on

# Volume RX
amixer -c 0 sset 'RX1 Digital' 84

# Headphones
amixer -c 0 sset 'HPHL' Switch
amixer -c 0 sset 'HPHR' Switch
amixer -c 0 sset 'Headphone Jack' on

# Ou Speaker
amixer -c 0 sset 'SPK DAC' on
```

---

## 4. Próximos Passos

1. **Teste de áudio audível** — conectar fones de ouvido ou verificar se o speaker produz som
2. **UCM2 auto-aplicação** — configurar ALSA para usar os perfis UCM2 automaticamente
3. **Remover debug prints** de `pcm_native.c` antes do commit final
4. **Commit dos patches** como arquivos `.patch` idempotentes no repo

---

## 5. Arquivos Documentação

| Arquivo | Conteúdo |
|---------|----------|
| `docs/archived/AUDIO_SPMI_USID1_INVESTIGATION.md` | Investigação completa do bug SPMI (21KB) |
| `docs/archived/AUDIO_MCLK_INVESTIGATION.md` | Investigação do bloqueador MCLK/PCM EINVAL (8KB) |
| `docs/archived/AUDIO_FINAL_STATUS.md` | Este arquivo — resumo final |
| `docs/AUDIO_SUBSYSTEM_PLAN.md` | Plano técnico atualizado |
| `docs/ROADMAP_AND_TODOS.md` | Roadmap com status atual |
