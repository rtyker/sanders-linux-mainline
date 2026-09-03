# lk2nd no sanders / potter — bootloader 2º estágio

Procedimento para compilar, carregar e persistir o **lk2nd** (Little Kernel 2nd stage) no Moto G5s Plus (`sanders`) e Moto G5 Plus (`potter`).

> 🚀 **STATUS DE DEPLOY:** A gravação permanente do `lk2nd` na partição `boot` do eMMC está **concluída e validada**. O bootloader stock da Motorola (ABOOT) chainloadeia o `lk2nd` automaticamente no cold boot.
> Veja os detalhes completos em [`docs/AUTONOMOUS_DIRECT_BOOT_PLAN.md`](../../docs/AUTONOMOUS_DIRECT_BOOT_PLAN.md).

---

## 1. Pré-requisitos

- Bootloader Motorola **desbloqueado** (`fastboot oem unlock`).
  - Verifique: `sudo fastboot getvar unlocked` → `yes`
- Toolchain `arm-none-eabi-gcc` + binutils + newlib.
- `android-tools` para `fastboot`.

---

## 2. Compilação (Automatizada)

```bash
./scripts/01-build-lk2nd.sh
```

- Fonte: Repositório `lk2nd` compilado com target `lk2nd-msm8953`.
- Saída: `build/out/lk2nd.img` (~353 KB).

---

## 3. Gravação e Execução

### A. Boot Transitório via RAM (Modo Desenvolvimento)
```bash
sudo fastboot boot build/out/lk2nd.img
```

### B. Flash Permanente no eMMC (Deploy Definitivo)
- O fastboot stock de fábrica da Motorola rejeita a gravação direta da partição `boot`.
- No entanto, a gravação pode ser feita:
  1. Pelo próprio fastboot do `lk2nd`: `sudo fastboot flash boot build/out/lk2nd.img`
  2. Ou diretamente dentro do Linux via `dd`:
     ```bash
     dd if=/root/lk2nd.img of=/dev/mmcblk0p37 bs=4M conv=fsync
     ```
- No cold boot, o ABOOT de fábrica executa a partição `boot`, carregando o `lk2nd` de forma 100% autônoma.

---

## 4. Inicialização do Kernel Mainline via Extlinux
O `lk2nd` lê a partição `cache` (`mmcblk0p52`, formatada em ext2) procurando por `/extlinux/extlinux.conf` e salta para o Kernel Linux Mainline 6.x/7.x sem necessidade de computador conectado.
