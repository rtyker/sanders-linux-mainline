# 🔊 Investigação: Botões de Volume Laterais

> **Data:** 2026-09-03
> **Status:** ✅ Confirmado ao vivo em 2026-09-04 — ambos os botões funcionam
> **Contexto:** Áudio PCM já funciona, usuário quer controle de volume via botões físicos

## 0. Confirmação ao vivo (2026-09-04)

Os dois fixes deste documento (pinctrl `gpio_key_default` + `CONFIG_POWER_RESET_QCOM_PON=y`)
acabaram commitados junto com um commit de outra frente de trabalho (GPU) e só foram
efetivamente rebuildados/deployados/testados nesta data — mas funcionam:

- **Volume Down** (`pm8941_resin`, `/dev/input/event1`, `code=114`/`KEY_VOLUMEDOWN`):
  capturado ao vivo — 20 pares de press/release limpos, sem bounce.
- **Volume Up** (`gpio-keys`, `/dev/input/event4`, `code=115`/`KEY_VOLUMEUP`):
  capturado ao vivo — 17 pares de press/release limpos, sem bounce.
- **Bônus confirmado no mesmo teste:** o botão **Power** (`pm8941_pwrkey`) também
  está vivo — um toque disparou o `poweroff` padrão do systemd-logind
  (`shutdown[1]: Powering off` / `reboot: Power down` no dmesg). Cuidado ao testar:
  um único toque desliga o aparelho de verdade, não é só um evento de input inofensivo.

Os eventos raw (`type=1 EV_KEY`) foram capturados via
`cat /dev/input/eventN > arquivo.bin` e decodificados em Python com
`struct.unpack('qqHHi', chunk)` por registro de 24 bytes (`tv_sec`, `tv_usec`, `type`,
`code`, `value`) — mesma abordagem sugerida na seção 5 abaixo, só que via arquivo
binário em vez de `od` direto (mais fácil de decodificar os campos de tamanhos
mistos u16/u16/s32).

Pendência real que sobra: nenhum daemon consome esses eventos ainda (sem
`acpid`/`systemd-logind` com handler de volume configurado) — os botões geram o
evento de input corretamente, mas nada no userspace ainda muda o volume do ALSA/
PipeWire em resposta. Ver nota final da seção 6.

---

## 1. Estado Atual dos Input Devices

Dispositivos detectados em `/proc/bus/input/devices`:

| Device | Nome | Tipo | Events |
|--------|------|------|--------|
| event0 | ft5436-sanders | Touchscreen | ABS |
| event1 | motorola-potter Headset Jack | ALSA Jack | KEY/SW |
| event2 | gpio-keys | GPIO keys | KEY (1 tecla) |

### `gpio-keys` (event2) — Volume UP
- **GPIO:** 85 (TLMM), `GPIO_ACTIVE_LOW`
- **Key code:** `KEY_VOLUMEUP` (115)
- **Pinctrl:** Referencia `<&gpio_key_default>` — mas NÃO definido no DTS!

### `pm8953_resin` — Volume DOWN
- **Compatible:** `qcom,pm8941-resin`
- **Key code:** `KEY_VOLUMEDOWN` (114)
- **Driver:** `pm8941-pwrkey` (`CONFIG_INPUT_PM8941_PWRKEY=y`) ✅
- **PON driver:** `qcom-pon` (`CONFIG_POWER_RESET_QCOM_PON=m`) ❌ — MORTO!

---

## 2. Diagnóstico: Volume UP (GPIO 85)

### Teste de GPIO
```bash
# GPIO 85 lido via debug/gpio durante 10s (20 amostras):
gpio85: in high func0 2mA pull up
# Resultado: HIGH durante todo o teste — botão não muda o estado
```

### Causa Raiz: pinctrl ausente
O DTS referencia `&gpio_key_default` mas não define esse pinctrl state:
```dts
gpio-keys {
    pinctrl-0 = <&gpio_key_default>;  /* ← referenciado */
    /* ... mas gpio_key_default não está definido em nenhum lugar do DTS */
};
```

Comparação com `msm8953-flipkart-rimob.dts` (que funciona):
```dts
&tlmm {
    gpio_key_default: gpio-key-default-state {
        pins = "gpio85";
        function = "gpio";
        drive-strength = <2>;
        bias-pull-up;
    };
};
```

### Fix
Adicionado `gpio_key_default` ao nó `&tlmm` em `dts/msm8953-motorola-sanders.dts`:
```dts
gpio_key_default: gpio-key-default-state {
    pins = "gpio85";
    function = "gpio";
    drive-strength = <2>;
    bias-pull-up;
};
```

---

## 3. Diagnóstico: Volume DOWN (pm8953_resin)

### Causa Raiz: driver PON era módulo

`CONFIG_POWER_RESET_QCOM_PON=m` — compilado como módulo.

Este kernel **nunca roda `make modules_install`** e não tem `/lib/modules/`. Portanto:
- `qcom-pon` driver nunca é carregado
- `pon@800` (SPMI pmic@0) fica `waiting_for_supplier`
- O child node `resin` (compatible `qcom,pm8941-resin`) nunca é criado
- Nenhum input device é registrado para volume down

### Evidência
```bash
# Driver existe mas nenhum device bound:
ls /sys/bus/platform/drivers/pm8941-pwrkey/
# Output: bind module uevent unbind  (no device listed)

# pon@800 está waiting:
cat /sys/bus/platform/devices/200f000.spmi:pmic@0:pon@800/waiting_for_supplier
# Output: 1

# Nenhum device de input para resin/volume-down:
cat /proc/bus/input/devices | grep -i resin
# Output: (empty)
```

### Fix
Adicionado ao `kernel/sanders.config.fragment`:
```
# PMIC Power-On (PON) — controla power-key e resin (volume down) no
# pm8953. Sem isso, o pon@800 (qcom,pm8916-pon) fica deferred e o
# node resin (qcom,pm8941-resin) nunca e criado — volume down morto.
CONFIG_POWER_RESET_QCOM_PON=y
```

---

## 4. Arquivos Modificados

| Arquivo | Mudança |
|---------|---------|
| `dts/msm8953-motorola-sanders.dts` | Adicionado `gpio_key_default` pinctrl em `&tlmm` (linha ~462) |
| `kernel/sanders.config.fragment` | Adicionado `CONFIG_POWER_RESET_QCOM_PON=y` |

---

## 5. Verificação Pós-Fix (após rebuild + deploy)

### Volume UP
```bash
# 1. Verificar pinctrl ativo:
cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep "pin 85"
# Esperado: pin 85 (GPIO_85): device gpio-keys function gpio group gpio85

# 2. Testar evento:
# Com audio tocando, pressionar Volume Up:
timeout 10 cat /dev/input/event2 | od -A d -t x1
# Esperado: eventos tipo=1 (EV_KEY) code=115 (VOLUMEUP) value=1/0

# 3. Verificar se DAPM do codec reage (volume muda):
# O som deveria diminuir/aumentar ao pressionar
```

### Volume DOWN
```bash
# 1. Verificar que PON driver probeou:
dmesg | grep -i "pon\|resin\|pwrkey"
# Esperado: mensagens de probe do pm8941-pwrkey

# 2. Verificar input device criado:
cat /proc/bus/input/devices | grep -A5 "resin\|pwrkey"
# Esperado: device com KEY_VOLUMEDOWN

# 3. Testar evento:
timeout 10 cat /dev/input/event3 | od -A d -t x1
# Esperado: eventos tipo=1 code=114 (VOLUMEDOWN)
```

---

## 6. Notas para o Próximo Agente

- **Áudio PCM funciona** (mpg123, speaker-test) — o próximo passo é apenas os botões
- O rebuild precisa produzir **DTB atualizado** (para o pinctrl) + **kernel atualizado** (para o CONFIG_PON)
- O DTB é copiado do `dts/` pelo `06-build-boot.sh`, não pelo `02-build-kernel.sh`
- O deploy usa `10-deploy-boot.sh --reboot` que sobrescreve o cache partition
- Após deploy, testar AMBOS os botões — pode ser que um funcione e o outro não ainda
- Se o botão de volume UP funcionar mas não controlar volume do ALSA, verificar se o userspace (aplay/amixer) precisa de um daemon para reagir a KEY_VOLUMEUP/KEY_VOLUMEDOWN
