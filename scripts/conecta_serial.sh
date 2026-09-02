#!/usr/bin/env bash
# Script para conectar na serial CDC ACM do Moto G5s Plus / Moto G5 Plus
# Configura o picocom com tratamento de quebra de linha (CR/LF) automático.

PORT="${1:-/dev/ttyACM0}"

echo "============================================================"
echo " Conectando no terminal serial: $PORT"
echo " Pressione Ctrl+A e depois Ctrl+X para sair do picocom"
echo "============================================================"

if [ ! -c "$PORT" ]; then
    echo "Aguardando dispositivo $PORT aparecer..."
    while [ ! -c "$PORT" ]; do
        sleep 1
        echo -n "."
    done
    echo ""
    echo "Dispositivo $PORT encontrado!"
fi

# Executa picocom com mapeamento correto de Newline/Carriage Return
exec picocom --omap crlf --imap lfcrlf --baud 115200 "$PORT"
