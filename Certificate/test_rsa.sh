#!/bin/bash

HOST="192.168.100.10"
PORT="443"
OPENSSL="/opt/homebrew/opt/openssl@3/bin/openssl"

CIPHERS=(
"ECDHE-RSA-AES256-SHA"
"ECDHE-RSA-AES128-GCM-SHA256"
"ECDHE-RSA-AES128-SHA256"
"ECDHE-RSA-AES128-SHA"
"AES256-SHA256"
"AES256-SHA"
"AES128-GCM-SHA256"
"AES128-SHA256"
"AES128-SHA"
"ECDH-RSA-AES256-SHA"
"ECDH-RSA-AES128-GCM-SHA256"
"ECDH-RSA-AES128-SHA256"
"ECDH-RSA-AES128-SHA"
)

while true; do
    echo "===================================================="
    echo "TLS cipher test started: $(date)"
    echo "===================================================="

    for CIPHER in "${CIPHERS[@]}"; do
        echo
        echo "Testing: $CIPHER"
        echo "----------------------------------------------------"

        RESULT=$(
            echo Q | "$OPENSSL" s_client \
                -connect "$HOST:$PORT" \
                -tls1_2 \
                -cipher "$CIPHER" \
                -brief \
                -ign_eof 2>&1
        )

        if echo "$RESULT" | grep -q "Protocol version: TLSv1.2"; then
            echo "RESULT: OK"
            echo "$RESULT" | grep -E "Protocol version|Ciphersuite|Peer certificate|Verification error"
        else
            echo "RESULT: FAILED"
            echo "$RESULT" | grep -E "alert|handshake failure|no cipher match|wrong version|unexpected eof|Cipher is|error:"
        fi
    done

    echo
    echo "Sleeping 2 seconds..."
    sleep 2
done