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

### Validação necessária no aparelho
Depois de gerar e inicializar a nova imagem, coletar:

```sh
dmesg -w
iw dev wlan0 scan
wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf
iw dev wlan0 link
dmesg | grep -E 'wcn36xx|MEM_FAIL|4WAY|deauth'
```

Critério de sucesso: associação WPA2 concluída, endereço IP obtido e ausência de `hal_config_bss/sta ... failure: 5`. Se o `MEM_FAIL=5` permanecer, o patch deve ser revertido como hipótese falsificada e a investigação deve retornar ao protocolo HAL/firmware.

---
*Documentação gerada automaticamente pela Gemini CLI em 2026-05-21.*
