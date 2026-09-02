#!/bin/bash
# sanders-ssh-setup.sh — Configura chave SSH e endurece sshd
#
# Motivacao: o sshd vem com PasswordAuthentication=no (seguranca), mas
# o usuario precisa de uma chave.pub antes de ser bloqueado. Rode este
# script via serial (picocom) ANTES de tentar SSH.
#
# Uso (via serial / ttyACM0):
#   1. No HOST, gere a chave (se ainda nao tiver):
#        ssh-keygen -t ed25519 -C "sanders"
#   2. No DEVICE, cole a chave publica:
#        sanders-ssh-setup.sh < /caminho/chave.pub
#      Ou:
#        sanders-ssh-setup.sh              # entra em modo interativo
#
# O script:
#   - Cria /root/.ssh/ (se nao existir)
#   - Adiciona a chave a authorized_keys (deduplicando)
#   - Ajusta permissoes (700/600)
#   - Reinicia sshd pra aplicar

set -euo pipefail

AUTH_KEYS="/root/.ssh/authorized_keys"
KEY_FILE="${1:-}"

msg() { echo "[sanders-ssh] $*"; }
die() { echo "[sanders-ssh] ERRO: $*" >&2; exit 1; }

mkdir -p /root/.ssh
chmod 700 /root/.ssh

if [ -n "$KEY_FILE" ] && [ -f "$KEY_FILE" ]; then
    # Arquivo de chave informado como argumento.
    NEW_KEY=$(awk 'NR==1{print}' "$KEY_FILE")
else
    # Modo interativo: le da stdin.
    msg "Cole sua chave publica (uma linha, ex: ssh-ed25519 AAAA... user@host):"
    msg "Depois pressione Ctrl+D."
    NEW_KEY=$(cat | awk 'NR==1{print}')
fi

[ -z "$NEW_KEY" ] && die "nenhuma chave fornecida"

# Valida formato basico.
echo "$NEW_KEY" | grep -qE '^(ssh-ed25519|ssh-rsa|ecdsa-sha2) ' \
    || die "chave invalida (deve comecar com ssh-ed25519/ssh-rsa/ecdsa-sha2)"

# Deduplica: so adiciona se nao existe ja.
if [ -f "$AUTH_KEYS" ] && grep -qF "$NEW_KEY" "$AUTH_KEYS"; then
    msg "chave ja presente em $AUTH_KEYS, nada a fazer"
else
    echo "$NEW_KEY" >> "$AUTH_KEYS"
    msg "chave adicionada a $AUTH_KEYS"
fi

chmod 600 "$AUTH_KEYS"

# Reinicia sshd pra aplicar (se estiver rodando).
if systemctl is-active --quiet sshd; then
    systemctl restart sshd
    msg "sshd reiniciado"
else
    msg "sshd nao ativo — sera iniciado no proximo boot"
fi

msg "Pronto. Agora voce pode SSH via:"
msg "  ssh -i ~/.ssh/id_ed25519 root@10.42.0.2"
