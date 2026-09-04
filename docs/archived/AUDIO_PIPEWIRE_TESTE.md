# 🔊 PipeWire + WirePlumber: teste ao vivo — UCM2 ativado automaticamente

> **Data:** 2026-09-03
> **Status:** ✅ Confirmado — PipeWire contorna o bug do `alsaucm` (ver
> `AUDIO_UCM2_ALSAUCM_BUG.md`) e ativa a rota do Speaker automaticamente.

---

## 1. Por que testar

`alsaucm set _enadev`/`_disdev` está quebrado nesta imagem (alsa-lib
1.2.16.1, ver `AUDIO_UCM2_ALSAUCM_BUG.md`). O PipeWire usa uma
implementação própria do parser UCM2 dentro do `spa-alsa`/ACP
(historicamente um fork do código do PulseAudio `module-alsa-card`, não
passa pelo `snd_use_case_*` do `alsa-lib`), então era plausível que
contornasse o bug. **Confirmado que sim.**

---

## 2. Setup do teste

```bash
pacman -S --needed wireplumber pipewire-pulse pipewire-alsa
loginctl enable-linger alarm   # usuario normal — PipeWire recusa rodar como root
usermod -aG audio alarm
su - alarm -c "
  export XDG_RUNTIME_DIR=/run/user/1000
  systemctl --user start pipewire.socket pipewire-pulse.socket wireplumber.service
"
```

`pipewire.service` tem `ConditionUser=!root` — não roda como root de
propósito (é uma proteção padrão do projeto upstream).

---

## 3. Resultado

Card detectado automaticamente via `udev`/ACP:
```
alsa_card.platform-c051000.sound-card
  api.alsa.card.name = "motorola-potter"
  device.nick = "motorola-potter"
```

Perfis do card, lidos diretamente do nosso `HiFi.conf`/UCM2 (nomes batem
com `SectionDevice` × combinações de captura):
```
HiFi (Mic1, Speaker)      priority: 8500  available: yes
HiFi (Headphones, Mic1)   priority: 8600  available: no (sem jack conectado)
...
```

### Teste decisivo: reset bruto + troca de profile

Resetei os 4 controles-chave pro estado bruto do driver (mudo, rota
desligada) e depois troquei o profile via `pactl`:
```bash
pactl set-card-profile alsa_card.platform-c051000.sound-card off
pactl set-card-profile alsa_card.platform-c051000.sound-card "HiFi (Mic1, Speaker)"
```

Resultado — **os 4 controles voltaram ao estado correto sozinhos**,
sem nenhum `amixer` manual nem `sanders-audio-route.service`:
```
SPK DAC Switch = on
RX3 Mute Switch = off
RX3 MIX1 INP1 = RX1
PRI_MI2S_RX Audio Mixer MultiMedia1 = on
```

Isso só funcionou depois de reimplantar os arquivos UCM2 já corrigidos
(`AUDIO_UCM2_ALSAUCM_BUG.md`, seção 2) — a primeira tentativa (com a
versão antiga do `SpeakerEnableSeq.conf`, sem o unmute) trocou o profile
mas deixou `RX3 Mute Switch=on`, confirmando que o ACP realmente executa
literalmente as sequências do nosso `HiFi.conf`/`*Seq.conf`.

Som real confirmado tocando via `paplay` (decodificado de MP3 real com
`ffmpeg` → WAV, já que `mpg123 -o pulse` travou sem tocar nem dar erro —
não investigado, não é o caminho recomendado de qualquer forma).

---

## 4. Detalhe: `api.acp.auto-profile`/`auto-port` = false

O card aparece com essas duas properties em `false` — é política padrão
do monitor ALSA do WirePlumber (não troca profile sozinho ao detectar o
card, para não fazer pop de áudio em hardware desconhecido). Isso
**não impede** o uso — só significa que o profile precisa ser
selecionado explicitamente uma vez (via `pactl set-card-profile` ou por
uma regra do WirePlumber), não que a ativação em si esteja quebrada.
Diferente do bug do `alsaucm`, que falha mesmo pedindo explicitamente.

---

## 5. Comparação rápida

| | `sanders-audio-route.service` (atual) | PipeWire + WirePlumber |
|---|---|---|
| Ativa a rota via UCM2 de verdade | ❌ (replica csets à mão) | ✅ |
| Robusto a mudança no `HiFi.conf` | ❌ (precisa manter script em sincronia) | ✅ (lê o UCM2 direto) |
| Múltiplos apps de áudio simultâneos | ❌ (`hw:0,0` exclusivo) | ✅ (mixa via PipeWire) |
| Troca automática por hotplug (fone) | ❌ | ✅ (com regras do WirePlumber) |
| Footprint | Zero (só um oneshot no boot) | +2 daemons persistentes (pipewire, wireplumber), ~30MB RAM |
| Precisa de usuário não-root | Não | Sim (`pipewire.service` recusa rodar como root) |

---

## 6. Pendências se isso virar o caminho padrão

1. Adicionar `wireplumber`, `pipewire-pulse`, `pipewire-alsa` ao
   `05-build-rootfs.sh` (hoje só `pipewire`/`libpipewire` estão na
   imagem base).
2. Decidir qual usuário roda a sessão PipeWire (`alarm` já existe por
   default no Arch Linux ARM; ou criar um usuário de sistema dedicado).
3. Habilitar `pipewire.socket`/`pipewire-pulse.socket`/
   `wireplumber.service` no user manager desse usuário
   (`systemctl --user enable`) + `loginctl enable-linger` persistente
   (hoje feito manualmente, precisa ir para o `05-build-rootfs.sh` ou
   um `.service` de sistema equivalente).
4. Decidir o profile default (`HiFi (Mic1, Speaker)`) — hoje precisa de
   `pactl set-card-profile` manual porque `auto-profile=false`; dá pra
   fixar isso com uma regra do WirePlumber (`51-alsa-custom.conf` /
   `wireplumber.conf.d`) ou aceitar selecionar uma vez por boot via um
   script pequeno (mais simples, mesma filosofia do
   `sanders-audio-route.service` atual, só que chamando `pactl` em vez
   de `amixer`).
5. `sanders-audio-route.service` pode continuar existindo como fallback
   de baixo nível (não atrapalha, os csets são os mesmos que o UCM2
   aplicaria) — ou ser removido se PipeWire virar o único caminho.
