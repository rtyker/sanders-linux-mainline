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

## 17. Aparelho trava na tela de aviso "unlocked bootloader" do ABOOT de fábrica (não chega nem no lk2nd) — NÃO RESOLVIDO

**Data:** 2026-09-04
**Status:** ⚠️ Aberto — usuário está resolvendo fisicamente, não retomar automação até ele confirmar que voltou.

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

**Para o próximo agente/sessão:** não repetir os dois reflashes acima
sem motivo novo — já foram tentados e não fazem diferença. Se o usuário
confirmar que voltou a bootar normalmente após intervenção física,
perguntar como resolveu antes de continuar qualquer trabalho, e
considerar se algo na sequência de testes de botões (poweroff real via
tecla Power, ciclos rápidos de liga/desliga) tem relação causal real ou
foi coincidência.
