# 🔊 UCM2 corrigido na fonte + bug encontrado no `alsaucm` (`_enadev`/`_disdev`)

> **Data:** 2026-09-03
> **Status:** UCM2 corrigido e correto; ativação automática via `alsaucm`
> **não é viável nesta imagem** por um bug aparente do `alsa-lib` 1.2.16 —
> mitigado via `sanders-audio-route.service` (amixer direto).

---

## 1. Objetivo

Depois de conseguir som audível (`docs/archived/AUDIO_RESOLVIDO_SOM_AUDIVEL.md`)
via `amixer` + `sanders-audio-route.service`, o próximo passo natural era
ativar a rota corretamente através do **UCM2** (`alsaucm -c ... set _verb
HiFi` + `set _enadev Speaker`), que é o mecanismo padrão do ALSA para isso
e o que qualquer app "bem comportada" (ou PipeWire/PulseAudio) consulta
para saber como rotear áudio — em vez de depender de csets fixos escritos
à mão no nosso próprio script.

---

## 2. Bug real encontrado e corrigido nos arquivos UCM2 (nossos, não de pacote)

Os arquivos em `/usr/share/alsa/ucm2/Motorola/potter/` e
`/usr/share/alsa/ucm2/codecs/msm8953-wcd/` são autorais deste projeto —
`pacman -Qo` não encontra pacote dono de nenhum deles. Achei dois problemas
reais neles, ambos corrigidos nesta sessão:

### 2.1 Controles de "Voice" inexistentes no `HiFi.conf`
```
cset "name='PRI_MI2S_RX Voice Mixer VoiceMMode1' 1"
cset "name='VoiceMMode1 Capture Mixer TERT_MI2S_TX' 1"
```
Esses controles **não existem** no nosso `amixer -c 0 controls` — sobra de
algum template de referência de um driver com suporte a chamada de voz,
que nosso machine driver customizado (patch `0005`) não implementa. O
`amixer` isolado simplesmente ignora um control inexistente (com um erro
que não aborta o resto do script), mas o `alsaucm` **aborta a `SectionVerb`
inteira** no primeiro `cset` que falha — então isso sozinho já impedia
`set _verb HiFi` de completar corretamente. Removido do `HiFi.conf`.

### 2.2 `RX{1,2,3} Mute Switch` nunca desligado nas sequências de device
Já documentado em `AUDIO_RESOLVIDO_SOM_AUDIVEL.md` — `Mute Switch=on` é o
default do driver e significa mudo ativo, não "unmute habilitado". Como
essas sequências (`SpeakerEnableSeq.conf`, `HeadphonesEnableSeq.conf`,
`EarpieceEnableSeq.conf`) são description autoral nossa, adicionei o
unmute do canal RX correspondente diretamente nelas (e o mute de volta
nos `*DisableSeq.conf`, por simetria/economia de energia):

| Device | RX usado | Enable adiciona | Disable adiciona |
|---|---|---|---|
| Speaker | RX3 | `RX3 Mute Switch=0` | `RX3 Mute Switch=1` |
| Headphones | RX1+RX2 | `RX1/RX2 Mute Switch=0` | `RX1/RX2 Mute Switch=1` |
| Earpiece | RX1 | `RX1 Mute Switch=0` | `RX1 Mute Switch=1` |

---

## 3. Bug remanescente: `alsaucm set _enadev`/`_disdev` falham sempre

Mesmo depois da correção acima, `alsaucm -c motorola-potter set _enadev
Speaker` (e também `-c hw:0`) continua falhando:
```
alsaucm: error failed to set _enadev=Speaker: No such file or directory
```

### Isolamento do problema
1. `alsaucm -c hw:0 dump text` funciona perfeitamente — parsing 100% OK,
   mostra todos os `Verb`/`Device` corretamente resolvidos, incluindo
   `PlaybackPCM "_ucm0001.hw:motorolapotter,0"`.
2. `alsaucm -c hw:0 list _devices/HiFi` também funciona, lista os 6
   devices corretamente.
3. `alsaucm -c hw:0 set _verb HiFi` **funciona** (retorna 0, aplica o
   `SectionVerb.EnableSequence` — confirmado via `amixer cget numid=149`).
4. **Criado um `SectionDevice` de teste mínimo**, sem nenhum `Include`,
   sem `ConflictingDevice`, só um `Value.PlaybackPCM` — e mesmo assim
   `set _enadev TestNoConflict` falha com o mesmo erro genérico. Isso
   prova que **não é conteúdo da nossa config** (nem os `Include`
   Enable/DisableSeq, nem `ConflictingDevice`) — é o mecanismo interno de
   troca de device do `alsaucm`/`alsa-lib` que está quebrado nesta build.
5. `set _disdev` falha identicamente (mesmo texto de erro), mesmo para um
   device nunca habilitado.
6. Sintaxe confirmada correta contra o `man alsaucm` local (`set _enadev
   VALUE` / `set _disdev VALUE`, válidos só depois de `set _verb`, que já
   tinha sido setado com sucesso).

### Ambiente
```
alsa-lib 1.2.16.1-1
alsa-ucm-conf 1.2.16.1-1
alsaucm: version 1.2.16
```

### Hipótese
`_verb` funciona (executa `SectionVerb.EnableSequence` direto), mas
`_enadev`/`_disdev` internamente resolvem um nome de PCM/CTL alias
(`_ucm0001.hw:motorolapotter`, visível no `dump text`) que parece exigir
um registro/plugin de configuração ALSA (`_ucm0001`) que só é criado sob
certas condições — possivelmente um bug real do `alsa-lib` 1.2.16 nesse
caminho de código, ou uma dependência de inicialização que normalmente é
feita pelo PulseAudio/PipeWire e que o `alsaucm` CLI sozinho não replica
nesta versão. Não investigado mais a fundo — fora do escopo desta sessão.

---

## 4. Mitigação atual (funcional, testada com reboot real)

`sanders-audio-route.service` (`sanders-audio-route.sh`) aplica os
`amixer cset` equivalentes diretamente, sem depender do `alsaucm`. Isso
funciona 100% e já foi validado com áudio real e reboot completo (ver
`AUDIO_RESOLVIDO_SOM_AUDIVEL.md`). O ganho desta investigação: **o
`HiFi.conf` e os `*Seq.conf` agora estão corretos e consistentes com a
mitigação** — se o bug do `alsaucm`/`alsa-lib` for corrigido no futuro
(upgrade de pacote), a ativação via UCM2 real vai funcionar sem precisar
tocar em mais nada nesses arquivos.

---

## 5. Próximos passos possíveis (não bloqueiam nada hoje)

1. Testar se o **PipeWire** (já instalado: `pipewire 1.6.8`) consegue
   ativar o device corretamente — o `spa-alsa`/ACP do PipeWire tem sua
   própria implementação do parser UCM2 (historicamente um fork do código
   do PulseAudio, não usa `alsaucm`/`snd_use_case_*` do `alsa-lib`), então
   pode não ter o mesmo bug. Precisa de `wireplumber` (session manager,
   não instalado ainda) para ser útil de verdade.
2. Reportar/procurar issue upstream no `alsa-lib` para essa versão
   (1.2.16.1) sobre `_enadev`/`_disdev` falhando com ENOENT mesmo em
   device mínimo sem `Include`.
3. Se um dia for necessário suporte a múltiplos apps de áudio simultâneos
   (hoje `hw:0,0` é exclusivo — só um processo por vez), isso empurra na
   direção de precisar de um servidor de som (PipeWire) de qualquer jeito,
   o que tornaria o item 1 obrigatório, não só nice-to-have.
