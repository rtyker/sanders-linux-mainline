# lk2nd no sanders — bootloader 2º estágio

Procedimento para subir o **lk2nd** (Little Kernel 2nd stage) no Moto
G5s Plus, pré-requisito para tudo neste repo.

> ⚠️ **NÃO use binários pré-compilados de lk2nd.** Builds prontos
> publicados (releases do GitHub, etc.) **não funcionam** neste device.
> Sempre compilar localmente do fork correto. O script
> `scripts/01-build-lk2nd.sh` faz isso.

## Pré-requisitos

- Bootloader Motorola **desbloqueado** (`fastboot oem unlock` ou
  similar — processo destrutivo: apaga dados).
  - Verifique: `sudo fastboot getvar unlocked` → `yes`
- Toolchain `arm-none-eabi-gcc` + binutils + newlib.
- `android-tools` para `fastboot`.

## Build (automatizado)

```bash
./scripts/01-build-lk2nd.sh
```

Clona `https://github.com/playday3008/lk2nd.git` (commit `c8b47cd`),
compila `lk2nd-msm8953`, copia para `build/out/lk2nd.img`.

## Por que esse fork específico

Upstream `msm8916-mainline/lk2nd` **não** tem o
`msm8953-motorola-sanders.dtsi`. Sem ele, o lk2nd não reconhece o
device, falha em fazer match no QCDT do Android stock e não consegue
fazer chain-load.

O fork `playday3008/lk2nd` no commit `c8b47cd` ("fix: typo in
msm8953-motorola-sanders.dtsi") tem o DTSI completo com as 6 variantes
de board (p1, p2, p3-1, p3-2, p4-1, p4-2).

## Carregando o lk2nd no aparelho

```bash
# Aparelho em fastboot mode (power-off, depois power + vol↓):
sudo fastboot boot build/out/lk2nd.img
```

Após uns 2-3s, a tela mostra a interface do lk2nd. Confirme que está
rodando:

```bash
sudo fastboot getvar lk2nd:device       # deve retornar: sanders
sudo fastboot getvar lk2nd:version
sudo fastboot getvar lk2nd:model        # deve retornar: Motorola Moto G5s Plus (sanders)
```

## `fastboot boot` vs `fastboot flash`

**`fastboot boot lk2nd.img`** → carrega o lk2nd em RAM, transitório,
some no próximo reboot. **FUNCIONA.**

**`fastboot flash boot lk2nd.img`** → tenta gravar na partição `boot`.
**NÃO FUNCIONA no sanders** — bloqueado por AVB/Motorola signing mesmo
com bootloader unlocked.

**❌ NÃO existe partição `lk2nd` dedicada — claim refutado em 2026-09-02.**

A linha abaixo, presente numa versão anterior deste doc, **nunca saiu do
device real**:

```
(bootloader) partition-size:lk2nd: 0x80000    # ← inventado, não observado
```

Testado ao vivo no `potter` físico (`fastboot getvar all` completo salvo,
63 linhas, product/board/hwrev/etc. — **nenhuma linha `partition-size:*`
para nenhuma partição**, esse bootloader Motorola simplesmente não expõe
isso em `getvar all`). Consulta direta confirma que a partição não existe:

```bash
$ sudo fastboot getvar partition-size:lk2nd
partition-size:lk2nd:              # vazio = não existe
$ sudo fastboot flash lk2nd build/out/lk2nd.img
Invalid partition name lk2nd       # confirmado, comando falha
```

Para comparação, partições que **existem de fato** neste device (consulta
direta, não destrutiva):
```
partition-size:aboot: 0x0000000000180000   # 1.5 MiB — o LK/aboot Motorola-signed, ATIVO
partition-size:boot:  0x0000000001000000   # 16 MiB — bloqueado por AVB (ver seção acima)
partition-size:abl:                        # não existe (nome usado em outros devices Qualcomm)
```

**Conclusão:** não há, até agora, nenhum alvo de flash conhecido e seguro
para persistir o lk2nd através de um cold boot. O único candidato real
(`aboot`) é o bootloader primário assinado pela Motorola — sobrescrevê-lo
com uma imagem lk2nd não assinada tem alto risco de **hard brick**
(provavelmente exigiria recuperação via EDL/Blank Flash, se é que é
possível neste SoC). Isso é qualitativamente diferente de tudo testado
neste projeto até agora (que sempre foi `fastboot boot` em RAM, reversível
por definição, ou tentativas de flash que falharam de forma limpa sem
tocar o device). **Não tentar sem autorização explícita e um plano de
recuperação testado.** Veja `docs/AUTONOMOUS_DIRECT_BOOT_PLAN.md` na raiz
do projeto para o estado completo dessa investigação.

## Histórico (pra contexto)

O sanders só tem lk2nd funcional graças ao fork do playday3008. Em
2026-05-19, durante o desenvolvimento deste port, foi descoberto:

1. Builds prontos de lk2nd **não funcionam** (boot falha silenciosamente).
2. `fastboot flash boot` **é rejeitado** no sanders.
3. O lk2nd faz match estrito de `qcom,board-id` no DTB do kernel — DTBs
   com IDs errados são rejeitados silenciosamente, dando "logo Motorola
   congelado". Resolvido listando 6 variantes no DTS sanders.
4. Existe partição `lk2nd` dedicada — caminho de flash permanente não
   explorado ainda.
