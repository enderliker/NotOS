# HTTPS / TLS 1.2 & 1.3 and X.509 Policy

## Kernel HTTPS Client Usage

```text
https example.com
https example.com:443
https localhost:8443 10.0.2.2
net
tls
tls-selftest
```

`https HOST[:PORT] [IP]` connects to port `443` by default. The DNS `HOST` name
is always used for **TLS Server Name Indication (SNI) and X.509 certificate
identity verification**. The optional `[IP]` argument overrides only the TCP
destination address (allowing local fixture testing against `10.0.2.2` while
verifying a DNS hostname). Literal IP identities / `iPAddress` SANs are not
supported, and there is no `--insecure` bypass or plaintext HTTP fallback.

The kernel client sends an `HTTP/1.0 GET /` request inside the encrypted tunnel,
reports the HTTP status line and authenticated plaintext byte count, and
requires an authenticated TLS `close_notify` alert before reporting success.

## Architecture & Cryptographic Policy

NotOS uses **MbedTLS 3.6.5** (`Apache-2.0`) for TLS 1.2, TLS 1.3, and X.509
chain validation, and **BearSSL 0.6** (`MIT`) for its `fw_cfg`-seeded
HMAC-DRBG/SHA-256 entropy generator. All TLS handshakes, record encryption, and
certificate checks run inside the NotOS guest—never via a host proxy.

- **Protocol Versions**: **TLS 1.3 preferred, TLS 1.2 minimum**. TLS 1.0/1.1,
  PSK, 0-RTT early data, and renegotiation are disabled.
- **Cipher Suites & Key Exchange**:
  - TLS 1.3: `AES-128-GCM`, `AES-256-GCM`, and `ChaCha20-Poly1305` with ECDHE
    (`secp256r1`, `secp384r1`, `x25519`) and `RSA-PSS` / `ECDSA` signatures.
  - TLS 1.2: `ECDHE-RSA` and `ECDHE-ECDSA` with `AES-128-GCM` or
    `ChaCha20-Poly1305` (no static RSA key exchange or CBC ciphers).
- **Certificate & Key Policy (`kernel/certificate_policy.c`)**:
  - RSA keys must be **>= 2048 bits** (2047-bit keys are rejected); EC keys
    support P-256, P-384, and P-521.
  - Certificate signatures must use **SHA-256, SHA-384, or SHA-512** (MD5 and
    SHA-1 peer certificate chains are rejected).
  - **DNS SAN required**: Peer certificates must contain a matching `dNSName`
    Subject Alternative Name (including single-label leftmost wildcards);
    CN-only fallback is rejected.
  - **Extended Key Usage (EKU)**: When present on leaf or intermediate
    certificates, EKU must permit `serverAuth` or `anyExtendedKeyUsage`.
  - Chain depth is bounded to 8 certificates of at most 16 KiB each, validated
    against the pinned Mozilla/curl root snapshot (`trust/roots.pem`).
- **Memory & Timeouts**: Per-connection state and 32 KiB I/O FIFOs are allocated
  from the kernel heap and explicitly zeroed (`mbedtls_platform_zeroize`) before
  freeing. Responses are bounded to 16 MiB, with a 30 s idle timeout and 60 s
  total transfer deadline.

## Entropy & Real-Time Clock (Fail-Closed)

- **Entropy (`kernel/entropy.c`)**: `tools/run.py` generates 64 fresh bytes from
  the host CSPRNG on each launch and passes them via QEMU `fw_cfg`
  (`opt/notos/entropy`). The kernel seeds BearSSL HMAC-DRBG/SHA-256 and wipes
  temporary buffers. If no seed is present, TLS and `SYS_ENTROPY` **fail closed
  before connecting**.
- **CMOS RTC UTC (`kernel/rtc.c`, `kernel/utc.c`)**: Reads coherent CMOS RTC
  snapshots (supporting BCD/binary and 12/24-hour modes for UTC years
  `2000..2099`) and converts them to 64-bit Unix epoch seconds (`tests/utc_unit.c`
  verifies post-2038 dates). Missing or invalid RTC data fails closed.

## User-Space `curl` HTTPS (`ring 3`)

Upstream `curl` 8.11.1 links against a separate user-space build of MbedTLS
3.6.5 (`user/curl_port/`) using Picolibc, `SYS_ENTROPY`, and `SYS_TIME`. It
reads the immutable default CA bundle from `/system/cacert.pem` (or a custom
bundle via `--cacert PATH`) and enforces the same strict X.509 policy
(`certificate_policy.c`), preventing `-k` / `--insecure` from bypassing
certificate verification. See [CURL_PORT.md](CURL_PORT.md).

## Verification

- `tests/cert_unit.c` and `tests/utc_unit.c` (ASAN/UBSAN): Test DER X.509
  policy parsing across all truncated prefixes and 10,000 mutations, plus 64-bit
  UTC calendar math.
- `tests/https.py` and `tests/curl_https.py`: Test TLS 1.2 and TLS 1.3 against
  ephemeral OpenSSL fixtures (`notos-tls-test.img` for the kernel client;
  `--cacert` in `tmpfs` for `curl`), verifying rejection of untrusted roots,
  wrong SAN/wildcards, missing SAN, bad EKU, weak RSA (`1024`/`2047`), SHA-1
  leaf signatures, expired/future dates, missing `close_notify`, and tampered
  AEAD records.
- `make test-online`: Verifies live TLS 1.3 handshakes, X.509 validation, HTTP
  200 responses, and authenticated `close_notify` against `https://example.com`.
