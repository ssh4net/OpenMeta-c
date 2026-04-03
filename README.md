# OpenMeta-c

OpenMeta-c is a native C port of OpenMeta.

It keeps the same general metadata model and safety posture as the C++ library,
but the public API is C-native: flat, explicit, caller-buffer-oriented, and
designed around bounded read, edit, and writeback steps instead of C++ object
lifetimes.

## Status

The C port is already useful for real metadata reads and bounded XMP writeback,
but it is not full C++ parity yet.

Current planning estimate:

| Milestone | Status |
| --- | --- |
| Read-only core | About `80-85%` |
| Overall public surface | About `60-65%` |

In practice:
- Read-path coverage is broad and regression-tested.
- Optional parity tests compare selected shared cases against the C++ library.
- XMP edit and writeback are real for bounded target families.
- `EXR`, the full transfer stack, and real C2PA crypto verification are still
  missing.

## What OpenMeta-c Does

- Scan containers to find metadata blocks in `jpeg`, `png`, `webp`, `gif`,
  `tiff/bigtiff`, `crw/ciff`, `raf`, `x3f`, `jp2`, `jxl`, and
  `heif/avif/cr3` (ISO-BMFF).
- Reassemble multipart carriers and optionally decompress supported payloads.
- Decode metadata into a normalized `omc_store`.
- Apply bounded in-memory edit batches with `omc_edit`.
- Dump XMP sidecars from decoded metadata and edited stores.
- Build embedded XMP payloads and rewrite embedded XMP for bounded container
  families.
- Project bounded JUMBF / C2PA semantic fields, including non-crypto verify
  scaffolding.

## Metadata Families

OpenMeta-c currently covers these major families:

- EXIF, including pointer IFDs and broad MakerNote support.
- Canon CRW / CIFF, including bounded native CIFF naming and projection.
- XMP as RDF/XML properties.
- ICC profile header and tag table.
- IPTC-IIM datasets.
- Photoshop IRB, with raw preservation plus an interpreted subset.
- JPEG comments, GIF comments, and PNG text chunks.
- GeoTIFF and PrintIM.
- ISO-BMFF derived fields for primary-item, relation, and auxiliary semantics.
- JUMBF / C2PA draft structural and semantic projection.

Not yet ported:
- EXR header metadata.
- Full transfer / metadata synchronization parity with the C++ library.
- Real C2PA signature verification backend.

## Naming Model

EXIF and MakerNote names have two layers:

- Stable canonical names from decode.
- Contextual compatibility names through `omc_exif_name`, for cases where
  ExifTool-style or vendor-specific display naming matters.

That split keeps the stored keys stable while still exposing compatibility
names when needed.

## Edit and XMP Writeback

OpenMeta-c already has a real bounded edit and XMP writeback core:

- `omc_edit_*` for append, replace, tombstone, commit, and compact.
- `omc_xmp_dump_*` for portable and lossless sidecar generation.
- `omc_xmp_embed_*` for raw embedded XMP payload generation.
- `omc_xmp_write_embedded(...)` for embedded XMP rewrite.
- `omc_xmp_apply(...)` for higher-level in-memory XMP writeback policy.

Current embedded XMP writeback targets:

| Target | Status |
| --- | --- |
| JPEG | Real |
| PNG | Real |
| TIFF / BigTIFF | Real |
| WebP | Real |
| JP2 | Real |
| JXL | Real |
| HEIF / AVIF | Real |
| CR3 | Explicitly unsupported |

Current policy surface:
- `embedded_only`
- `sidecar_only`
- `embedded_and_sidecar`
- preserve existing embedded XMP
- strip existing embedded XMP

This is still narrower than the C++ transfer layer. OpenMeta-c does not yet
provide the full prepared-transfer / file-persistence stack.

## Layout

- `src/omc/`: public headers
- `src/read/`: scan, payload, decode, and naming logic
- `src/edit/`: XMP dump, embed, write, and apply logic
- `src/core/`: store, key/value, and edit core
- `src/base/`: low-level arena support
- `tests/`: unit tests and optional parity tests

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run tests with:

```bash
ctest --test-dir build --output-on-failure
```

Useful options:
- `-DOMC_BUILD_STATIC=ON|OFF`
- `-DOMC_BUILD_SHARED=ON|OFF`
- `-DOMC_BUILD_TESTS=ON|OFF`
- `-DOMC_BUILD_PARITY_TESTS=ON` for optional parity tests against the C++
  OpenMeta build
- `-DOMC_WARNINGS_AS_ERRORS=ON`
- `-DOMC_USE_ZLIB=ON|OFF`
- `-DOMC_USE_BROTLI=ON|OFF`
- `-DOMC_OPENMETA_DIR=/path/to/OpenMeta/build` for parity tests

## Portability

The core code is written in a conservative C89/C90 style, with one explicit
portability shim for 64-bit integers because offsets and sizes require it.

The current validation baseline is:
- Clang on 64-bit Linux
- GCC in strict C90 mode
- Clang `-m32`
- GCC `-m32`

## Quick Usage

The main read entry point is `omc_read_simple(...)`.

- Input: whole-file bytes
- Output: normalized `omc_store` plus discovered blocks
- Scratch: caller-provided buffers for block refs, payload assembly, and
  format-specific decode state

For writeback:
- use `omc_edit_*` to adjust the store
- use `omc_xmp_dump_*` for sidecars
- use `omc_xmp_write_embedded(...)` for embedded XMP rewrite
- use `omc_xmp_apply(...)` for higher-level embedded/sidecar policy

## Current Gaps

The largest remaining gaps relative to the C++ library are:

- `EXR` read support
- the full transfer / write pipeline beyond bounded XMP writeback
- file-persistence helpers around the new writeback policy layer
- real C2PA verification backends
- broader parity work for newer upstream features
