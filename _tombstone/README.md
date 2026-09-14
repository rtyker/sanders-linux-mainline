# _tombstone/ (sanders-linux-mainline)

Experimentos arquivados deste workspace — não fazem parte do pipeline
ativo, mantidos só de referência. Mesmo conceito do `_tombstone/` do
projeto pai, escopado a este submódulo porque os arquivos aqui dentro
são parte da história de git dele, não do repo pai.

- **`sanders-lean.config.fragment`** + **`build-kernel-lean.sh`** —
  kernel "enxuto" (headless-only): removia display/DRM/GPU, áudio,
  touchscreen e sensores do `.config`, mantendo só rede/Bluetooth/Docker/
  zram/watchdog. Buildado com sucesso via Docker e **deployado ao vivo no
  potter em 2026-09-14** (boot limpo, 0 unidades falhas, confirmado por
  SSH). Abandonado a pedido do usuário no mesmo dia: o ganho de tamanho
  do `Image.gz` foi modesto (16M vs 17M do completo, ~6% — a maior parte
  do peso do `arm64 defconfig` é código de outras plataformas que nenhum
  dos dois fragments toca; um ganho de verdade exigiria trimmar o próprio
  `defconfig` base, não só o fragment por cima), não justificando manter
  um segundo kernel em paralelo. Aparelho revertido pro kernel completo
  no mesmo dia. Histórico completo em
  `docs/ROADMAP_AND_TODOS.md` (entrada 2026-09-14) e
  `docs/TROUBLESHOOTING.md` #22 (inclui um incidente real de recuperação
  de `pacman`/`systemd` no caminho, sem relação com o kernel em si — essa
  lição continua válida e não foi revertida).

Se algum dia quiser retomar: o script já tem o isolamento (`build-lean/`,
não mexe no `build/` principal) e o compartilhamento de ccache resolvidos
— só precisa atualizar os paths (`$REPO/kernel/...` →
`$REPO/_tombstone/...`, já corrigido nesta arquivagem).
