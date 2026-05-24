#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  ./generate_certbot_cert.sh domain    --domain NAME --email EMAIL [options]
  ./generate_certbot_cert.sh public-ip --ip IP       --email EMAIL [options]
  ./generate_certbot_cert.sh install-existing --lineage DIR [options]

Examples:
  # Best fit for an STM32 board on a private LAN:
  # Issue a public cert for a real DNS name via DNS-01, then access the board
  # as https://stm32.example.com after local DNS resolves it to 192.168.100.10.
  ./generate_certbot_cert.sh domain --domain stm32.example.com --email you@example.com --challenge dns

  # Public HTTP validation, if a webserver can serve /.well-known/acme-challenge on port 80.
  ./generate_certbot_cert.sh domain --domain stm32.example.com --email you@example.com --challenge webroot --webroot /var/www/html

  # Public IP certificate. This cannot work for 192.168.x.x, 10.x.x.x, 172.16-31.x.x, localhost, etc.
  ./generate_certbot_cert.sh public-ip --ip 203.0.113.10 --email you@example.com --challenge webroot --webroot /var/www/html

  # Convert a previously issued certbot lineage to cert.h/key.h.
  ./generate_certbot_cert.sh install-existing --lineage ./letsencrypt/config/live/stm32.example.com

Options:
  --domain NAME       DNS name to include. Can be used multiple times.
  --ip IP             Public IPv4/IPv6 address to include. Can be used multiple times.
  --email EMAIL       ACME account email.
  --no-email          Register without an email address.
  --challenge TYPE    dns, webroot, standalone, manual-http. Default: dns for domain, webroot for public-ip.
  --webroot DIR       Webroot for the webroot challenge.
  --http-port N       Local certbot standalone HTTP port. Default: 80.
                     The CA still validates through public port 80.
  --key-type TYPE     ecdsa or rsa. Default: ecdsa.
  --curve NAME        ECDSA curve. Default: secp256r1.
  --rsa-bits N        RSA key size. Default: 2048.
  --cert-name NAME    Certbot lineage name. Default: first domain/IP.
  --config-dir DIR    Certbot config dir. Default: ./letsencrypt/config
  --work-dir DIR      Certbot work dir. Default: ./letsencrypt/work
  --logs-dir DIR      Certbot logs dir. Default: ./letsencrypt/logs
  --out-dir DIR       Output directory for cert.pem/key.pem/cert.h/key.h. Default: this folder.
  --staging           Use Let's Encrypt staging.
  --dry-run           Test renewal/issuance flow without saving certs.
  --force-renewal     Force certbot renewal/reissue.
  --reuse-key         Ask certbot to reuse the current private key on renewal.
  -h, --help          Show this help.

Outputs:
  cert.pem, key.pem, cert.h, key.h

Important:
  Public CAs cannot issue a trusted certificate for private LAN IP
  192.168.100.10. For a trusted browser/curl test on that board, use a real
  domain name and make your LAN DNS/hosts file resolve that name to 192.168.100.10.
USAGE
}

die() {
    echo "error: $*" >&2
    exit 1
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

is_private_ipv4() {
    local ip="$1"
    local a b c d
    IFS=. read -r a b c d <<< "$ip"
    [[ "$a" =~ ^[0-9]+$ && "$b" =~ ^[0-9]+$ && "$c" =~ ^[0-9]+$ && "$d" =~ ^[0-9]+$ ]] || return 1
    (( a == 10 )) && return 0
    (( a == 127 )) && return 0
    (( a == 169 && b == 254 )) && return 0
    (( a == 172 && b >= 16 && b <= 31 )) && return 0
    (( a == 192 && b == 168 )) && return 0
    (( a == 0 )) && return 0
    return 1
}

is_private_ipv6() {
    local ip="${1,,}"
    [[ "$ip" == "::1" || "$ip" == fc* || "$ip" == fd* || "$ip" == fe80:* ]]
}

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

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-}"
if [[ -z "$mode" || "$mode" == "-h" || "$mode" == "--help" ]]; then
    usage
    exit 0
fi
shift

domains=()
ips=()
email=""
no_email=0
challenge=""
webroot=""
http_port=80
key_type="ecdsa"
curve="secp256r1"
rsa_bits=2048
cert_name=""
state_dir="$script_dir/letsencrypt"
config_dir=""
work_dir=""
logs_dir=""
out_dir="$script_dir"
lineage=""
staging=0
dry_run=0
force_renewal=0
reuse_key=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --domain)
            [[ $# -ge 2 ]] || die "--domain requires a value"
            domains+=("$2")
            shift 2
            ;;
        --ip)
            [[ $# -ge 2 ]] || die "--ip requires a value"
            ips+=("$2")
            shift 2
            ;;
        --email)
            [[ $# -ge 2 ]] || die "--email requires a value"
            email="$2"
            shift 2
            ;;
        --no-email)
            no_email=1
            shift
            ;;
        --challenge)
            [[ $# -ge 2 ]] || die "--challenge requires a value"
            challenge="$2"
            shift 2
            ;;
        --webroot)
            [[ $# -ge 2 ]] || die "--webroot requires a value"
            webroot="$2"
            shift 2
            ;;
        --http-port)
            [[ $# -ge 2 ]] || die "--http-port requires a value"
            http_port="$2"
            shift 2
            ;;
        --key-type)
            [[ $# -ge 2 ]] || die "--key-type requires a value"
            key_type="$2"
            shift 2
            ;;
        --curve)
            [[ $# -ge 2 ]] || die "--curve requires a value"
            curve="$2"
            shift 2
            ;;
        --rsa-bits)
            [[ $# -ge 2 ]] || die "--rsa-bits requires a value"
            rsa_bits="$2"
            shift 2
            ;;
        --cert-name)
            [[ $# -ge 2 ]] || die "--cert-name requires a value"
            cert_name="$2"
            shift 2
            ;;
        --config-dir)
            [[ $# -ge 2 ]] || die "--config-dir requires a value"
            config_dir="$2"
            shift 2
            ;;
        --work-dir)
            [[ $# -ge 2 ]] || die "--work-dir requires a value"
            work_dir="$2"
            shift 2
            ;;
        --logs-dir)
            [[ $# -ge 2 ]] || die "--logs-dir requires a value"
            logs_dir="$2"
            shift 2
            ;;
        --out-dir)
            [[ $# -ge 2 ]] || die "--out-dir requires a value"
            out_dir="$2"
            shift 2
            ;;
        --lineage)
            [[ $# -ge 2 ]] || die "--lineage requires a value"
            lineage="$2"
            shift 2
            ;;
        --staging)
            staging=1
            shift
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        --force-renewal)
            force_renewal=1
            shift
            ;;
        --reuse-key)
            reuse_key=1
            shift
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

case "$mode" in
    domain|public-ip|install-existing) ;;
    *) die "first argument must be 'domain', 'public-ip', or 'install-existing'" ;;
esac

require_cmd certbot
require_cmd od

config_dir="${config_dir:-$state_dir/config}"
work_dir="${work_dir:-$state_dir/work}"
logs_dir="${logs_dir:-$state_dir/logs}"

mkdir -p "$config_dir" "$work_dir" "$logs_dir" "$out_dir"

if [[ "$mode" == "install-existing" ]]; then
    [[ -n "$lineage" ]] || die "--lineage is required for install-existing"
else
    [[ "$key_type" == "ecdsa" || "$key_type" == "rsa" ]] || die "--key-type must be ecdsa or rsa"
    [[ "$rsa_bits" =~ ^[0-9]+$ ]] || die "--rsa-bits must be numeric"
    [[ "$http_port" =~ ^[0-9]+$ ]] || die "--http-port must be numeric"
    [[ -n "$email" || "$no_email" -eq 1 ]] || die "provide --email EMAIL or --no-email"
fi

certbot_args=(
    certonly
    --agree-tos
    --no-eff-email
    --config-dir "$config_dir"
    --work-dir "$work_dir"
    --logs-dir "$logs_dir"
)

if [[ "$mode" != "install-existing" ]]; then
    if [[ -n "$email" ]]; then
        certbot_args+=(--email "$email")
    else
        certbot_args+=(--register-unsafely-without-email)
    fi

    certbot_args+=(--key-type "$key_type")
    if [[ "$key_type" == "ecdsa" ]]; then
        certbot_args+=(--elliptic-curve "$curve")
    else
        certbot_args+=(--rsa-key-size "$rsa_bits")
    fi

    (( staging == 1 )) && certbot_args+=(--staging)
    (( dry_run == 1 )) && certbot_args+=(--dry-run)
    (( force_renewal == 1 )) && certbot_args+=(--force-renewal)
    (( reuse_key == 1 )) && certbot_args+=(--reuse-key)
fi

case "$mode" in
    domain)
        [[ "${#domains[@]}" -gt 0 ]] || die "--domain is required"
        challenge="${challenge:-dns}"
        [[ -n "$cert_name" ]] || cert_name="${domains[0]}"
        certbot_args+=(--cert-name "$cert_name")
        for domain in "${domains[@]}"; do
            certbot_args+=(-d "$domain")
        done

        case "$challenge" in
            dns)
                certbot_args+=(--manual --preferred-challenges dns)
                ;;
            webroot)
                [[ -n "$webroot" ]] || die "--webroot is required for --challenge webroot"
                certbot_args+=(--webroot --webroot-path "$webroot")
                ;;
            standalone)
                certbot_args+=(--standalone --preferred-challenges http --http-01-port "$http_port")
                ;;
            *)
                die "domain challenge must be dns, webroot, or standalone"
                ;;
        esac
        ;;
    public-ip)
        [[ "${#ips[@]}" -gt 0 ]] || die "--ip is required"
        challenge="${challenge:-webroot}"
        [[ -n "$cert_name" ]] || cert_name="${ips[0]}"
        certbot_args+=(--cert-name "$cert_name" --preferred-profile shortlived)
        for ip in "${ips[@]}"; do
            if is_private_ipv4 "$ip" || is_private_ipv6 "$ip"; then
                die "Refusing private IP $ip. Public CAs cannot validate or issue a trusted cert for 192.168/10/172.16-31/localhost/link-local LAN addresses."
            fi
            certbot_args+=(--ip-address "$ip")
        done

        case "$challenge" in
            webroot)
                [[ -n "$webroot" ]] || die "--webroot is required for --challenge webroot"
                certbot_args+=(--webroot --webroot-path "$webroot")
                ;;
            standalone)
                certbot_args+=(--standalone --preferred-challenges http --http-01-port "$http_port")
                ;;
            manual-http)
                certbot_args+=(--manual --preferred-challenges http)
                ;;
            *)
                die "public-ip challenge must be webroot, standalone, or manual-http"
                ;;
        esac
        ;;
esac

if [[ "$mode" != "install-existing" ]]; then
    echo "Running certbot..."
    certbot "${certbot_args[@]}"
    lineage="$config_dir/live/$cert_name"
fi

fullchain_pem="$lineage/fullchain.pem"
privkey_pem="$lineage/privkey.pem"

[[ -f "$fullchain_pem" ]] || die "fullchain not found: $fullchain_pem"
[[ -f "$privkey_pem" ]] || die "private key not found: $privkey_pem"

cert_pem="$out_dir/cert.pem"
key_pem="$out_dir/key.pem"
cert_h="$out_dir/cert.h"
key_h="$out_dir/key.h"

cp "$fullchain_pem" "$cert_pem"
cp "$privkey_pem" "$key_pem"
pem_to_header "$cert_pem" "$cert_h" cert_pem cert_pem_len
pem_to_header "$key_pem" "$key_h" key_pem key_pem_len

echo "Installed certbot certificate:"
echo "  lineage: $lineage"
echo "  $cert_pem"
echo "  $key_pem"
echo "  $cert_h"
echo "  $key_h"
