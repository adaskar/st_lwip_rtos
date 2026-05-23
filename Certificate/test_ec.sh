#!/bin/bash

HOST="192.168.100.10"
PORT="443"
OPENSSL="/opt/homebrew/opt/openssl@3/bin/openssl"

CIPHERS=(
"ECDHE-ECDSA-AES256-SHA"
"ECDHE-ECDSA-AES128-GCM-SHA256"
"ECDHE-ECDSA-AES128-SHA256"
"ECDHE-ECDSA-AES128-SHA"

# optional / static ECDH, usually not needed
"ECDH-ECDSA-AES256-SHA"
"ECDH-ECDSA-AES128-GCM-SHA256"
"ECDH-ECDSA-AES128-SHA256"
"ECDH-ECDSA-AES128-SHA"
)

for round in {1..10}; do
    echo
    echo "===================================================="
    echo "ROUND $round - $(date)"
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
            echo "$RESULT" | grep -E "Protocol version|Ciphersuite|Peer certificate|Signature type|Verification error"
        else
            echo "RESULT: FAILED"
            echo "$RESULT" | grep -E "alert|handshake failure|no cipher match|wrong version|unexpected eof|Cipher is|error:"
        fi
    done
    

    echo
    echo "Sleeping 2 seconds..."
    sleep 2
done