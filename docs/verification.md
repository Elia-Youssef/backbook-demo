# Verification

This checklist is the evidence required before creating `v0.1.0-demo`. Run it
from a clean checkout of the candidate commit. Do not create the tag until every
row passes and the public workflow reports a successful `quality-gate`.

## Compiler and runtime matrix

| Surface | Required toolchain | Current verified result |
| --- | --- | --- |
| Native Windows | MSVC 19.51 (14.51 toolset), CMake 3.24+, Ninja | 191/191 tests |
| Native Linux | GCC 13.3.0, CMake 3.24+, Ninja | 191/191 tests |
| Native sanitizers | Clang 18.1.3, ASan, UBSan, CMake 3.24+, Ninja | 191/191 tests |
| Frontend | Node.js 24, npm from the committed lockfile | Typecheck, 16/16 tests, production build |

## Windows

Run from Visual Studio Developer PowerShell:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure --timeout 30
```

## Linux

Build inside a Linux filesystem:

```bash
cmake --preset gcc-release
cmake --build --preset gcc-release --parallel
ctest --preset gcc-release --output-on-failure --timeout 30
```

Confirm the compiler major version:

```bash
g++ -dumpfullversion -dumpversion
```

## Frontend

```bash
cd web
npm ci
npm run typecheck
npm test
npm run build
git diff --exit-code -- dist
```

The final command proves that production assets reproduce from source and the
committed lockfile. Confirm that `web/dist` contains no source maps.

## Canonical demo

Start the candidate executable:

```text
backbook-server --demo --port 0
```

The startup report must show:

```text
Accepted commands: 7
Expected rejections: 1
State fingerprint: 0x21bd5cac4ef6e98d
```

In a browser, verify that:

- the connection state becomes `LIVE`;
- state version is `7` and the abbreviated fingerprint matches the startup
  report;
- the ledger strip reports zero for USD, JPY, and KWD with the correct currency
  exponents;
- `TRD-1001` version 1 is superseded by version 2;
- selecting version 1 shows its original and reversal postings, while selecting
  version 2 shows the replacement postings;
- `TRD-1002` remains captured after its rejected confirmation;
- the settlement table contains one incoming and one outgoing obligation.

Also verify that `GET /api/v1/state` reports `BOOK-FX-1` USD remaining minor
units as `4875000`.

## Flagship invariant evidence

The suite must directly cover:

1. rejection of a ledger entry that is unbalanced in any currency;
2. atomic reversal-plus-rebook with the original trade version retained;
3. identical state, ledger, journal, and headroom after a rejected limit check;
4. the canonical fingerprint before and after journal replay;
5. exactly one successful confirmation when two threads race for limited
   headroom.

Also retain coverage for checked money overflow, exhaustive lifecycle
transitions, duplicate posting IDs, idempotent replay, conflicting command IDs,
torn-tail recovery, fatal CRC corruption, storage failure, HTTP status and
content types, 64 KiB request limits, runtime frontend decoders, and committed
static delivery without source maps.

## Repository release audit

Before committing, pushing, or tagging:

```bash
git status --short
git diff --check
git diff --cached --check
git diff --cached --name-status
```

Review every staged path and staged line. Exclude build output, journals, logs,
environment files, editor state, dependency directories, caches, credentials,
and local-only material. Confirm that the candidate contains only intended
public project files.

After the branch is published, wait for the required `quality-gate` on the exact
candidate commit. Create the annotated tag only from that verified commit. The
tag message should identify the submitted demo state and record the native test
count and compiler matrix.

## Post-release sanitizer verification

Subsequent native changes also run the complete suite on Ubuntu 24.04 with
Clang 18, AddressSanitizer, and UndefinedBehaviorSanitizer:

```bash
sudo apt-get install --no-install-recommends clang-18 libclang-rt-18-dev llvm-18
export ASAN_SYMBOLIZER_PATH="$(command -v llvm-symbolizer-18)"
cmake --preset clang-sanitizers
cmake --build --preset clang-sanitizers --parallel
ctest --preset clang-sanitizers --output-on-failure --timeout 30
```

The preset enables leak detection, fail-fast sanitizer behavior, stack traces,
and frame pointers. The CI `quality-gate` requires this sanitizer job alongside
the MSVC, GCC 13, and frontend jobs.
