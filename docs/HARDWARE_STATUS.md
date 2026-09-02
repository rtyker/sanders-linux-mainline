# Hardware status

Última atualização: 2026-05-21.

## Visão geral

| Componente | Status | Notas |
|---|---|---|
| Kernel mainline boot | ✅ | Linux master (~v6.x), defconfig + fragment |
| eMMC | ✅ | `mmcblk0` 29.1 GiB, GPT, todas 54 partições visíveis |
| ext4 rootfs | ✅ | Montado via `blkid -L rootfs` (sem udev no initramfs) |
| systemd / userspace Arch ARM | ✅ | Boot até shell root (autologin no tty1 via drop-in `getty@tty1.service.d/autologin.conf`) |
| Framebuffer console | ✅ | `simple-framebuffer` do bootloader; rotated portrait |
| USB CDC ACM (console serial via cabo USB) | ✅ | Autologin root via `serial-getty@ttyGS0` do systemd. Estável após drop-in com `TTYReset/Hangup/VTDisallocate=no` + udev no-autosuspend. |
| USB CDC ECM (rede sobre cabo USB) | ✅ | `usb0` no phone @ 10.42.0.2/24, host @ 10.42.0.1/24, SSH `root@10.42.0.2` OK. NAT no host via `scripts/08-host-net.sh`. **Ordem das functions no initramfs importa**: ECM tem que ser registrada *antes* da ACM (kernel 7.1.0-rc4 faz TX stuck do ECM se ACM vier primeiro — qdisc enche e tx_packets fica em 0). Veja TROUBLESHOOTING #13. |
| Touchscreen Focaltech FT5436 | ✅ | Reportando eventos ABS/KEY/SYN limpos. Probe via patch (driver mainline `edt-ft5x06` precisa skip-identify). |
| Wi-Fi (QCA WCN3680B / pronto) | ⚠️ | Hardware OK (scan funciona). Auth+assoc completam. Fallback V0 para `hal_config_bss/sta` preparado em `kernel/0002-wcn36xx-force-v0-for-wcn3680.patch`; resultado WPA2 ainda pendente de teste no aparelho. Veja secao "Wi-Fi WCN3680B" abaixo. |
| USB OTG Ethernet (host mode) | ⚠️ | Suporte implementado no kernel (`CONFIG_USB_USBNET=y`, `CONFIG_USB_NET_CDCETHER=y`, `CONFIG_USB_NET_CDC_NCM=y`, `CONFIG_USB_RNDIS_HOST=y`). Config systemd-networkd (`30-en-eth.network`) e script `sanders-network-setup.sh` prontos. **Pendente:** recompilar kernel e testar adaptador USB-C Ethernet ao vivo. |
| Bluetooth WCN3680B | ✅ | Mesmo chip do Wi-Fi (Pronto). Driver mainline `btqcomsmd` sobe limpo, `hci0` ativo, BR/EDR + BLE OK, scan/discovery/pareamento validados. DT (`qcom,wcnss-bt`) já vem pronto no `msm8953.dtsi` como subnode de `wcnss/smd-edge/wcnss_ctrl` — só precisou habilitar `CONFIG_BT*` + `CONFIG_BT_QCOMSMD=y`. **MAC de fábrica restaurado** via `sanders-bt-mac.service` (lê `/persist/bluetooth/.bt_nv.bin`, formato NV Qualcomm, e injeta no controller via `btmgmt public-addr` antes do `bluetooth.service`). OUI Motorola real preservada — pareamento agora persiste entre boots. |
| Display "de verdade" (painel Tianma NT35596 ou DJN ILI7807D) | ❌ | Driver mainline inexistente. `simple-framebuffer` do bootloader exposto como `/dev/dri/card0` via `DRM_SIMPLEDRM` — suficiente pra Weston/Wayland rodar em SW renderer (pixman), sem aceleração. |
| Wayland (Weston) | ✅ | Compositor DRM rodando sobre SimpleDRM, output 1080×1920@60 `transform=rotate-270`, touch FT5436 + gpio-keys funcionais. Renderer pixman (CPU). Disponível no flavor `desktop` mas **não habilitado** por default (Phosh é o default). Adreno 506 ocioso até termos driver de painel real + freedreno. |
| Phosh + Phoc + Squeekboard | ✅ | Shell mobile estilo Android (lockscreen, app drawer, painel). Compositor Phoc (wlroots) com `WLR_RENDERER=pixman` (EGL/GBM em simpledrm não fecha o ciclo de buffers — veja TROUBLESHOOTING #16). Squeekboard como OSK integrado, aparece em qualquer cliente Wayland que ative `text-input-v3` ou `virtual-keyboard-v1`. `scale=3` no `phoc.ini` (painel ~400 DPI). Default no flavor `desktop`. |
| OpenGL via Xwayland | ✅ | `glxgears` ~140 FPS via Mesa 26 / llvmpipe (LLVM 22.1, software rasterizer 128-bit) sobre Xwayland sobre Weston. `glxinfo`: OpenGL 4.5 Core, GLES 3.2, direct rendering yes. Habilitado no `weston.ini` (`xwayland=true`). Apenas no flavor `desktop`. |
| Áudio | ❌ | — |
| Modem (telefonia/dados) | ❌ | — |
| Câmera | ❌ | — |
| Proximidade + luz ambiente (LiteON LTR559) | ⚠️ | I2C @ bus 1 / 0x23 probou OK no driver mainline `ltr501`. `/sys/bus/iio/devices/iio:device0/in_proximity_raw` reage a obstrução (variações 700–960). ALS (`in_intensity_*`, `in_illuminance_input`) trava em 0 — driver inicializa ALS_CONTR mas o chip não emite ALS_RDY no STATUS. Calibração de threshold de proximidade ainda crua (stock usava `ps-threshold=800`, mainline binding só expõe `proximity-near-level` semântico). |
| Acelerômetro / giroscópio / magnetômetro | ❌ | **Não** estão em I2C no stock — passam pela SSC (Sensor Subsystem) no SLPI/DSP, caminho proprietário (`sns_dsps` blob + IPC QMI). Sem driver mainline prático hoje. Bloqueia autorotate do Phosh. |
| GPU (Adreno 506) | ❌ | freedreno provavelmente funcionaria com mais trabalho |
| LED de notificação frontal | ⚠️ | Enumerado como `/sys/class/leds/white:notification` via `gpio-leds` no MPP2 do pmi8950 (DTS + `CONFIG_LEDS_GPIO=y`). `echo 1 > brightness` retorna sucesso mas **validação visual pendente** — usuário ainda não viu o LED acender fisicamente. Pode precisar inverter polaridade (`GPIO_ACTIVE_LOW`) ou trocar pra `pmi8950_pwm`+`leds-pwm`. |

## Detalhes do que **não** funciona ainda

### USB DWC3 / CDC ACM — ✅ FUNCIONANDO

**Resolvido em 2026-05-19.**

Sintoma original: `dwc3: failed to initialize core` + `platform 7000000.usb:
deferred probe pending`.

Causa raíz descoberta via debug no initramfs (cat
`/sys/kernel/debug/devices_deferred`):

```
7000000.usb     platform: supplier 79000.phy not ready
```

O `dwc3` não estava falhando — estava **deferred** esperando o supplier
`79000.phy` (o USB 2.0 PHY do msm8953). O PHY nunca probava porque
`CONFIG_PHY_QCOM_QUSB2=m` (módulo no defconfig), e nosso initramfs minimal
não tem `modprobe`. Sem o driver carregado, o nó do DTS ficava órfão.

**Fix:** `CONFIG_PHY_QCOM_QUSB2=y` no `kernel/sanders.config.fragment`.

Resultado: após boot, `/sys/class/udc/7000000.usb` aparece, o initramfs
configura CDC ACM via configfs, e o host vê `/dev/ttyACM0`. Login Arch via
`picocom -b 115200 /dev/ttyACM0` (precisa apertar Enter algumas vezes pra
acordar o getty).

**Estabilidade:** resolvida em 2026-05-19 com três mudanças combinadas:

1. **Initramfs não spawna mais shell em `/dev/ttyGS0`** quando vai dar
   switch_root — o shell ficava órfão depois do `exec switch_root` e
   brigava com o `serial-getty@ttyGS0` do systemd pelo tty.
2. **`serial-getty@ttyGS0.service` habilitado no rootfs** com drop-in:
   - `--autologin root --keep-baud 115200,57600,38400,9600`
   - `TTYReset=no`, `TTYVHangup=no`, `TTYVTDisallocate=no` (CDC ACM não
     tem flow control de hardware; reset/hangup do getty deixava a
     sessão num estado ruim).
3. **Udev rule desligando USB autosuspend** (`power/control=on`) no
   gadget — sem isso o controller suspendia com a sessão ACM ativa.

Bônus: `setsid` adicionado aos symlinks busybox do initramfs (o init
chamava `/bin/setsid` mas não existia).

### Touchscreen Focaltech FT5436 — ✅ FUNCIONANDO

Diferente do irmão `potter` (Moto G5 Plus, Synaptics RMI4 @ 0x20), o
`sanders` (G5s Plus) usa **Focaltech FT5436 @ 0x38** com:
- Bus: `i2c@78b7000` (= `i2c_3` no mainline)
- IRQ: `&tlmm 65 IRQ_TYPE_EDGE_FALLING`
- Reset: GPIO 64 (fixado HIGH via `gpio-hog`)
- VDD: `pm8953_l10` @ 2.85V (`regulator-always-on`)
- VCC_I2C: `pm8953_l5` @ 1.8V
- I2C clock: 400kHz (sem DMA)

Driver: `edt-ft5x06` mainline (compatible `focaltech,ft5426`), **com patch
necessário** porque o FT5436 não expõe o registro `0xBB` que o
`edt_ft5x06_ts_identify()` lê — sem o patch o probe falha com `-110`
(ETIMEDOUT). Patch: `kernel/0001-edt-ft5x06-skip-identify-for-ft5436.patch`.

Após boot, aparece `/dev/input/event1` reportando `ABS_MT_POSITION_X/Y`
(multi-touch protocol B) + `BTN_TOUCH` + `SYN_REPORT`. Confirmado com
2659 eventos / 10s de toque (ABS=2099 KEY=24 SYN=536).

Descoberta-chave: o **endereço, chip e reguladores corretos foram
extraídos do DTB Android stock** (`SANDERS_RETAIL_7.1.1...` boot.img →
QCDT v3 LZ4-compressed → dtb individual → procurar `touch`). Sem isso,
seguíamos chutando "deve ser igual ao potter".

Para teclado virtual seria necessário stack gráfica (Weston/Phosh/Sxmo),
que é outro nível de trabalho — mas o touch em si está pronto.

### Wi-Fi WCN3680B (parcial)

O msm8953 usa o chip QCA WCN3680B (pronto) integrado, controlado pelo
driver mainline `wcn36xx` via `qcom-wcnss-pil` (Peripheral Image Loader).

**O que ja foi feito:**

1. DTS (`dts/msm8953-motorola-sanders.dts`): habilitado `&wcnss` com
   `vddpx-supply = &pm8953_l5` e `&wcnss_iris` com `compatible = "qcom,wcn3680"`
   + supplies (`vddxo` -> `pm8953_l7`, `vddrfa` -> `pm8953_l19`,
   `vddpa` -> `pm8953_l9`, `vdddig` -> `pm8953_l5`). Voltagens
   extraidas do DTB stock Android.
2. Kernel (`kernel/sanders.config.fragment`): toda a cascata builtin —
   `CFG80211`, `MAC80211`, `WCN36XX`, `QCOM_WCNSS_PIL`, `QCOM_WCNSS_CTRL`,
   `QCOM_SYSMON`, `QRTR`, `RPMSG_QCOM_*`, `RFKILL`. Cada um precisa ser
   `=y` por causa de `depends on FOO || FOO=n` em cascata.
3. Firmware:
   - Pronto: `wcnss.mdt` + `wcnss.b0X` (~4MB) extraidos do
     `NON-HLOS.bin` do flashfile stock 7.1.1. Vao em
     `/lib/firmware/wcnss.*` E **TAMBEM** no initramfs (em
     `/lib/firmware/`) porque o probe acontece antes do switch_root.
   - NV cal: `WCNSS_qcom_wlan_nv.bin` baixado do repo LineageOS
     `alissonlauffer/proprietary_vendor_motorola_sanders` (branch `ten`,
     md5 `7784365e9db784737ed3eb613d0c705e`). **Nao** vem no flashfile
     do device e o `/persist/wifi` esta vazio (Android nunca foi inicializado
     no device modificado). O `WCNSS_cfg.dat` em `/vendor/firmware/wlan/prima/`
     do device tem outro formato (header `01 0c 03 0b` vs `ff ff ff ff be ba fe ca`
     do wlan_nv real).

**O que funciona:**
- Pronto firmware carrega no boot: `WCN v2.0 RadioPhy vIris_TSMC_4.0 with 48MHz XO`
- `wcn36xx: firmware API 1.5.1.2, 41 stations, 2 bssids`
- `wlan0` (mac `02:00:7f:53:68:74`) + `phy0` criados
- Scan funcional, vendo redes 2.4G e 5G (~-22dBm pra AP proximo)
- Auth + associate completam contra AP WPA2

**O que NAO funciona ainda:**
- **4-way handshake WPA2 falha**: ap envia EAPOL frames mas chip nao
  consegue configurar BSS/STA. dmesg mostra:
  ```
  wcn36xx: WARNING hal config bss response failure: 5
  wcn36xx: ERROR hal_config_bss response failed err=-5
  wcn36xx: WARNING hal config sta response failure: 5
  wcn36xx: ERROR hal_config_sta response failed err=-5
  wlan0: associated
  wlan0: deauthenticated (Reason: 15=4WAY_HANDSHAKE_TIMEOUT)
  ```
  `err=5` = `WCN36XX_FW_MSG_RESULT_MEM_FAIL` (chip diz que falta memoria
  para o BSS/STA context). Reproduz com WPA, WPA2, 2.4G e 5G, com
  diferentes ciphers (CCMP/TKIP). NAO se resolve com mudancas de config.
- Suspeitos:
  - Pronto firmware do stock 7.1.1 + NV cal do Lineage 10 podem ter
    incompatibilidade de versao.
  - Pode haver bug no driver wcn36xx que precisa patch (postmarketOS
    community tem patches nao-mainline para msm8953/wcn3680b).
  - DT esta com warning `supply vddmx not found, using dummy regulator` —
    pode estar relacionado.
- Proximos passos: tentar firmware do LineageOS 18.1+/pmOS, ou patches do
  pmOS-linux-msm8953 (`linux-postmarketos-qcom-msm8953`).

**Investigacao 2026-05-20 (sessao de tentativa de fix — sem sucesso):**

Tres hipoteses validadas e descartadas:

1. **wlan_nv.bin alternativo**: comparado md5 dos 4 forks LineageOS/dotOS
   do sanders (alissonlauffer/ten, dotOS-Devices/dot11, Motorola-Common,
   effeffe/halium-10). **Todos identicos** (md5 7784365e9db784737ed3eb...).
   Variantes regionais (`*_Brazil.bin`, `*_India.bin`) sao placebos —
   mesmo blob. Nao e cal corrupto.

2. **Supply `vddmx`**: warning `supply vddmx not found, using dummy
   regulator` no boot e **cosmetico**. O driver `qcom_wcnss.c` usa
   `pronto_v3_data` (compatible `qcom,pronto-v3-pil`), que tira `mx`/`cx`
   de `power-domains = <&rpmpd MSM8953_VDDCX/VDDMX>` (ja presente em
   `msm8953.dtsi`). O warning vem do regulator-core olhando pelo nome,
   nao impede o funcionamento.

3. **Patches pmOS no wcn36xx**: comparado byte-a-byte
   `drivers/net/wireless/ath/wcn36xx/{main,smd,hal,dxe,wcn36xx,smd}.c/.h`
   entre `github.com/msm8953-mainline/linux@7.0.2/main` e nosso build
   mainline (master perto de 7.1.0-rc4). **Um unico diff** em `smd.c`
   (`len < sizeof(*rsp)` vs `!=`) que e cosmetico no warning "Bad TX
   complete indication". **Nenhum patch resolve o MEM_FAIL=5.**

**Conclusao**: `MEM_FAIL=5` em `hal_config_bss` e firmware-side. O Pronto
FW do stock Motorola 7.1.1 nao aloca contexto BSS quando o mainline
wcn36xx pede certas combinacoes de capabilities. PostmarketOS para
msm8953 sofre o mesmo problema — eles rodam o mesmo codigo mainline e
documentam isso em issues.

**MAC `02:xx:xx:xx:xx:xx`** gerado no boot e mainline `wcn36xx` nao
saber parsear o formato do `WCNSS_qcom_wlan_nv.bin` que temos (formato
da Pronto FW stock). Cosmetico — nao e cal corrupto, e driver sem codigo
pra ler.

**Saidas reais (todas custosas, fora do escopo)**:
- Engenharia reversa do Pronto FW pra inferir o que `hal_config_bss`
  precisa que mandamos errado.
- Portar driver vendor `qcacld` (proprietary, downstream, **nao
  funciona em mainline**).
- Esperar fix upstream — tem patches em revisao na lista wcn36xx
  (devs do msm8916), nao mergeados.

**Workaround atual**: cabo USB CDC ECM da acesso a internet, suficiente
pro proposito atual do dispositivo.

**Continuacao 2026-05-20 (RE diferencial mainline vs prima/qcacld)**:

Clonado kernel downstream `Sanders-Revived/kernel_motorola_msm8953@4.9.337`
que tem `drivers/staging/prima/` (qcacld) — o vendor driver sabidamente
funcional com esta Pronto FW. Comparados field-by-field:

| Struct/area | Mainline | Prima | Match? |
|---|---|---|---|
| `tConfigBssParams` (V0) ordem dos campos | sta no meio (TODO comment) | staContext no fim | **NAO** |
| `tConfigBssParams_V1` (V1) ordem | sta antes de vht_*, ok | staContext antes de vht_* | sim |
| `tConfigStaParams_V1` ordem completa | matched 30+ campos | matched | sim |
| `tSirSupportedRates_V1` layout | 66 bytes | 66 bytes | sim |
| Enums (`bss_type`, `nw_type`) tamanho | 4 bytes (MAX_ENUM_SIZE = 0x7FFFFFFF) | 4 bytes (idem) | sim |
| Header `wcn36xx_hal_msg_header` | 8 bytes | 8 bytes | sim |
| `WLAN_FEATURE_VOWIFI_11R` | ifdef sempre incluido | sanders_defconfig=y → incluido | sim |
| `tTxComplIndMsg` payload | 4 bytes (apenas status) | **8 bytes** (status + dialogToken) | **NAO** |

**Dois fixes reais aplicados (commit subsequente):**

1. `wcn36xx_hal_config_bss_params` (V0): mover `sta` para o fim. Faz a
   sugestao do `TODO move sta to the end for 3680` que estava no proprio
   mainline. **Nao destrava nosso sanders** porque FW 1.5.1.2 > 1.2.2.24
   faz o driver usar V1 (nao V0). Mas e correto upstream.

2. `wcn36xx_hal_tx_compl_ind_msg`: adicionar `u32 dialog_token` ao final.
   Faz `len != sizeof(*rsp)` parar de tripar. **Elimina o warning
   "Bad TX complete indication"** que aparecia em todo TX. **Nao
   destrava o MEM_FAIL** — esse warning era sintoma colateral, nao
   causa.

**Estado pos-fixes (2026-05-20)**: `Bad TX complete indication` sumiu.
`MEM_FAIL=5` em `hal_config_bss/hal_config_sta` continua identico.

**Limite alcancado**: sem disassembly da Pronto FW (Hexagon DSP,
Qualcomm-specific, semanas de trabalho), nao da pra inferir o que
`hal_config_bss` quer diferente. Todas as structs visiveis batem. Os
fixes acima sao upstream-quality, mas o problema raiz e firmware-side.

**Estado atual:** hipótese do fallback V0 preparada, mas ainda não validada no aparelho.
Retomar após o teste WPA2; se falhar, considerar o bloqueio firmware-side e preservar o
workaround USB CDC ECM.

### Bluetooth WCN3680B — ✅ FUNCIONANDO

**Resolvido em 2026-05-21.**

Mesmo SoC `pronto` do Wi-Fi, mas o caminho BT é totalmente independente
no kernel: `btqcomsmd` consome o canal SMD/RPMSG que o `wcnss_ctrl` já
expõe e fala HCI puro com o firmware. Nada que dependa da pilha
`hal_config_*` quebrada do Wi-Fi.

**O que foi preciso:**

1. Verificar que `msm8953.dtsi` já tinha o subnode:
   ```
   smd-edge {
       wcnss {
           wcnss_bt: bluetooth { compatible = "qcom,wcnss-bt"; };
           ...
       };
   };
   ```
   Sem alterações no DT.

2. Habilitar no `kernel/sanders.config.fragment`:
   ```
   CONFIG_BT=y
   CONFIG_BT_BREDR=y
   CONFIG_BT_LE=y
   CONFIG_BT_LE_L2CAP_ECRED=y
   CONFIG_BT_QCOMSMD=y
   CONFIG_BT_QCA=y
   CONFIG_BT_HIDP=y
   CONFIG_BT_RFCOMM=y
   CONFIG_BT_BNEP=y
   ```
   Tudo builtin (`=y`) porque não há modprobe no initramfs.

3. `pacman -S bluez bluez-utils` no rootfs e `systemctl enable bluetooth`.

**Validação:**
- `dmesg`: `Bluetooth: Core ver 2.22` + `RFCOMM/BNEP/HIDP` registrados.
- `/sys/class/bluetooth/hci0` presente.
- `bluetoothctl scan on` descobre devices vizinhos.
- `discoverable on` + `pairable on` → pareado com outro phone Android OK.

**MAC de fábrica restaurado (2026-05-21):**

O driver `btqcomsmd` não consulta a NV/persist do device, então sem
intervenção o controller sobe com MAC locally-administered aleatório
(`02:xx:..`) a cada boot — pareamento nenhum persiste.

Solução: o stock guarda o MAC em `/persist/bluetooth/.bt_nv.bin` no
formato NV Qualcomm — 9 bytes, header `01 01 06` (type=1, item=1,
len=6) + 6 bytes do MAC em **little-endian** (byte-reversed).

Ex.: arquivo contém `01 01 06 7c 4f df 14 77 d0` → MAC real é
`D0:77:14:DF:4F:7C` (OUI Motorola).

`/usr/local/bin/sanders-bt-mac.sh` (no overlay common) faz:
1. Monta `/dev/disk/by-partlabel/persist` read-only em `/run/...`.
2. Lê os 9 bytes, valida o header, extrai os 6 invertidos.
3. `btmgmt --index 0 power off && public-addr $MAC && power on`.
4. Desmonta `/persist` no exit (`trap`).

`sanders-bt-mac.service` (oneshot) roda com:
- `Before=bluetooth.service`
- `WantedBy=bluetooth.service`
- `ConditionPathExists=/dev/disk/by-partlabel/persist`

Validado em ambos os flavors (overlay `common`). Pareamento com
outro phone Android agora sobrevive reboot.

### Sensor LTR559 — ⚠️ PARCIAL

**Em 2026-05-21.**

LiteON LTR559 (proximidade + ALS) está no i2c_7 (BLSP2 QUP3, `7af7000`)
addr 0x23 com IRQ no GPIO 86, vdd no `pm8953_l10` (2.85V, mesmo do
FT5436) e vddio no `pm8953_l6` (1.8V always-on). DT criado em `&i2c_7`
no sanders.dts, driver mainline `ltr501` (cobre 501/559/301) habilitado
via `CONFIG_LTR501=y` + `CONFIG_IIO=y` (builtin).

**O que funciona:**
- Probe limpo, sem erros no dmesg.
- `/sys/bus/i2c/devices/1-0023` presente.
- `/sys/bus/iio/devices/iio:device0/name` = `ltr559`.
- `in_proximity_raw` lê valores reais (700–960), reage a obstrução do
  emissor (variação observada cobrindo o sensor com dedo).

**O que falta:**

1. **ALS travada em 0** — `in_intensity_both_raw`, `in_intensity_ir_raw`,
   `in_illuminance_input` retornam zero permanentemente. Driver
   inicializa `ALS_CONTR` com `als_mode_active=BIT(0)` mas a flag
   `STATUS_ALS_RDY` no registrador `LTR501_ALS_PS_STATUS` (0x8c) nunca
   seta — chip não está produzindo medidas. Precisa investigar:
   - Confirmar via i2cget (driver claim hoje impede, precisa unbind).
   - Verificar se há atraso de power-up entre vdd/vddio que o ltr501
     mainline não respeita (stock tinha `pinctrl-1 = <&ltr559_sleep>` +
     toggle no probe, mainline não toca pinctrl em runtime).
   - Comparar com qualquer outro device mainline usando ltr559.

2. **Calibração de proximidade** — valor "longe" reportado é ~800
   (saturado pela reflectância do vidro do display). Stock usava
   `ps-threshold = 800`, `ps-nearoffset = 20`, `ps-faroffset = 15` em
   propriedades vendor-specific que o mainline não consome. O binding
   só expõe `proximity-near-level` (semântico pra userspace), e o
   driver não programa o registrador `PS_OFFSET`. Calibração precisa
   ser feita em userspace (subtrair baseline) ou patchar driver pra
   ler offset do DT.

**Acel/giro/magnetômetro:** nada disso está em I2C no sanders. O stock
DTS confirma: só LTR559 e FT5436 no i2c_7/i2c_3. Sensores de movimento
passam pela SSC do msm8953 (LPASS/SLPI), via `sns_dsps` (firmware) e
IPC QMI — sem driver mainline prático. Bloqueia autorotate.

### Painel de display

Nenhum driver mainline para Tianma NT35596 ou DJN ILI7807D específico
do sanders. Opções:

- Verificar drivers similares (`drivers/gpu/drm/panel/`) — pode haver
  algo compatível.
- Escrever um driver (sub-projeto significativo).
- Continuar usando simple-framebuffer (o que temos hoje).

## Flavors de rootfs

O `05-build-rootfs.sh` aceita `FLAVOR={headless,desktop}` (default `headless`).
Kernel, DTS, initramfs e lk2nd são idênticos — só muda o userspace.

| | Headless | Desktop |
|---|---|---|
| Tamanho da imagem | 3 GiB | 5 GiB |
| Pacotes extras | `samba` (não habilitado) | `samba` + `phoc`, `phosh`, `squeekboard`, `weston`, `xorg-xwayland`, `mesa`, `mesa-utils`, `mesa-demos`, `seatd`, `libdisplay-info`, `ttf-dejavu`, `noto-fonts` |
| Compositor no boot | nenhum (getty@tty1 ativo) | `phosh.service` substitui `getty@tty1` (default). Weston disponível mas dormindo — alternar com `systemctl disable phosh && systemctl enable weston`. |
| Caso de uso | SSH, SAMBA, server-style headless | Desktop interativo + apps gráficos |

Arquivos versionados específicos do desktop ficam em
`rootfs-overlay/desktop/` e são copiados pelo `cp -a` no fim do
`05-build-rootfs.sh`.

## Métricas atuais

- Tamanho do `boot-sanders.img`: ~16 MiB (cabe na partição `boot` de
  15.5 MiB? **Não** — por isso usamos `fastboot boot` em vez de
  `fastboot flash boot`, que é bloqueado pelo bootloader Motorola
  mesmo desbloqueado).
- Tempo de cold boot (lk2nd → login prompt): ~30s.
- RAM usada por systemd em idle: não medido (sem console).
- Temperatura do device: aquece levemente (CPU sem governor adequado,
  provavelmente).
