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

## Solução (Fallback V0)
A estratégia definitiva adotada foi **forçar o uso da API V0** (legada) para os comandos de configuração de BSS e Estação. 

### Consequências:
- **Prós:** Estabilidade garantida, fim do erro `MEM_FAIL=5`, conexão WPA2 funcional.
- **Contras:** Desativação do suporte a 802.11ac (5GHz VHT). O dispositivo operará apenas em modo 802.11n (HT), o que é suficiente para a maioria dos usos.

### Alterações Aplicadas:
- Reversão das mudanças experimentais em `hal.h`.
- Modificação no `smd.c` para ignorar o check de `RF_IRIS_WCN3680` nas funções `wcn36xx_smd_config_bss` e `wcn36xx_smd_config_sta`, forçando o caminho de código que utiliza mensagens sem campos VHT.

---
*Documentação gerada automaticamente pela Gemini CLI em 2026-05-21.*
