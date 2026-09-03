# Investigação e Fix do Wi-Fi WCN36xx (WPA2 MEM_FAIL=5)

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

## Solução em teste (Fallback V0)
A estratégia preparada para teste é **forçar o uso da API V0** (legada) para os comandos de configuração de BSS e Estação no WCN3680.

### Consequências esperadas:
- **Prós:** Evita o layout V1 que retorna `MEM_FAIL=5` e pode permitir a conexão WPA2.
- **Contras:** Desativação do suporte a 802.11ac (5GHz VHT). O dispositivo operará apenas em modo 802.11n (HT), o que é suficiente para a maioria dos usos.

### Alterações Preparadas:
- O patch `kernel/0002-wcn36xx-force-v0-for-wcn3680.patch` altera somente os wrappers `wcn36xx_smd_config_bss()` e `wcn36xx_smd_config_sta()`.
- Para `RF_IRIS_WCN3680`, os wrappers selecionam V0 e continuam pelo caminho comum de resposta e `mutex_unlock`.
- Não há alteração de estrutura em `hal.h`, MAC fixo ou edição persistente da árvore efêmera do kernel.

### Validação realizada no aparelho (2026-09-03)
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

## 🎯 Roteiro para a Próxima Fase do Wi-Fi

Para futuras sessões focadas na resolução definitiva do Wi-Fi, os caminhos técnicos mapeados são:

1. **Investigação do Formato Híbrido V1 / V0:**
   * O Pronto v3 (`qcom,pronto-v3-pil`) possui definições com `#define WCN36XX_DIFF_BSS_PARAMS_V1_NOVHT`.
   * Testar se a firmware 1.5.1.2 espera a mensagem BSS no formato `V1` com tamanho reduzido (`NOVHT`) em vez de `V0` puro.
2. **Comparação Byte-a-Byte com o Driver CAF Stock (Android 3.18 / Prima):**
   * Extrair a `struct hal_config_bss_req_msg` do kernel CAF da Motorola (`drivers/net/wireless/wcnss/wcnss_wlan.c` e `wcnss_v1.c` da árvore stock do sanders/potter).
   * Comparar o alinhamento de memória (`sizeof`), campos adicionados/removidos e a ordem das structs de BSS e STA.
3. **Alternativa via Firmware alternativa de outro device MSM8953:**
   * Testar arquivos `wcnss.mdt` / `wcnss.b*` extraídos de outros aparelhos Snapdragon 625 com mainline maduro (ex: Xiaomi Redmi 4X / `santoni` ou Xiaomi Mi A1 / `tissot`), que utilizam firmwares Pronto com suporte V1/VHT estável.
4. **Modo Operacional Atual do Servidor:**
   * Até que o handshake WPA2 seja sanado, a rede standalone recomendada para o servidor é via adaptador USB Ethernet (OTG) ou USB ECM (`10.42.0.2`).

