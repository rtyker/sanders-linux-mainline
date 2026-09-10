# Troubleshooting

Problemas encontrados durante o desenvolvimento, com causa e solução.
Refira-se a este doc quando algo travar.

## 1. `fastboot boot lk2nd.img` retorna OKAY, mas aparelho fica no logo Motorola

**Sintoma:** o lk2nd carrega e mostra sua tela. Em seguida, ao
`fastboot boot boot-sanders.img`, o aparelho mostra o logo Motorola e
fica parado indefinidamente.

**Causa:** o lk2nd faz **match estrito** entre o `qcom,msm-id` /
`qcom,board-id` do DTB anexado e o hardware real
(`platform_dt_absolute_match()` em
`platform/msm_shared/dev_tree.c:1498`). Se não bate, **rejeita o DTB
silenciosamente** — kernel nunca é chamado.

**Solução:** listar **todas as 6 variantes** do sanders em
`qcom,board-id` no DTS:

```dts
qcom,board-id = <0x4B 0x8100>,
                <0x4B 0x8200>,
                <0x4B 0x8300>,
                <0x4B 0x83B0>,
                <0x4B 0x8400>,
                <0x4C 0x8400>;
```

O lk2nd itera sobre os pares e seleciona o que bate com o
`lk2nd_dt_override` populado a partir do hardware real. Já está
aplicado no `dts/msm8953-motorola-sanders.dts` deste repo.

## 2. `fastboot flash boot ...` falha no sanders

**Sintoma:** `flash boot` retorna erro, mesmo com bootloader
desbloqueado.

**Causa:** o sanders bloqueia escrita na partição `boot` por
verificação adicional (AVB / Motorola signing) que `unlock` sozinho
não desativa.

**Solução:** use `fastboot boot` (transitório, em RAM) em vez de
`flash boot`. Outras partições (`userdata`, `system`, etc.) aceitam
flash normalmente.

> ❌ **REFUTADO em 2026-09-02, testado ao vivo no `potter` físico.** A
> partição `lk2nd` dedicada **não existe** neste bootloader:
> `sudo fastboot flash lk2nd build/out/lk2nd.img` → `Invalid partition
> name lk2nd`. Confirmado também via `fastboot getvar partition-size:lk2nd`
> → resposta vazia. E mais grave: a linha `(bootloader) partition-size:lk2nd:
> 0x80000` citada como saída de `fastboot getvar all` neste device
> **nunca aparece** — este `getvar all` (Motorola moto-msm8953-C0.92) não
> lista `partition-size:*` para NENHUMA partição (verificado, saída
> completa não contém essas linhas para nenhum nome). Ou seja, a
> "descoberta" da partição `lk2nd` dedicada foi **inventada**, não
> observada. Veja `LK2ND_SETUP.md` para os detalhes e o que isso implica
> pro plano de boot autônomo.

## 3. Busybox 1.36.1 quebra ao compilar (`tc.c`)

**Sintoma:**
```
networking/tc.c:308: error: 'TC_CBQ_MAXPRIO' undeclared
networking/tc.c:309: error: invalid use of undefined type 'struct tc_cbq_wrropt'
```

**Causa:** headers Linux modernos (~v5.18+) removeram esses símbolos do
iproute. O `tc.c` do busybox 1.36.1 não foi atualizado.

**Solução:** `# CONFIG_TC is not set` no `.config` do busybox. Já está
aplicado no `03-build-busybox.sh`.

## 4. Kernel boota, mas init falha com `/sbin/switch_root: not found`

**Sintoma:**
```
init: exec: line N: /sbin/switch_root: not found
Kernel panic - not syncing: Attempted to kill init!
```

**Causa:** busybox foi linkado só em `/bin/`, não em `/sbin/`. O script
init chamou `/sbin/switch_root`.

**Solução:** criar symlinks em `/sbin/` apontando para `../bin/busybox`.
Já está aplicado no `04-build-initramfs.sh` (lê
`initramfs/busybox-symlinks-sbin.txt`).

## 5. Init não encontra rootfs (`/dev/disk/by-partlabel/` vazio)

**Sintoma:** init imprime `[init] partlabel:` (vazio) mesmo depois de
flashar a userdata.

**Causa:** initramfs busybox-only **não tem udev**. Os symlinks em
`/dev/disk/by-partlabel/` são criados por udev/systemd, não pelo kernel.

**Solução:** o init deste repo busca o rootfs em ordem:
1. `blkid -L rootfs` (busybox blkid)
2. iteração em `/dev/mmcblk0p*` lendo `LABEL`/`TYPE` com `sed`
3. fallback: maior partição ext4 (provavelmente a userdata recém-flashada)

A imagem ext4 é criada com `mkfs.ext4 -L rootfs`, então busca por label
funciona.

## 6. Init imprime `/bin/cut: not found`

**Sintoma:** spam de `[init] line NN: /bin/cut: not found` no boot.

**Causa:** lista de symlinks busybox incompleta — faltou `cut`.

**Solução:** já corrigido em `initramfs/busybox-symlinks-bin.txt`.
Garanta que `cut`, `tr`, `xargs`, `sed`, `wc`, `basename` etc. estão
linkados.

## 7. USB DWC3 fica "deferred probe pending: failed to initialize core"

**Sintoma:**
```
platform 7000000.usb: deferred probe pending: dwc3: failed to initialize core
gcc-msm8953 ...: sync_state() pending due to 79000.phy
```

A mensagem "failed to initialize core" sugere bug no driver, mas é
enganosa — na verdade o dwc3 está deferred esperando o supplier.

**Diagnóstico:** ler `/sys/kernel/debug/devices_deferred` revela:
```
7000000.usb     platform: supplier 79000.phy not ready
```

**Causa:** `CONFIG_PHY_QCOM_QUSB2=m` (modular) no defconfig. Initramfs
minimal não tem `modprobe`, então o PHY USB nunca probava → dwc3 ficava
deferred infinito.

**Solução:** `CONFIG_PHY_QCOM_QUSB2=y` no config fragment. Já aplicado
em `kernel/sanders.config.fragment`.

**Lição:** quando ver "deferred probe pending: ... failed to initialize",
o diagnóstico mais valioso é `cat /sys/kernel/debug/devices_deferred`
no userspace (ou no initramfs após `mount -t debugfs none /sys/kernel/debug`).
Mostra exatamente qual supplier está faltando.

## 8. fastboot precisa de senha sudo

Em background o sudo falha (sem TTY). Rode os scripts que precisam de
sudo no terminal interativo:

```bash
sudo ./scripts/05-build-rootfs.sh
./scripts/07-flash-and-boot.sh     # ele faz sudo fastboot internamente
```

## 9. `dtc` ou `flex/bison` faltando

**Sintoma:**
```
HOSTCC  scripts/dtc/dtc-parser.tab.o
sh: line 1: bison: command not found
```

**Solução:** `./scripts/00-setup-host.sh` instala `flex`, `bison`, `dtc`.

## 10. Compilação do kernel sai diferente do esperado

Sempre rode `make olddefconfig` após mexer no `.config` para o kernel
expandir dependências. O script `02-build-kernel.sh` já faz isso.

## 11. Touchscreen "probe failed (-110)" no FT5436

**Sintoma:**
```
edt_ft5x06 0-0038: touchscreen probe failed
edt_ft5x06 0-0038: probe with driver edt_ft5x06 failed with error -110
```

**Causa:** O FT5436 do sanders não expõe o registrador `0xBB` (nome do
modelo) que o `edt_ft5x06_ts_identify()` do driver mainline lê. Logo o
probe aborta com `-ETIMEDOUT` antes de qualquer touch funcionar.

**Solução:** patch incluso em `kernel/0001-edt-ft5x06-skip-identify-for-ft5436.patch`
— em vez de abortar quando o identify falha, assume defaults de "generic
ft5x06" (M09, sem regmap separado). O script `02-build-kernel.sh` aplica
o patch automaticamente.

## 12. Descobrir o chip de touch/sensor real do sanders

Sanders **não é igual ao potter**. Mesmo CPU (msm8953), mesmo board family
Motorola, mas componentes do board diferentes (touchscreen, painel,
sensores, talvez Wi-Fi cal). Para descobrir o chip real de algum periférico:

```bash
# Extrair DTB do Android stock
python3 unpack_bootimg.py SANDERS_..._boot.img /tmp/stock
# O DT vem comprimido em LZ4:
lz4 -d /tmp/stock/dt /tmp/stock/dt.bin
# QCDT v3, várias DTBs concatenadas. Extrair a do sanders:
python3 qcdt_extract.py /tmp/stock/dt.bin /tmp/stock/dtbs
# Decompilar:
dtc -I dtb -O dts -o /tmp/sanders-stock.dts /tmp/stock/dtbs/00_*.dtb
# Procurar o chip:
grep -E 'touch|focaltech|synaptics|atmel|wcnss|bluetooth' /tmp/sanders-stock.dts
```

Scripts auxiliares em `tools/unpack_bootimg.py` e `tools/qcdt_extract.py`.
Sem esse passo, "deve ser igual ao potter" é um chute caro — no caso do
touch, custou muitas iterações até descobrir que o sanders usa Focaltech
FT5436 (não Synaptics RMI4 como o potter).

## 13. USB CDC ECM com TX stuck (qdisc enche, tx_packets fica em 0)

**Sintoma:** após rebuild com kernel 7.1.0-rc4, a interface ECM no host
enumera normalmente (carrier=1, RX cresce), mas nenhum pacote sai do host
pro phone. `tc -s qdisc show dev <iface>` mostra backlog enchendo
(centenas de pacotes presos), `tx_packets` permanece zerado e
`ping 10.42.0.2` perde 100%.

**Causa:** ordem em que as functions são registradas no configfs do
gadget. Se `acm.usb0` for criada *antes* de `ecm.usb0`, no kernel
7.1.0-rc4 o IN endpoint do ECM não recebe completion do dwc3 — o
`u_ether` faz `netif_stop_queue` e nunca religa. Em kernels mais
antigos (até 2026-05-19, v7.0) o mesmo init funcionava nas duas ordens.

**Fix:** no `initramfs/init`, criar `ecm.usb0` primeiro e só depois
`acm.usb0`. Mudar a ordem dos `mkdir`/`ln -s` em `setup_usb_gadget()`
basta — não precisa pinar kernel, nem trocar ECM por RNDIS, nem
desabilitar ACM. ifname do host muda de `enp0s20f0u4i2` (ACM primeiro)
para `enp0s20f0u4` (ECM primeiro), mas isso é cosmético — o
`scripts/08-host-net.sh` detecta pelo MAC `02:11:22:33:44:55`.

## 16. Phosh (Phoc) inicia mas a tela fica preta (`Timeout 3000ms expired with 1 configures pending`)

**Sintoma:** Phoc + Phosh + gnome-session + Squeekboard sobem todos (procs
vivos), DRM faz modeset OK em `Unknown-1` 1080x1920@60, mas a tela permanece
preta. Journal mostra `Phosh ready` seguido de
`Timeout (3000ms) expired with 1 configures pending`.

**Causa:** wlroots tenta o caminho **EGL/GBM** para apresentar frames no
simpledrm. `simpledrm` é um driver "fake KMS" que só expõe o
`simple-framebuffer` do bootloader — não suporta o pipeline GBM/dma-buf
completo. Mesmo com `WLR_RENDERER_ALLOW_SOFTWARE=1` (que libera llvmpipe), o
ciclo de buffers GBM→DRM não fecha e nada chega ao painel.

**Fix:** forçar wlroots a usar o renderer **pixman** puro, que escreve
diretamente em DRM dumb buffers (caminho que o simpledrm aceita — é o que o
Weston também usa nesse sanders):

```
Environment=WLR_RENDERER=pixman
```

Já aplicado no `phosh.service` versionado em
`rootfs-overlay/desktop/etc/systemd/system/phosh.service`.

**Outras pegadinhas no caminho:**

- `WLR_DRM_NO_ATOMIC=1` parece travar phoc antes de iniciar o gnome-session.
  Não usar.
- `rotate = 270` no `phoc.ini` também trava — simpledrm não tem rotação por
  plane DRM. Deixar `transform`/`rotate` fora do `[output:Unknown-1]`.
- `scale = 3` no `[output:Unknown-1]` funciona e é necessário (painel
  ~400 DPI fica minúsculo em scale=1).

## 15. Rootfs cheia logo após instalar (df mostra 2.9 GiB, não 24 GiB)

**Sintoma:** `df -h /` reporta 2.9 GiB total / ~100% usado depois de poucos
`pacman -S`. A partição (`mmcblk0p54`) tem 23.9 GiB, mas o filesystem dentro
não.

**Causa:** `05-build-rootfs.sh` cria a imagem com `truncate -s 3G` +
`mkfs.ext4`. O `fastboot flash userdata` grava esses 3 GiB literalmente em
cima da partição de 24 GiB — o ext4 não cresce sozinho pra ocupar o resto.

**Fix permanente (já no rootfs):** o oneshot
`sanders-rootfs-expand.service` (instalado pelo `05-build-rootfs.sh`) roda
no primeiro boot, chama `resize2fs $(findmnt -no SOURCE /)` e cria o marker
`/var/lib/sanders-rootfs-expanded` pra não rodar de novo.

**Fix manual em rootfs antigo:**
```bash
resize2fs /dev/mmcblk0p54
touch /var/lib/sanders-rootfs-expanded   # impede o oneshot de tentar de novo
```
`resize2fs` online (com / montado rw) funciona — não precisa unmount.

## 14. Interface ECM aparece sem IPv4 (NetworkManager remove o IP estático)

**Sintoma:** rodei `scripts/08-host-net.sh` (ou `ip addr add`), `ip -4 addr`
mostra `10.42.0.1/24` por alguns segundos, e logo depois o IP some sozinho.
A interface continua UP, carrier=1, MAC certo — só o IPv4 sumiu.

**Causa:** o NetworkManager classifica `enp0s...` como ethernet padrão,
tenta DHCP, o phone não responde DHCP, NM marca a interface como
`disconnected` e *limpa* qualquer endereço IPv4 que estiver lá — inclusive
o que setamos manualmente. `journalctl -u NetworkManager` mostra
`state change: ip-config -> failed (reason 'ip-config-unavailable')`
seguido de `failed -> disconnected`.

**Fix:** criar uma conexão NM estática para essa interface:

```bash
sudo nmcli connection add type ethernet ifname enp0s20f0u4 \
    con-name sanders-ecm ipv4.method manual \
    ipv4.addresses 10.42.0.1/24 ipv6.method ignore \
    connection.autoconnect yes
sudo nmcli connection up sanders-ecm
```

Com isso o NM aplica o IP automaticamente toda vez que a interface
aparece, e o `08-host-net.sh` só precisa cuidar de NAT/forward.

## 17. Aparelho "trava" na tela de aviso "unlocked bootloader" do ABOOT — causa real era o HOST, não o aparelho

**Data:** 2026-09-04
**Status:** ✅ Resolvido — causa raiz era ambiental (host), não o aparelho/lk2nd/kernel.

**Causa raiz real:** uma VM do `virt-manager`/libvirt rodando no HOST de
desenvolvimento estava configurada para **sequestrar automaticamente**
o dispositivo USB do aparelho (regra de passthrough automático por
idVendor/idProduct, comum em configs de VM Windows-pra-ADB/fastboot ou
similar) toda vez que ele reenumerava. Isso fazia o Linux do host
perder a interface bem no meio da sequência de boot, dando a falsa
impressão de que o aparelho tinha travado na tela do ABOOT — na
verdade o aparelho provavelmente seguia o boot normalmente, só que o
host não conseguia mais falar com ele (nem serial, nem fastboot
estável) porque a VM tinha acabado de puxar o dispositivo pra si.
**Nenhuma das duas ações de recuperação abaixo (seção antiga) foi
necessária** — o problema nunca esteve na `cache`, no `lk2nd`, nem no
hardware do aparelho.

**Fix:** desabilitar/desativar a regra de auto-attach de USB dessa VM
no `virt-manager` (ou parar a VM) antes de trabalhar no aparelho.

**Efeito colateral do diagnóstico incorreto:** durante a tentativa de
recuperação, foi feito `fastboot flash cache build/out/boot-cache.img`
usando o arquivo mais recente por data de modificação no host — mas
esse arquivo tinha sido **sobrescrito por outra sessão de trabalho
concorrente** (rodando `06-build-boot.sh` em paralelo, provavelmente
a mesma frente de FastRPC/SLPI de sensores) com um kernel **antigo**
(`7.1.0-rc4-...-dirty #33`, de 2026-09-03), não o kernel com GPU
validado nesta sessão (`7.2.0-dirty #43`). Resultado: depois do
aparelho voltar a bootar normalmente, a GPU regressiu
(`no GPU device was found` de novo) porque o kernel errado ficou
gravado na `cache`. **Lição:** não confiar no timestamp de
`build/out/*.img` como proxy de "última build boa" quando há mais de
uma sessão/agente rodando `02-build-kernel.sh`/`06-build-boot.sh` no
mesmo `$BUILD` compartilhado — confirmar a versão do kernel
(`uname -a`) e o conteúdo esperado (ex.: `grep CONFIG_DRM_MSM_DPU
build/linux/.config`) antes de flashar em situação de recuperação.

---

### Histórico da investigação original (diagnóstico incorreto, mantido para contexto)

O texto abaixo documenta o que foi tentado **antes** de descobrir a causa
real acima — mantido porque as duas ações (reflash de `cache` e de
`boot`/`recovery`) continuam válidas como recursos de recuperação
genuínos para quando o problema for de verdade no aparelho, só não
foram a causa nem a solução desta vez.

**Sintoma:** boot normal (power-on ou `fastboot reboot`) mostra a tela
padrão do ABOOT desbloqueado ("Your device has been unlocked and can't
be trusted... your device will boot in 5 seconds"), vibra uma vez, mas
**nunca sai dela** — nem chega a mostrar a tela do `lk2nd` (apelido
interno: "lk"), muito menos o Linux. Rodapé da tela mostra a versão do
ABOOT ("19.0"). `fastboot devices` continua enxergando o aparelho
normalmente o tempo todo (forçando o modo manualmente via botão) —
**o bootloader ABOOT em si responde**, só a sequência de boot normal
(ABOOT → aviso → chainload pro `lk2nd`) é que não progride.

**Contexto de quando apareceu:** durante uma sessão de trabalho em outra
frente (botões de volume associados ao PipeWire, ver
`docs/archived/AUDIO_VOLUME_BUTTONS_INVESTIGATION.md`) que envolveu
vários testes de apertar Volume Up/Down/Power fisicamente repetidas
vezes, incluindo um teste que confirmou o botão Power disparando um
`poweroff` real via systemd-logind. Depois de um desses ciclos de
liga/desliga físicos, o aparelho passou a travar nessa tela.

**O que foi tentado e NÃO resolveu** (cada um confirmado com reboot
completo depois, mesmo resultado):

1. `fastboot flash cache build/out/boot-cache.img` (última imagem
   conhecida-boa, com kernel+DTB+initramfs do trabalho de GPU já
   validado ao vivo horas antes) — sem efeito. Isso descarta corrupção
   do kernel/DTB/initramfs/`extlinux.conf` como causa, já que nem chega
   a esse estágio.
2. `fastboot flash boot build/out/lk2nd.img` + `fastboot flash recovery
   build/out/lk2nd.img` (mesmo binário do lk2nd que já devia estar
   gravado ali permanentemente, ver seção "Boot Architecture" do
   `AGENTS.md`) — sem efeito. Ambos os flashes reportaram `OKAY` (o
   aviso `Image not signed or corrupt` é esperado/normal em bootloader
   desbloqueado, não é erro). Isso descarta corrupção das partições
   `boot`/`recovery` como causa — o problema é anterior a isso, na
   própria lógica de boot do ABOOT de fábrica.

**Hipóteses não testadas** (o processo foi interrompido a pedido do
usuário antes de tentar):
- Corte de energia forçado (segurar Power ~10-15s até vibrar/desligar
  de vez) seguido de power-on normal — não testado, foi o próximo passo
  sugerido quando a sessão parou.
- Problema de hardware genuíno (eMMC, conector, bateria/energia,
  térmico) não relacionado a nenhuma escrita de partição feita nesta
  sessão.
- Nenhuma mudança feita nesta sessão tocou em `boot`/`recovery`/`cache`
  antes do travamento começar — os únicos arquivos alterados no device
  rodando antes disso foram userspace puro (`/usr/local/bin/sanders-volume-keys.py`,
  rodado como processo em foreground via SSH, nada que toque bootloader
  ou partições).

**Para o próximo agente/sessão:** se o aparelho parecer travar no ABOOT
de novo, **verifique primeiro se alguma VM local (virt-manager/libvirt,
VirtualBox, etc.) está com auto-attach de USB configurado** antes de
sair reflashando partições — foi essa a causa real desta vez, não o
aparelho. Os testes de botões físicos (poweroff real via tecla Power,
ciclos de liga/desliga) não tiveram relação causal — foi coincidência
de timing com a VM.

## 18. Boot trava depois do `lk`, sem chegar no Linux — `.config` do kernel com drift de sessões concorrentes

**Data:** 2026-09-04
**Status:** ✅ Resolvido — causa confirmada por reprodução e correção (não só teoria).

**Sintoma:** depois de resolver o incidente #17 (VM roubando USB), o
recovery reflashou por engano um `build/out/boot-cache.img` desatualizado
(kernel `7.1.0-rc4 #33`, sem GPU). Um rebuild "de recuperação" rodado em
seguida (sem apagar `build/linux/.config`) gerou um `Image.gz` visivelmente
menor (13MB, contra ~17-18MB dos builds que funcionavam) e, ao ser
deployado, travava o boot **depois** da tela do `lk` (mais adiante que o
incidente #17 — não chegava a mostrar nada no Linux, nem log de kernel
nenhum no serial).

**Causa raiz confirmada:** `scripts/02-build-kernel.sh` só roda `make
defconfig` se `.config` **não existir**:
```bash
if [ ! -f .config ]; then
    make ARCH=arm64 CROSS_COMPILE="$ARM64_CC" defconfig
fi
```
Como `$BUILD/linux` é uma árvore **compartilhada** entre sessões/agentes
trabalhando em paralelo no mesmo host, um `.config` deixado por uma
build anterior (nesse caso, de quando outra sessão testava
`CONFIG_QCOM_FASTRPC=y` antes de decidir desativá-lo e comentar no
fragment) continua servindo de base pra sempre. `merge_config.sh -m
.config fragment` só sobrescreve os símbolos **explícitos no fragment
atual** — um símbolo que uma versão antiga do fragment forçava e a
versão nova removeu/comentou fica preso com o valor velho
silenciosamente, sem warning nenhum. Diferente das patches de kernel
(que são idempotentes por design, ver `AGENTS.md`), esse merge de
`.config` **não é idempotente entre versões diferentes do fragment**.

**Fix aplicado:** apagar `build/linux/.config` e rodar
`02-build-kernel.sh` do zero (força `defconfig` limpo + merge do
fragment atual, sem nenhum resíduo). Resultado: `Image.gz` voltou a
~17MB, `boot-cache.img` voltou a ~30MB (batendo com os builds que
funcionavam), e o deploy bootou normalmente — GPU incluída, zero erros
no dmesg, confirmado via SSH (`uname -a` → kernel novo, `/dev/dri/card1`
presente).

**Correção permanente aplicada (2026-09-04, commit `ef18b23`):**
`02-build-kernel.sh` agora sempre roda `defconfig` do zero antes de
aplicar o fragment, em vez de só quando `.config` não existe — elimina
o drift de vez, não é mais preciso lembrar de `rm -f .config`
manualmente. Testado: build reproduziu o `.config` correto (`Image.gz`
17MB, mesmo tamanho dos builds que funcionam) sem recompilar o que já
estava certo (ccache seguiu cobrindo normalmente).

**Para o próximo agente/sessão:** o fix já está no script — não precisa
mais fazer `rm -f build/linux/.config` manualmente antes de builds de
recuperação. Se um deploy travar o boot de forma misteriosa mesmo assim,
comparar o tamanho do `Image.gz` contra o último build que funcionou
continua sendo um sinal rápido de diagnóstico (diferença de vários MB é
suspeita).

## 19. LED frontal de notificação nunca acende (sysfs/SPMI "funcionam" mas nenhuma luz aparece) — componente inexistente no XT1683

**Data:** 2026-09-10 (investigação encerrada)
**Status:** ✅ Causa definitiva encontrada — **hardware não povoado; não é bug de software. Não re-abrir.**

**Sintoma:** escrever em `/sys/class/leds/white:notification/brightness`
ou direto no registrador SPMI do LED ATC do PMI8950 (`0x1243`, via
`moto_led`/MMIO) retorna sucesso e a leitura de volta confirma o valor —
mas **nenhuma luz aparece** no difusor da grade do alto-falante, nem no
escuro.

**Causa raiz:** o Moto G5 Plus XT1683 RETBR (unidade de bancada
`potter`) **não possui o LED frontal povoado** (ou a trilha não chega ao
difusor ótico). Investigação exaustiva em 3 fases — varredura pulsada de
todos os GPIOs/MPPs livres dos PMICs, engenharia reversa do driver stock
`atc_leds` (LED ATC, BAT_IF `0x1243`, bits [2:1], max_brightness=3), e
escritas diretas verificadas em todos os modos (solid ON, blink 1/2,
10 blinks software) incluindo com o caminho analógico de carga habilitado
(`0x1242=0x00` com unlock `0x12D0=0xA5`) — confirmada visualmente pelo
usuário em duas sessões: **nada acendeu**. Relatos históricos de outros
donos de XT1683 no XDA corroboram. No stock o LED era só "sign of life"
(bateria em descarga profunda), e mesmo esse caminho está morto nesta
variante.

**Solução:** nenhuma — não há o que consertar em software. Limpeza já
executada em 2026-09-10: nó fictício `white:notification` **removido do
DTS** (comentário no próprio `dts/msm8953-motorola-sanders.dts` explica e
proíbe re-adicionar), `sanders-led.service`/`.timer`/`sanders-led.sh`
removidos do rootfs-overlay e do aparelho, kernel recompilado/deployado e
verificado ao vivo (`/proc/device-tree` sem nó `leds`, `systemctl
--failed` limpo). **Não re-adicionar o nó nem recriar os serviços.**
Detalhes completos da investigação:
`docs/archived/RELATORIO_INVESTIGACAO_LED_FRONTAL.md`; veredito formal em
`docs/HARDWARE_REFERENCE.md` (seção LEDs) e em
`sanders-linux-mainline/docs/HARDWARE_STATUS.md`.

*Investigação executada e encerrada por **BF** (agente Codebuff),
2026-09-10.*

## 20. Host de build Debian sem toolchain cross (`aarch64-linux-gnu-gcc`, `dtc`, `mkbootimg`) e sem `pacman` — build do kernel via Docker

**Data:** 2026-09-10
**Status:** ✅ Resolvido com container; receita commitada no repo.

**Sintoma:** `02-build-kernel.sh` falha com `comando
'aarch64-linux-gnu-gcc' não encontrado` no host Debian 13. O
`00-setup-host.sh` é escrito pra Arch (`pacman`) e só imprime dicas de
tradução pra outras distros — e o `sudo` deste host exige senha
interativa (indisponível pra agentes), então não dá pra instalar
`gcc-aarch64-linux-gnu`, `device-tree-compiler` e `mkbootimg` via apt.

**Causa:** os builds anteriores rodaram no host de bancada Arch. O host
Debian atual tem `flex`/`bison`/`bc`/`ccache`/`gcc` nativo, mas nenhum
cross-compiler arm64 nem as ferramentas de empacotamento de boot.

**Solução:** Docker (o usuário já está no grupo `docker`), com o projeto
bind-mountado e o ccache do projeto reaproveitado. Receita commitada em
`scratch/kbuild-docker/Dockerfile` (imagem `sanders-kbuild:bookworm`:
`gcc-aarch64-linux-gnu`, `binutils-aarch64-linux-gnu`,
`device-tree-compiler`, `bc flex bison`, `ccache`, `libssl-dev
libelf-dev`, `mkbootimg`, `e2fsprogs`, `git`). Uso:

```bash
docker build -q -t sanders-kbuild:bookworm scratch/kbuild-docker/
docker run --rm -u 1000:1000 \
  -v /mnt/hdauxiliar/android/projeto_g5:/proj \
  -w /proj/sanders-linux-mainline/scripts \
  sanders-kbuild:bookworm ./02-build-kernel.sh   # e depois ./06-build-boot.sh
```

- Rodar com `-u 1000:1000` pra os artefatos ficarem com o dono do host
  (`anderson`, uid 1000).
- O `CCACHE_DIR` do container aponta pra `/proj/cache_ccache_aarch64`
  (mesmo cache do host) — ccache reutiliza normalmente entre container e
  host.
- Timing observado: build de v7.2 com ~69% de cache miss ≈ **40 min**;
  builds incrementais/só-DTB levam segundos.
- O deploy não muda: `10-deploy-boot.sh --reboot <IP>` roda no host
  (só precisa de `ssh`/`scp`).
- **Armadilha pra agentes:** processos em background lançados num comando
  SYNC morrem quando o comando retorna — para builds longos usar
  `setsid nohup ... &` + polling do log (o modo BACKGROUND da tool de
  terminal não está implementado nesta sessão).

*Configurado e validado ponta a ponta (build + deploy + boot verificado)
por **BF** (agente Codebuff), 2026-09-10.*
