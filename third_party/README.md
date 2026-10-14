# Third-Party Dependencies

All third-party archives are fetched on the development host, verified against
pinned SHA-256 digests before safe extraction, and cached in `.cache/` across
`make clean`. Upstream sources, versions, and hashes are never modified.

## Entropy DRBG & SHA-256 (BearSSL)

- **Package**: BearSSL 0.6 (Thomas Pornin, MIT license — see
  `BearSSL-LICENSE.txt`)
- **Source**: `https://bearssl.org/bearssl-0.6.tar.gz`
- **SHA-256**: `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`
- **Offline override**: `BEARSSL_ARCHIVE=/path/to/bearssl-0.6.tar.gz`

Intrinsic-only files and the upstream OS seeder are excluded. The kernel
provides an explicit `fw_cfg`-seeded HMAC-DRBG/SHA-256 implementation without OS
time, OS RNG, SIMD, AES-NI, RDRAND, or POWER8 intrinsics, compiled as portable
freestanding `i486` code.

## Root CA Trust Store Snapshot

- **File**: `trust/roots.pem` (sourced from `https://curl.se/ca/cacert.pem`)
- **SHA-256**: `a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`
- **License**: Mozilla CA Certificate Store (`MPL-2.0` data license:
  `https://www.mozilla.org/MPL/2.0/`)

Original snapshot headers are preserved. During the build, `tools/gen_trust_anchors.py`
parses and embeds full DER certificates for the kernel TLS client, and the raw
PEM bundle is also exposed as `/system/cacert.pem` for user-space `curl`. Trust
updates require reviewing and committing a new snapshot; there are no silent
runtime updates.

## TLS 1.2 / TLS 1.3 & X.509 Backend (MbedTLS)

- **Package**: MbedTLS 3.6.5 (Mbed TLS Contributors, `Apache-2.0` selected under
  upstream dual license — see `MbedTLS-LICENSE.txt`)
- **Source**: `https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2`
- **SHA-256**: `4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8`
- **Offline override**: `MBEDTLS_ARCHIVE=/path/to/mbedtls-3.6.5.tar.bz2`

MbedTLS is built twice from the same verified upstream source tree:
1. **Kernel TLS backend**: Configured via `kernel/notos_mbedtls_config.h` with
   freestanding adapter headers (`kernel/tls_platform/`), explicit `kmalloc`/RTC
   UTC/PSA entropy hooks, and no host headers, OS sockets, SIMD, AES-NI, or FPU.
2. **User-space `curl` TLS backend**: Configured via
   `user/curl_port/notos_user_mbedtls_config.h` and compiled against the NotOS
   Picolibc sysroot (`tools/build_user_mbedtls.py`), using `SYS_ENTROPY` and
   `SYS_TIME` without FPU/SSE instructions.

SHA-1 parsing is enabled solely to recognize legacy root anchor signature OIDs;
the verification profile and advertised signature algorithms strictly reject
SHA-1 peer certificate chains.

## User-Space C Library (Picolibc)

- **Package**: Picolibc 1.8.11 (Keith Packard / The Newlib Project and per-file
  contributors — see `Picolibc-COPYING.txt`)
- **Source**: `https://github.com/picolibc/picolibc/releases/download/1.8.11/picolibc-1.8.11.tar.xz`
- **SHA-256**: `b4671ddeecbe427b77fa9a863be92c74a9059e0373a5b31343c791ff2337db64`
- **Offline override**: `PICOLIBC_ARCHIVE=/path/to/picolibc-1.8.11.tar.xz`

Selected libc sources use their upstream BSD/MIT/Newlib per-file notices (see
`Picolibc-COPYING.txt`). The upstream tarball also contains an AGPL-3 helper
script (`scripts/GeneratePicolibcCrossFile.sh`), which NotOS neither runs nor
incorporates; `tools/build_picolibc.py` generates its own Meson cross file.

Meson builds a freestanding `i486` static library (`libc.a`) installed into
`build/sysroot` with integer/long-long `stdio` and without host headers,
semihosting, multilib, `picocrt`, TLS, or thread locks. `user/libc/` provides
the NotOS CRT (`crt0.S`, `start.c`) and explicit syscall adapters for console,
VFS, sockets, `poll`/`select`, `sbrk`, process exit/exec, time, sleep, and
entropy. See [docs/LIBC.md](../docs/LIBC.md).

## User-Space TCP/IPv4 & DNS Stack (lwIP)

- **Package**: lwIP 2.2.1 (Swedish Institute of Computer Science and
  contributors, `BSD-3-Clause` — see `lwIP-LICENSE.txt`)
- **Source**: `https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE`
- **SHA-256**: `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`
- **Offline override**: `LWIP_ARCHIVE=/path/to/lwip-2.2.1.tar.gz`

Compiled in raw `NO_SYS=1` mode with IPv4, TCP, DNS, ARP, and ICMP (`tools/build_lwip.py`),
without lwIP socket/netconn/thread layers or host headers. The kernel owns
`sys_now`, CSPRNG entropy, RTL8139 frame transmission/demux, descriptor
reference counting, and process blocking. Production images use UDP port 53 for
DNS; the dedicated test fixture image (`build/notos-socket-test.img`) uses UDP
port 53530 so unprivileged host tests can run a local DNS fixture. See
[docs/SOCKETS.md](../docs/SOCKETS.md).

## User-Space HTTP/HTTPS Client (curl)

- **Package**: curl 8.11.1 (Daniel Stenberg and contributors, `curl` license —
  see `curl-COPYING.txt`)
- **Source**: `https://curl.se/download/curl-8.11.1.tar.xz`
- **SHA-256**: `c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56`
- **Offline override**: `CURL_ARCHIVE=/path/to/curl-8.11.1.tar.xz`

`tools/build_curl.py` compiles `libcurl` and the `curl` CLI tool against the
NotOS Picolibc sysroot, user-space MbedTLS 3.6.5, and software integer/`binary64`
runtime helpers (`user/curl_port/`), producing a static ring 3 ELF binary
embedded in `/system/curl.elf`. See [docs/CURL_PORT.md](../docs/CURL_PORT.md).
