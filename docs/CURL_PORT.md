# Upstream `curl` in NotOS

## Scope

Upstream **curl 8.11.1** is compiled as a static 32-bit ELF (`ET_EXEC`) and runs
in **ring 3** with Picolibc 1.8.11, a private PAE address space, heap, and user
stack. It uses NotOS user-space TCP/IPv4 sockets and DNS over RTL8139/lwIP, and
multiplexes I/O with `poll` / `select`.

The port supports both **HTTP** and **HTTPS** using a dedicated user-space build
of **MbedTLS 3.6.5** linked against Picolibc. By default, `curl` trusts the
immutable guest CA bundle at `/system/cacert.pem` (generated from
`trust/roots.pem`); `--cacert PATH` selects an alternate bundle in the guest
VFS. TLS 1.2 and TLS 1.3 enforce certificate chain validation, expiration dates,
DNS SAN matching, `serverAuth` EKU, SHA-256/384/512 signatures, and RSA keys of
at least 2048 bits. Entropy comes from `SYS_ENTROPY` (`fw_cfg`-seeded
HMAC-DRBG); missing entropy or an invalid RTC clock fails closed before
connecting.

## Usage

```text
curl --version
curl -sS https://example.com/
curl -sS -o /tmp/page.txt http://example.com/
curl -sS --max-time 1 http://10.0.2.2:8080/
ps
kill PID
```

The shell remains interactive while `curl` runs as a background process. By
default, only `curl`'s standard output/error is printed, suppressing kernel PID
spawn/reap banners. Run `process-trace on` to enable kernel lifecycle banners or
`process-trace off` to hide them; `ps` lists active processes at any time.
Option errors, HTTP errors with `-f`, DNS failures, refused connections, and
timeouts preserve upstream `curl` exit codes.

The `curl` binary executes entirely inside the NotOS guest using NotOS syscalls,
Picolibc, MbedTLS, and lwIP—never host `curl`, host libc, or host sockets. QEMU
provides the emulated RTL8139 NIC and user-mode NAT.

Arguments are split on spaces (consecutive spaces are collapsed), up to **256
bytes** after `curl ` and at most **16 `argv` elements** including `argv[0]` (15
user arguments). Quotes and backslashes are treated as literal characters; there
is no POSIX shell quoting, escaping, or variable expansion. Overlong argument
lines or excessive argument counts are rejected without executing a truncated
command.

Downloaded files can be inspected and managed with `ls`, `stat`, `cat`, `rm`,
`cd`, and `pwd`. `curl` starts with `cwd` set to `/` and resolves relative paths
against its current working directory. Files in `/tmp` live in RAM until
unlinked or rebooted; files written to `/disk` persist on the NotFS v2 data
disk (see [STORAGE.md](STORAGE.md)).

## Build Integration

- `tools/fetch_curl.py` downloads `curl-8.11.1.tar.xz` and verifies its SHA-256
  digest (`c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56`).
  For offline builds: `CURL_ARCHIVE=/path/to/curl-8.11.1.tar.xz make`.
- `tools/build_user_mbedtls.py` builds MbedTLS 3.6.5 for ring 3 against the
  Picolibc sysroot.
- `tools/build_curl.py` compiles `libcurl` and the `curl` CLI tool using
  `user/curl_port/curl_config.h` (HTTP/HTTPS, IPv4, `poll`/`select`, no threads).
- The link step combines `libcurl.a`, user-space MbedTLS archives, sysroot
  `libc.a`, and NotOS integer/`binary64` software helpers (`integer_runtime.c`,
  `float_runtime.c`), without host libc, `libgcc`, semihosting, or FPU/MMX/SSE
  instructions.
- The stripped ELF binary is embedded as a read-only/NX payload in high physical
  memory (`0x100000..0x1c0000`), loaded by the BIOS bootloader via 32 KiB bounce
  copies and exposed in the VFS as `/system/curl.elf` (see
  [MEMORY.md](MEMORY.md)).

First-party code compiles with `-Wall -Wextra -Werror`. Upstream `curl` sources
compile with `-Wall -Wextra` under the third-party warning policy without
modifying upstream files (GCC 15.2 emits enum/bool conversion warnings in
`transfer.c` and an unused-function warning in `url.c`).

## Architectural Limits

- Up to 4 managed processes, 8 TCP endpoints, and 16 FDs per process.
- `tmpfs` supports up to 64 KiB per file (256 KiB global RAM quota); `NotFS v2`
  supports up to 60 KiB total payload across 24 nodes. Real directories and
  `--create-dirs` are supported (see [VFS.md](VFS.md)).
- Only HTTP/1.x and HTTPS (TLS 1.2 / 1.3) over IPv4 are enabled; no HTTP/2,
  HTTP/3, proxies, cookies, MIME/multipart, FTP, or other protocols.
- **Strict TLS verification**: `-k` / `--insecure` does not bypass certificate
  validation. No client certificates, `CApath` directory hashing, or OCSP/CRL
  revocation checks. CA bundle parsing is bounded to 256 KiB and peer chains to
  8 certificates of 16 KiB each.
- Unauthenticated TLS EOF (missing `close_notify`) fails transfers that rely on
  EOF framing. Responses with a satisfied `Content-Length` may complete once all
  declared body bytes are received.
- No `fork`, threads, signals, or full POSIX environment.
- Asynchronous DNS resolution in `getaddrinfo` enforces its own 8-second
  application deadline; `--max-time` is not guaranteed to preempt an in-flight
  DNS query mid-sleep.

## Verification

- `tests/curl.py` (`make test`): Verifies `curl --version`, empty/overlong
  arguments, HTTP GET, 32,768-byte binary download to `tmpfs`, cross-process
  verification/unlink from another ELF, 32,768-byte binary POST upload, HTTP 404
  with `-f`, refused HTTP/HTTPS connections, `--max-time` timeout, concurrent
  shell/`INT3` responsiveness, 4 concurrent `curl` processes, `kill`
  cancellation, and DNS `A`/`NXDOMAIN` against the local UDP 53530 fixture image.
- `tests/curl_https.py` (`make test`): Verifies TLS 1.2 and TLS 1.3 downloads
  and uploads in 16 MiB and 64 MiB RAM using an ephemeral fixture CA downloaded
  to `/tmp`, rejection by the default `/system/cacert.pem` bundle, `-k`
  rejection, invalid SAN/EKU/key-size/signature/expired certificates, and
  truncated TLS EOF without `close_notify`.
- `tests/continuation.py` (`make test`): Verifies downloading a real ELF binary
  via `curl` into `/tmp` and `/disk` and executing it across a reboot.
- `tests/curl_online.py` (`make test-online`): Verifies live public DNS, HTTP,
  and HTTPS transfers from `example.com` into `/tmp` with full resource
  restoration.
