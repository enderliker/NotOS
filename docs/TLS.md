# TLS integration

## Platform and policy

BearSSL 0.6 (MIT) supplies cryptography/X.509; NotOS retains its own kernel, Ethernet, IP and TCP. No host proxy or host TLS client is used by the guest. The build fetches an archive pinned by SHA256; see third_party/README.md and the offline archive option.

TLS policy: 1.2 only, ECDHE_RSA/ECDHE_ECDSA, ChaCha20-Poly1305 or AES-128-GCM, RSA minimum 2048 bits and SHA256/384/512 certificate signatures. No TLS1.0/1.1, CBC, static RSA key exchange, SHA1/MD5 chains or verification bypass. No TLS1.3, RSA-PSS, client certificates, revocation/OCSP/CT or session resumption.

Committed Mozilla/curl roots are converted reproducibly from PEM. X.509 verifies the DNS identity supplied to the handshake and the chain against these roots. Time is read from coherent CMOS snapshots in UTC, supporting 2000..2099; invalid/missing clock fails closed. Host firmware/RTC are part of the trust boundary, not authenticated time.

`make run` generates 64 fresh bytes from the host CSPRNG and supplies `opt/notos/entropy` through QEMU fw_cfg. The guest seeds HMAC-DRBG/SHA256, wipes seed temporaries and derives separate handshake entropy for each attempt. No PIT jitter, fixed seed, Linux RNG call or implicit CPU RNG fallback. Without a seed TLS is unavailable, but plain HTTP remains available. Seeds must not be reused; restoring/cloning VM snapshots can repeat DRBG state and is not supported as a secure workflow. There is no entropy implementation yet for physical machines.

TLS state/record buffers are allocated per request from the kernel heap and wiped/freed on completion/error. Memory primitives are freestanding. The binary loader now reads 192 KiB; BSS and readonly/payload limits remain linker checked.

`tls` reports entropy/clock/root availability; `tls-selftest` verifies SHA256 known answer, TLS12-only profile, clock and heap cleanup. Current platform preparation is tested, but HTTPS command/transport integration is a subsequent advance.
