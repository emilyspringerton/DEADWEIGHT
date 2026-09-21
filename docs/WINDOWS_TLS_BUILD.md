# Windows TLS build (mbedTLS cross-compile) — S508f

`dw_gui.exe`'s real TLS support (see `core/http.c`, PARENA's `stdlib/net/tls.prn`) needs mbedTLS
built for the `x86_64-w64-mingw32` target. There is no `mingw-w64` mbedTLS package in apt — it has
to be cross-compiled from source once, the same way `SDL2_MINGW` already has to be vendored
locally for the Windows GUI cross-build in general.

## Status — honest, not glossed over

Structurally verified: `dw_gui.exe` cross-compiles clean with `PARENA_WITH_TLS` +
`PARENA_TLS_CA_BUNDLE_EMBEDDED`, links against the real mbedTLS static libs with zero undefined
symbols, produces a valid `PE32+` binary, and the embedded CA bundle really is in the binary (146
real `-----BEGIN CERTIFICATE-----` markers confirmed via `strings`).

**Linux runtime-verified (S519, 2026-09-21)**: the Linux `dw_gui` build with real TLS
(`MBEDTLS_CFLAGS`/`MBEDTLS_LDFLAGS` pointed at a locally-built libmbedtls-dev, statically linked)
was actually run headless (`SDL_VIDEODRIVER=dummy ./build/dw_gui --iduna-url https://okemily.com
--autostart`) against real production IDUNA: a live TLS 1.2 handshake, cert verification, and a
real guest-register all completed, account file written with a real player_id/secret. This also
uncovered the actual root cause of a "still says IDUNA offline after downloading" report: CI had
**never** built any release artifact (Linux or Windows) with `PARENA_WITH_TLS` at all — no
`libmbedtls-dev` install, no PARENA sibling checkout — so every shipped build failed clean at
connect time against the real `https://` default, 100% of the time, for every player. Fixed in
`.github/workflows/ci.yml`'s `gui` job (PARENA checked out public/no-token-needed as a sibling,
`libmbedtls-dev` installed for Linux, mbedTLS cross-compiled from source for the mingw/Windows
target on every CI run).

**Windows itself still not runtime-verified**: this sandbox has no Wine/Windows environment to
actually execute the `.exe`. The underlying mbedTLS Windows/winsock2 networking layer is mature
and widely used and the Linux build (same source, same mbedTLS version) is now proven live, so the
real risk here is low, but someone should still run the actual CI-built `.exe` on real Windows and
confirm a live handshake before treating it as equivalent to the Linux build's own verification.

## One-time setup: cross-compile mbedTLS for mingw

```bash
# 1. Get the exact same mbedTLS version the Linux build already uses (2.28.8, matching Ubuntu's
#    libmbedtls-dev package) -- source, not the Debian binary package, since that's Linux-only.
mkdir -p /tmp/mbedtls_src && cd /tmp/mbedtls_src
curl -sL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/mbedtls-2.28.8.tar.gz -o mbedtls.tar.gz
tar xzf mbedtls.tar.gz

# 2. mbedTLS ships a plain Makefile (no CMake needed) that supports CC/AR overrides --
#    cross-compile directly with the mingw toolchain (binutils-mingw-w64-x86-64 /
#    g++-mingw-w64-x86-64, already installed on this box).
cd mbedtls-mbedtls-2.28.8
make CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar WINDOWS=1 WINDOWS_BUILD=1 lib

# 3. Stage headers + the three real static libs (libmbedtls.a, libmbedx509.a, libmbedcrypto.a)
#    into one directory -- this is what MBEDTLS_MINGW_DIR points scripts/build.sh at.
mkdir -p /path/to/mbedtls-mingw/library
cp -r include /path/to/mbedtls-mingw/
cp library/*.a /path/to/mbedtls-mingw/library/
```

Verify: `file /path/to/mbedtls-mingw/library/libmbedtls.a` should say `current ar archive`, and
`x86_64-w64-mingw32-objdump -f library/ssl_tls.o` (inside the mbedTLS source tree, before you rm
it) should say `file format pe-x86-64`.

## Regenerating the embedded CA bundle

`core/runtime/ca_bundle_data.h` is checked in (generated, not hand-edited — same convention
`core/card_rules.c` already establishes). Regenerate it periodically as CA bundles rotate, not on
every build:

```bash
scripts/gen_ca_bundle.sh   # reads /etc/ssl/certs/ca-certificates.crt by default
```

## Building

```bash
SDL2_MINGW=/path/to/sdl2-mingw MBEDTLS_MINGW_DIR=/path/to/mbedtls-mingw \
  bash scripts/build.sh --gui --windows
```

Both `SDL2_MINGW` and `MBEDTLS_MINGW_DIR` are auto-detected-if-present, not hard-required —
`scripts/build.sh` builds `dw_gui.exe` without TLS (and prints a clear warning) if
`MBEDTLS_MINGW_DIR` isn't set or doesn't contain real libs, same "fail clean, never fall back to
plaintext" contract the runtime itself enforces for a `use_tls=1` call on a non-TLS build.
