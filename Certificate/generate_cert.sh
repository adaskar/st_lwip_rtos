#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  ./generate_cert.sh rsa [options]
  ./generate_cert.sh ec  [options]

Examples:
  ./generate_cert.sh rsa --bits 2048
  ./generate_cert.sh ec --curve prime256v1
  ./generate_cert.sh ec --curve secp384r1 --days 90 --cn stm32.local

Options:
  --bits N        RSA key size. Default: 2048
  --curve NAME    EC curve. Default: prime256v1
                  Common OpenSSL names: prime256v1, secp384r1, secp521r1
  --days N        Certificate validity in days. Default: 365
  --cn NAME       Common Name. Default: com.on
  --subject SUBJ  Full OpenSSL subject. Overrides --cn.
                  Default: /C=TR/ST=ANKARA/L=ANKARA/O=ON/OU=ON/CN=com.on
  --out-dir DIR   Output directory. Default: this script's directory
  -h, --help      Show this help.

Outputs:
  cert.pem, key.pem, cert.h, key.h

Note:
  This firmware config currently enables secp256r1 only
  (OpenSSL curve name: prime256v1). Other curves require enabling the matching
  MBEDTLS_ECP_DP_* option in mbedtls_config.h before testing on-device.
USAGE
}

die() {
    echo "error: $*" >&2
    exit 1
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
kind="${1:-}"
if [[ -z "$kind" || "$kind" == "-h" || "$kind" == "--help" ]]; then
    usage
    exit 0
fi
shift

bits=2048
curve="prime256v1"
days=365
cn="com.on"
subject=""
out_dir="$script_dir"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bits)
            [[ $# -ge 2 ]] || die "--bits requires a value"
            bits="$2"
            shift 2
            ;;
        --curve)
            [[ $# -ge 2 ]] || die "--curve requires a value"
            curve="$2"
            shift 2
            ;;
        --days)
            [[ $# -ge 2 ]] || die "--days requires a value"
            days="$2"
            shift 2
            ;;
        --cn)
            [[ $# -ge 2 ]] || die "--cn requires a value"
            cn="$2"
            shift 2
            ;;
        --subject)
            [[ $# -ge 2 ]] || die "--subject requires a value"
            subject="$2"
            shift 2
            ;;
        --out-dir)
            [[ $# -ge 2 ]] || die "--out-dir requires a value"
            out_dir="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

case "$kind" in
    rsa|ec) ;;
    *) die "first argument must be 'rsa' or 'ec'" ;;
esac

[[ "$bits" =~ ^[0-9]+$ ]] || die "--bits must be numeric"
[[ "$days" =~ ^[0-9]+$ ]] || die "--days must be numeric"

require_cmd openssl
require_cmd od
mkdir -p "$out_dir"

cert_pem="$out_dir/cert.pem"
key_pem="$out_dir/key.pem"
cert_h="$out_dir/cert.h"
key_h="$out_dir/key.h"

if [[ -z "$subject" ]]; then
    subject="/C=TR/ST=ANKARA/L=ANKARA/O=ON/OU=ON/CN=$cn"
fi

case "$kind" in
    rsa)
        openssl req \
            -x509 \
            -newkey "rsa:$bits" \
            -nodes \
            -sha256 \
            -days "$days" \
            -subj "$subject" \
            -keyout "$key_pem" \
            -out "$cert_pem"
        ;;
    ec)
        openssl ecparam -name "$curve" -genkey -noout -out "$key_pem"
        openssl req \
            -x509 \
            -new \
            -sha256 \
            -days "$days" \
            -subj "$subject" \
            -key "$key_pem" \
            -out "$cert_pem"
        ;;
esac

pem_to_header() {
    local input="$1"
    local output="$2"
    local array_name="$3"
    local len_name="$4"
    local tmp
    tmp="$(mktemp)"

    cp "$input" "$tmp"
    printf '\0' >> "$tmp"

    {
        printf 'unsigned char %s[] = {\n' "$array_name"
        od -An -tx1 -v "$tmp" |
            awk '
                {
                    for (i = 1; i <= NF; i++) {
                        bytes[++n] = "0x" $i
                    }
                }
                END {
                    for (i = 1; i <= n; i++) {
                        if ((i - 1) % 12 == 0) {
                            printf "  "
                        }
                        printf "%s", bytes[i]
                        if (i < n) {
                            printf ", "
                        }
                        if (i % 12 == 0 || i == n) {
                            printf "\n"
                        }
                    }
                }
            '
        printf '};\n'
        printf 'unsigned int %s = %s;\n' "$len_name" "$(wc -c < "$tmp" | tr -d ' ')"
    } > "$output"

    rm -f "$tmp"
}

pem_to_header "$cert_pem" "$cert_h" cert_pem cert_pem_len
pem_to_header "$key_pem" "$key_h" key_pem key_pem_len

echo "Generated $kind certificate:"
echo "  $cert_pem"
echo "  $key_pem"
echo "  $cert_h"
echo "  $key_h"
