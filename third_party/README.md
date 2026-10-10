# Dependencies

BearSSL 0.6, Thomas Pornin, MIT license (see BearSSL-LICENSE.txt).
Source: https://bearssl.org/bearssl-0.6.tar.gz
SHA256: 6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14

Fetched only on the development host, hash checked before extraction. Cache `.cache/bearssl-0.6.tar.gz` survives `make clean`. For offline builds set `BEARSSL_ARCHIVE=/path/to/verified/archive`. Upstream sources are unchanged; intrinsic-only files and the upstream OS seeder are excluded. Kernel provides an explicit no-system-seeder implementation. OS time, OS RNG, SIMD/AES-NI/RDRAND/POWER8 are disabled. Portable crypto is built for i486 with no guest libc or Linux runtime.

The trust snapshot `trust/roots.pem` was downloaded from https://curl.se/ca/cacert.pem.
SHA256: a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505
It derives from Mozilla's certificate store (MPL 2.0 data license: https://www.mozilla.org/MPL/2.0/), retaining the original snapshot headers. The standard-library generator embeds full DER certificates into a build header. Trust changes require reviewing and committing a new snapshot; there is no silent runtime update.

## TLS 1.2 / TLS 1.3 backend

MbedTLS 3.6.5, Mbed TLS Contributors. We choose Apache-2.0 under the upstream dual license; see MbedTLS-LICENSE.txt.
Source: https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2
SHA256: 4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8

Verified before safe extraction; upstream source unchanged. Offline: MBEDTLS_ARCHIVE=/path/to/archive.
Build uses a custom config (kernel/notos_mbedtls_config.h), no host system headers, OS sockets/RNG/time/filesystem, SIMD, AESNI or FPU.
Platform allocator, RTC UTC and external PSA entropy hooks are explicit. BearSSL is retained only for entropy DRBG/SHA256 and its known-answer test, not TLS negotiation.
SHA1 is compiled to recognize historical anchor signature OIDs; the verification profile and advertised signature algorithms exclude SHA1 chains.
The small freestanding headers are kernel crypto adapters, not a ported libc and not sufficient for curl.
