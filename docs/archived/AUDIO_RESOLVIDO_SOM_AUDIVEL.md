# 🔊 RESOLVIDO: Áudio Audível Confirmado — 2026-09-03

> **Status:** ✅ Resolvido — primeiro som real ouvido no aparelho, via alto-falante,
> com um MP3 real (`/home/anderson/Música`).
> **Cadeia de bugs, na ordem em que foram corrigidos:**
> 1. `AUDIO_SPMI_USID1_INVESTIGATION.md` — match table `qcom,pm8953` ausente no MFD
> 2. `AUDIO_MCLK_INVESTIGATION.md` — PCM open EINVAL por falta de rota DAPM FE→BE
> 3. `AUDIO_NO_SOUND_OUTPUT_INVESTIGATION.md` — PCM abre e roda, mas sem som audível
> 4. **Este documento** — causa raiz final e confirmação

---

## Causa raiz final: dois problemas empilhados, ambos fora do kernel

O PCM abrir e tocar (`PCM state: RUNNING`, `hw_ptr` avançando, zero erros no
dmesg) **não implica que o caminho DAPM até o alto-falante/fone esteja
energizado**. Faltavam dois passos manuais que o `alsa-ucm2` normalmente
aplicaria via `alsactl`/`alsaucm`, mas que nunca estavam sendo carregados
neste sistema (não há `alsa-ucm-conf` ativo por padrão em `hw:0,0` direto):

### 1. Rota do dispositivo de saída (`SectionDevice."Speaker"` do UCM2)
O `SectionVerb.EnableSequence` do UCM2 (`numid=149`,
`'PRI_MI2S_RX Audio Mixer MultiMedia1'`) só libera o roteamento
**front-end → back-end** no DSP (é o que resolve o EINVAL do
`AUDIO_MCLK_INVESTIGATION.md`). Ele **não** liga o caminho físico
analógico até o alto-falante. Isso está em
`SectionDevice."Speaker".Include.ses` →
`/usr/share/alsa/ucm2/codecs/msm8953-wcd/SpeakerEnableSeq.conf`:
```
cset "name='SPK DAC Switch' 1"
cset "name='RX3 MIX1 INP1' RX1"
```
Sem isso, `RX3 MIX1 INP1` fica em `ZERO` (confirmado pelo outro agente em
`AUDIO_NO_SOUND_OUTPUT_INVESTIGATION.md` seção 2) e o DAC do alto-falante
nunca recebe o sinal do DSP — mesmo com o PCM "tocando" o buffer é
descartado no caminho analógico.

### 2. Semântica invertida do `RX{1,2,3} Mute Switch`
**Este foi o erro que o outro agente cometeu** (ver
`AUDIO_NO_SOUND_OUTPUT_INVESTIGATION.md`, tabela da seção 2, linhas
"RX1/RX2/RX3 Mute | ... | ✅ setado on"): o nome `Mute Switch` engana —
`values=on` **significa mudo ativo**, não "unmute habilitado". O default
do driver já vem com os três em `on` (mudo). Setar para `on`
intencionalmente (achando que ativava o unmute) manteve a saída
silenciada mesmo com toda a rota DAPM correta. A correção é o oposto:
```bash
amixer -c 0 cset name='RX1 Mute Switch' 0
amixer -c 0 cset name='RX2 Mute Switch' 0
amixer -c 0 cset name='RX3 Mute Switch' 0
```

---

## Sequência completa que produziu som real

```bash
# 1. Rota FE->BE no DSP (resolve o EINVAL do PCM open)
amixer -c 0 cset name="PRI_MI2S_RX Audio Mixer MultiMedia1" 1

# 2. Rota física até o alto-falante (SectionDevice Speaker do UCM2)
amixer -c 0 cset name="RX3 MIX1 INP1" "RX1"
amixer -c 0 cset name="SPK DAC Switch" 1

# 3. Desmutar de fato (semântica invertida!)
amixer -c 0 cset name="RX1 Mute Switch" 0
amixer -c 0 cset name="RX2 Mute Switch" 0
amixer -c 0 cset name="RX3 Mute Switch" 0

# 4. Volume (0..124, ~-84dB a 0dB, step 1dB) — 50 é um nível moderado
amixer -c 0 cset numid=1 -- 50   # RX1 Digital Volume
amixer -c 0 cset numid=2 -- 50   # RX2 Digital Volume
amixer -c 0 cset numid=3 -- 50   # RX3 Digital Volume

# 5. Persistir via alsactl (systemd alsa-restore.service já é padrão e
#    ativo — só precisa gravar o estado uma vez)
alsactl store
```

Testado com um arquivo MP3 real (5MB, copiado de `/home/anderson/Música`)
via `mpg123 -a hw:0,0 arquivo.mp3` — **som audível confirmado pelo usuário
em volumes 50 e 70**, tanto logo após aplicar os `amixer` quanto **depois
de um reboot completo do aparelho** (o `alsa-restore.service`, padrão do
`alsa-utils` e já habilitado, restaura o `/var/lib/alsa/asound.state`
automaticamente no boot — não precisa de nenhum script extra).

---

## Por que o outro agente concluiu "sem som" mesmo com a rota certa

Comparando o estado que eles reportaram
(`AUDIO_NO_SOUND_OUTPUT_INVESTIGATION.md`, seção 2) com o que realmente
funcionou:

| Control | Eles setaram | Necessário | Efeito do erro |
|---|---|---|---|
| `RX1/2/3 MIX1 INP1` | `RX1` (correto) | `RX1` | OK |
| `SPK DAC Switch` | `on` (correto) | `on` | OK |
| `RX1/2/3 Mute Switch` | **`on`** | **`off`** | 🔴 Mantinha a saída muda |

Ou seja, eles chegaram a configurar corretamente a rota DAPM completa
(inclusive descobrindo o `RX3 MIX1 INP1 = ZERO` como suspeito, seção 2 do
doc deles) mas erraram a última etapa por causa do nome ambíguo do
control (`Mute Switch` soa como "habilita o mute" em vez de "ativa o
estado mudo").

As hipóteses levantadas na seção 6 do documento deles (DAPM widgets todos
`power=0`/`N/A`, `dai_link_wcd This is null`, MCLK, charge pump) **não
eram a causa real** — eram sintomas colaterais de investigar via
`/sys/kernel/debug/asoc/.../dapm/*/power`, que nem sempre reflete o
estado real de um DAPM path simples como este (alguns widgets desse
driver não expõem arquivo `power` — daí o `N/A`). Não é necessário
investigar essas hipóteses mais a fundo; o áudio já está confirmado
funcionando ponta a ponta.

---

## Pendências (não bloqueiam áudio funcional, mas ficam como próximos passos)

1. **Debug prints em `sound/core/pcm_native.c`** (`pr_info("SANDERS-DEBUG: ...")`,
   3 linhas em `snd_pcm_open_substream()`) — adicionados durante o
   diagnóstico do EINVAL, ainda presentes no working tree do kernel
   (`build/linux`), **não commitados** (arquivo core, fora de qualquer
   patch rastreado). Devem ser revertidos no próximo rebuild do kernel,
   já que cumpriram seu papel de diagnóstico.
2. **UCM2 não é ativado automaticamente** — hoje a rota é aplicada
   manualmente via `amixer` + persistida via `alsactl`. O caminho mais
   correto a médio prazo é ativar o verbo UCM2 (`alsaucm -c motorola-potter
   set _verb HiFi` + `SectionDevice."Speaker"`), que aplicaria tanto o
   `SectionVerb.EnableSequence` quanto o `SpeakerEnableSeq.conf`
   automaticamente para qualquer app que use `default`/UCM em vez de
   `hw:0,0` direto. Não é urgente: `alsa-restore.service` já garante que
   o estado atual (equivalente ao resultado de aplicar UCM2 completo)
   sobrevive a reboots.
3. **Fone de ouvido / earpiece** — só o Speaker foi testado e confirmado.
   A sequência de `HeadphonesEnableSeq.conf`/`EarpieceEnableSeq.conf`
   (ver `HiFi.conf`) segue o mesmo padrão e deve funcionar de forma
   análoga, mas não foi testada ao vivo.
