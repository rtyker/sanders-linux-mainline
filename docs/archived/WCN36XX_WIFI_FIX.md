# Investigação e Fix do Wi-Fi WCN36xx (WPA2 MEM_FAIL=5)

> ✅ **RESOLVIDO (2026-09-03).** WPA2 confirmado funcionando de ponta a ponta ao vivo
> no hardware real: handshake completo, IP via DHCP, ping com internet real pela
> interface `wlan0`. Ver seção "Validação final" abaixo. O fix é o patch
> `kernel/0002-wcn36xx-wcn3680-novht-fallback.patch`.

Este documento detalha o processo de depuração e a solução aplicada para o problema de autenticação WPA2 no Moto G5s Plus (sanders).

## Sintoma
O hardware Wi-Fi é detectado, realiza scans com sucesso, mas falha durante o 4-way handshake do WPA2. O `dmesg` reporta:
```
wcn36xx: WARNING hal config bss response failure: 5
wcn36xx: ERROR hal_config_bss response failed err=-5
wcn36xx: WARNING hal config sta response failure: 5
wcn36xx: ERROR hal_config_sta response failed err=-5
wlan0: deauthenticated from ... (Reason: 15=4WAY_HANDSHAKE_TIMEOUT)
```
O erro `5` no firmware Pronto (`WCN36XX_FW_MSG_RESULT_MEM_FAIL`) indica que a firmware rejeitou o layout ou o tamanho da mensagem HAL.

## Descobertas
1.  **Divergência de Estrutura:** O driver mainline utiliza estruturas "V1" (com suporte a VHT/802.11ac) para o chip WCN3680.
2.  **Sensibilidade a Alinhamento:** Através de dumps hexadecimais (`HAL >>>`), observamos que a firmware espera campos alinhados em 4 bytes. Tentativas de reorganizar o layout V1 resultaram em travamentos (Kernel Panic) ou persistência do erro `MEM_FAIL=5`.
3.  **Limitação da Firmware Stock:** A firmware do sanders (API 1.5.1.2) parece ter sido portada da arquitetura "Prima" (Nexus 5 era), onde o suporte a VHT é instável ou utiliza um layout de memória proprietário incompatível com o padrão mainline.

## Solução tentada #1 — Fallback V0 puro (FALSIFICADA, ver validação abaixo)
A primeira estratégia testada foi **forçar o uso da API V0** (legada) para os comandos
de configuração de BSS e Estação no WCN3680, via patch (versão antiga, já substituída)
que roteava incondicionalmente `wcn36xx_smd_config_bss()`/`config_sta()` para
`_v0()` quando `rf_id == RF_IRIS_WCN3680`. **Testado ao vivo em 2026-09-03: não
funcionou** — `hal_config_bss` continuou retornando `MEM_FAIL=5` mesmo com o struct
V0 puro (ver seção "Validação realizada no aparelho" abaixo). Hipótese descartada.

## Solução tentada #2 — Fallback "V1 NOVHT" via capacidade real do firmware (2026-09-03)

### A pista que faltava
O próprio driver mainline (`drivers/net/wireless/ath/wcn36xx/hal.h`) já define
`WCN36XX_DIFF_BSS_PARAMS_V1_NOVHT`/`WCN36XX_DIFF_STA_PARAMS_V1_NOVHT` — um **terceiro
formato**, distinto tanto do V0 puro quanto do V1 completo com VHT: é o struct V1
(`wcn36xx_hal_config_bss_params_v1`/`_sta_params_v1`), só que com o `header.len`
**truncado** para cortar os campos VHT do final da mensagem antes de enviar. Esse é
exatamente o "formato híbrido V1/V0" que o roteiro original deste documento (seção
"Próxima Fase", item 1) sugeria investigar.

Em `wcn36xx_smd_config_sta_v1()`/`wcn36xx_smd_config_bss_v1()`, o driver mainline já
decide entre os dois formatos — mas com um critério ingênuo:
```c
if (wcn->rf_id == RF_IRIS_WCN3680) {
    INIT_HAL_MSG_V1(...);              /* V1 completo, com VHT */
} else {
    INIT_HAL_MSG(...);
    msg_body.header.len -= WCN36XX_DIFF_...NOVHT;   /* V1 truncado, sem VHT */
}
```
Ou seja: **qualquer** chip identificado como `RF_IRIS_WCN3680` recebe o layout VHT
completo, na suposição de que todo WCN3680 suporta 802.11ac. Só que o driver
**também** já consulta a capacidade real que a firmware concedeu, via
`wcn36xx_smd_feature_caps_exchange()` (troca de bitmap de capacidades logo no boot do
subsistema Wi-Fi) — `wcn->fw_feat_caps` guarda o que a firmware *de fato* respondeu,
e existe o helper `wcn36xx_firmware_get_feat_caps(wcn->fw_feat_caps, DOT11AC)` pra
checar isso. **Esse helper nunca era consultado** nesses dois pontos de decisão —
o driver simplesmente confiava no `rf_id`, não na resposta real da firmware.

### Hipótese
A firmware Pronto stock do sanders/potter (API `1.5.1.2`, derivada da linha "Prima" —
ver seção "Descobertas" acima) roda num `RF_IRIS_WCN3680` real, mas **não concede**
`DOT11AC` na troca de capacidades (VHT instável/não suportado nessa firmware
específica, apesar do chip suportar em outras). O driver, achando (pelo `rf_id`) que
podia usar VHT, mandava a struct grande — a firmware rejeitava com `MEM_FAIL=5`. O V0
puro (estrutura antiga completamente diferente, não é só "V1 sem VHT") também não bate
com o que essa firmware espera — daí a solução tentada #1 também falhar.

### Correção aplicada (2026-09-03)
`kernel/0002-wcn36xx-wcn3680-novht-fallback.patch` (renomeado do antigo
`0002-wcn36xx-force-v0-for-wcn3680.patch`, que forçava V0):
- Reverte os wrappers `wcn36xx_smd_config_bss()`/`config_sta()` para o comportamento
  **original** do mainline (decidem V1-vs-V0 só pela versão de firmware antiga
  `1.2.2.24`, sem tocar em `rf_id`).
- Modifica **só** as duas funções internas `wcn36xx_smd_config_sta_v1()` e
  `wcn36xx_smd_config_bss_v1()`: a condição `rf_id == RF_IRIS_WCN3680` passa a exigir
  também `wcn36xx_firmware_get_feat_caps(wcn->fw_feat_caps, DOT11AC)` — só usa o V1
  completo com VHT se a firmware realmente concedeu essa capacidade. Caso contrário
  (nosso caso, se a hipótese estiver certa), cai no mesmo caminho NOVHT que os chips
  não-3680 já usam.

### Consequências esperadas (se a hipótese se confirmar):
- **Prós:** Evita tanto o layout V1-com-VHT (que já sabemos que falha) quanto o V0
  puro (que também já sabemos que falha) — usa o terceiro formato ainda não testado.
- **Contras:** Mesmo trade-off da tentativa #1 — sem VHT/802.11ac, só HT/802.11n.
  Aceitável pro caso de uso (servidor headless).

### ✅ Validação final ao vivo (2026-09-03) — SUCESSO
Testado ao vivo no hardware real com o kernel 7.1.0-rc4 (`#33`), rede real
`prfelicidade` (2.4 GHz, mesmo AP `6a:fa:c4:ea:34:9c` da tentativa anterior), via
`sanders-network-setup.sh wifi prfelicidade -` (prompt seguro de senha):

```
$ sanders-network-setup.sh status
--- Wi-Fi (wlan0) ---
  Estado:    up
  IP:        192.168.6.172/24
  Conectado: 6a:fa:c4:ea:34:9c
  wpa_supplicant: ativo

$ journalctl -u wpa_supplicant@wlan0
wlan0: WPA: Key negotiation completed with 6a:fa:c4:ea:34:9c [PTK=CCMP GTK=TKIP]
wlan0: CTRL-EVENT-CONNECTED - Connection to 6a:fa:c4:ea:34:9c completed [id=0 id_str=]

$ ping -I wlan0 -c3 -W3 8.8.8.8
PING 8.8.8.8 (8.8.8.8) from 192.168.6.172 wlan0: 56(84) bytes of data.
64 bytes from 8.8.8.8: icmp_seq=1 ttl=115 time=28.9 ms
64 bytes from 8.8.8.8: icmp_seq=2 ttl=115 time=27.8 ms
64 bytes from 8.8.8.8: icmp_seq=3 ttl=115 time=24.8 ms
3 packets transmitted, 3 received, 0% packet loss
```

`dmesg` confirmado **sem nenhuma ocorrência** de `MEM_FAIL`, `hal config bss response
failure` ou `hal config sta response failure` neste boot — os erros que apareciam nas
duas tentativas anteriores sumiram completamente. Handshake WPA2 completo, IP via
DHCP real, internet funcional pela Wi-Fi nativa. **Hipótese #2 confirmada: a firmware
Pronto 1.5.1.2 realmente não concede `DOT11AC`, e o formato correto era o V1
truncado (NOVHT), não V0 puro nem V1 completo.**

**Nota metodológica:** este teste foi feito com o `Image.gz` do fix já com o boot
autônomo (DTB "seguro", sem as mudanças experimentais de painel MIPI-DSI que outra
sessão estava testando em paralelo no mesmo hardware) — isolando a variável do Wi-Fi
de qualquer risco de boot hang relacionado a display. Ver
`../../../docs/archived/DISPLAY_PANEL_PLAN.md` (no repo pai) pro trabalho de painel, que segue em paralelo,
independente deste fix.

### Tentativa anterior (V0 puro, para referência histórica)
Testado ao vivo no hardware real com o kernel 7.1.0-rc4 (`#29`) e rede real `prfelicidade` (2.4 GHz, canal 7, -27 dBm):

```text
# 1. Varredura e Associação:
wlan0: SME: Trying to authenticate with 6a:fa:c4:ea:34:9c (SSID='prfelicidade' freq=2442 MHz)
wlan0: Trying to associate with 6a:fa:c4:ea:34:9c (SSID='prfelicidade' freq=2442 MHz)
wlan0: Associated with 6a:fa:c4:ea:34:9c

# 2. Recepção Over-the-Air:
wlan0: Event EAPOL_RX (23) received
wlan0: RX EAPOL from 6a:fa:c4:ea:34:9c (encrypted=1)
RX EAPOL - hexdump(len=99): 02 03 00 5f 02 00 8a 00 10 ...

# 3. Bloqueio no driver:
wcn36xx: WARNING hal config bss response failure: 5
wcn36xx: ERROR hal_config_bss response failed err=-5
wlan0: Not associated - Delay processing of received EAPOL frame (state=ASSOCIATING)
wlan0: CTRL-EVENT-DISCONNECTED bssid=6a:fa:c4:ea:34:9c reason=15
```

### 🔬 Conclusão Técnica da Validação
1. **Hardware RF / Demodulador / RX de Pacotes:** Estão **100% funcionais**. O chip sintoniza o canal correto e recebe os pacotes de handshake EAPOL enviados pelo roteador pelo ar.
2. **Falsificação da Hipótese de Fallback V0 Puro:**
   * A tentativa de simplesmente forçar `wcn36xx_smd_config_bss_v0()` **não solucionou** o `MEM_FAIL=5`. A firmware Pronto 1.5.1.2 rejeitou a mensagem V0 do BSS exatamente com o mesmo código de erro 5.
   * Por causa dessa rejeição, o driver não notifica o subsistema `cfg80211` de que o link BSS está estabelecido. Consequentemente, o `wpa_supplicant` mantém a interface em estado `ASSOCIATING`, atrasa o processamento do pacote EAPOL recebido e sofre timeout (`reason=15=4WAY_HANDSHAKE_TIMEOUT`).

---

## 🎯 Roteiro (histórico — item 1 RESOLVIDO em 2026-09-03)

1. ✅ **Investigação do Formato Híbrido V1 / V0 — RESOLVIDO.** Era exatamente isso:
   `WCN36XX_DIFF_BSS_PARAMS_V1_NOVHT`/`_STA_PARAMS_V1_NOVHT` já existiam no driver
   mainline, só precisavam ser selecionados com base na capacidade `DOT11AC`
   realmente concedida pela firmware (`wcn36xx_firmware_get_feat_caps`), não pelo
   `rf_id` isolado. Ver seção "Solução tentada #2" acima.
2. **(Não necessário agora, mas registrado para referência)** Comparação
   byte-a-byte com o driver CAF stock (Android 3.18/Prima) — ficaria útil só se o
   fix atual regredir em algum firmware/board diferente no futuro.
3. **(Não necessário agora)** Firmware alternativa de outro device MSM8953.
4. **Modo Operacional Atual do Servidor:** Wi-Fi nativo agora é uma opção viável de
   rede standalone (WPA2, HT/802.11n, sem VHT/5GHz-ac). USB OTG Ethernet e USB ECM
   (`10.42.0.2`) continuam disponíveis como alternativas.

## 🎯 Trabalho futuro (opcional, não bloqueia nada)

- Confirmar se o VHT/802.11ac (5GHz) funcionaria em firmwares Pronto mais recentes
  que realmente concedam `DOT11AC` — não é o caso da firmware stock atual (1.5.1.2),
  então isso é só curiosidade, não uma pendência prática pro servidor headless.
- Testar estabilidade de longa duração (reconexão após roaming, suspensão de rede,
  etc.) — fora do escopo desta sessão, que validou conectividade básica.

