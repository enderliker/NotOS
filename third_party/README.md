# Dependencies

BearSSL 0.6, Thomas Pornin, MIT license (see BearSSL-LICENSE.txt).
Source: https://bearssl.org/bearssl-0.6.tar.gz
SHA256: 6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14

Fetched only on the development host, hash checked before extraction. Cache `.cache/bearssl-0.6.tar.gz` survives `make clean`. For offline builds set `BEARSSL_ARCHIVE=/path/to/verified/archive`. Upstream sources are unchanged; intrinsic-only files and the upstream OS seeder are excluded. Kernel provides an explicit no-system-seeder implementation. OS time, OS RNG, SIMD/AES-NI/RDRAND/POWER8 are disabled. Portable crypto is built for i486 with no guest libc or Linux runtime.

The trust snapshot `trust/roots.pem` was downloaded from https://curl.se/ca/cacert.pem.
SHA256: a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505
It derives from Mozilla's certificate store (MPL 2.0 data license: https://www.mozilla.org/MPL/2.0/), retaining the original snapshot headers. The standard-library DER generator deterministically extracts subjects/public keys into a build header. Trust changes require reviewing and committing a new snapshot; there is no silent runtime update.
