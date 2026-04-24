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
- `EXR` read is implemented.
- Bounded `DNG` / `CCM` field query and validation are now implemented.
- Minimal-fresh-scaffold `DNG` transfer can now emit a real fresh `.dng`
  without target bytes for embedded and embedded+sidecar flows.
- The bounded transfer lane now has direct ICC carrier rewrite for `JPEG`
  `APP2`, `PNG` / `WebP` `iCCP` / `ICCP`, `JP2` `colr`, and `TIFF` / `DNG`
  tag `34675`.
- The TIFF-family bounded transfer lane now also has direct IPTC-IIM carriage
  via tag `33723` for `TIFF`, `BigTIFF`, and `DNG`, including the minimal
  fresh-scaffold `DNG` path.
- The bounded `JXL` EXIF rewrite path now replaces both direct `Exif` boxes
  and compressed `brob(Exif)` carriers, instead of leaving duplicate EXIF in
  place. Preserving target-only compressed EXIF fields still depends on a
  Brotli-enabled build.
- A stable serialized `JXL` encoder ICC handoff API now exists for bounded
  host/encoder bridging without porting the C++ adapter layer, including a
  zero-copy parse path for persisted handoff bytes.
- A first serialized transfer-payload batch API now exists in C for bounded
  `EXIF` / `XMP` / `ICC` / `IPTC` carrier bridging plus projected non-`C2PA`
  `JUMBF` export for `JPEG`, `JXL`, and bounded BMFF targets, using the public
  `OMTPLD01` payload-batch family on the wire while staying narrower than the
  C++ prepared-bundle surface.
- A first serialized transfer-package batch API now also exists in C for
  bounded carrier-chunk handoff via the public `OMTPKG01` family, turning the
  current payload bridge into replayable `JPEG` segments plus inline or
  target-typed transfer blocks for the current non-`JPEG` carriers. The wire
  format now matches the public C++ package-batch contract, but the current C
  builder still materializes only the bounded subset it can honestly emit from
  the current C transfer lane, so this is not full executed package-batch
  parity yet.
- A generic persisted-artifact inspect API now exists in C for serialized
  transfer payload batches, transfer package batches, and `JXL` encoder
  handoffs.
- The `JPEG` lane now also has direct bounded `APP13` IPTC-IIM carriage via
  Photoshop IRB `0x0404`, while preserving unrelated IRB resources.
- Prepared transfer / persist APIs exist for bounded XMP-centric targets, but
  they are still narrower and less stable than the C++ transfer layer.
- Real C2PA crypto verification and the C++ host-adapter surfaces are still
  missing.
- See [porting_plan.md](porting_plan.md) for the current public parity matrix
  and roadmap.

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
- EXR header attributes.

Remaining major gaps:
- Full transfer / metadata synchronization parity with the C++ library.
- EXR transfer / host-export parity with the C++ library.
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
| CR3 | Real |

Current policy surface:
- `embedded_only`
- `sidecar_only`
- `embedded_and_sidecar`
- preserve existing embedded XMP
- strip existing embedded XMP

This is still narrower than the C++ transfer layer. OpenMeta-c now has a real
prepared-transfer / file-persistence lane for bounded XMP writeback, but it
does not yet match the C++ target breadth, stability, or host-adapter surface.
The bounded BMFF lane now includes direct CR3 roundtrip coverage in the C test
suite, and the TIFF-family plus `JPEG` / `PNG` / `WebP` / `JP2` lane now
includes direct bounded ICC carrier rewrite. `JPEG` now has a real direct
bounded `APP13` IPTC transfer path that preserves unrelated Photoshop IRB
resources, and the TIFF-family now has direct bounded tag `33723` IPTC
carriage as well. For `JXL`, the first host-side bridge is now a stable
serialized encoder ICC handoff API rather than an in-place ICC rewrite path.
On top of that, `OpenMeta-c` now has a first serialized transfer-payload batch
surface for bounded `EXIF` / `XMP` / `ICC` / `IPTC` carrier export across `JPEG`,
`TIFF` / `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, and bounded BMFF targets, plus
projected non-`C2PA` `JUMBF` carrier export for `JPEG`, `JXL`, and bounded
BMFF targets. That payload bridge now has a bounded `OMTPKG01` package-batch
layer above it for replayable carrier chunks. The C wire layout now matches
the public C++ package-batch family, but the current C builder still emits
only the subset it can derive from the current bounded transfer lane rather
than the broader executed prepared-bundle surface.

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

For embedded previews:
- use `omc_preview_scan_candidates(...)` to discover bounded preview sources
- use `omc_preview_extract_candidate(...)` to copy one candidate out

For file-level validation:
- use `omc_validate_file(...)` for decode-health checks, file-size guards,
  optional `.xmp` sidecar validation, and bounded `DNG` / `CCM` checks

For normalized `DNG` / `CCM` fields:
- use `omc_ccm_collect_fields(...)` to extract bounded color-matrix,
  white-balance, and calibration fields from an `omc_store`

For bounded `JXL` encoder bridging:
- use `omc_jxl_encoder_handoff_build(...)` and
  `omc_jxl_encoder_handoff_serialize(...)` to hand off ICC payload state to a
  host-side encoder
- use `omc_jxl_encoder_handoff_parse_view(...)` to inspect one persisted
  handoff without copying the ICC payload out of the input byte buffer

For bounded serialized transfer payloads:
- use `omc_transfer_payload_batch_build(...)` to build a reusable payload
  batch from one `omc_store`
- use `omc_transfer_payload_batch_serialize(...)` and
  `omc_transfer_payload_batch_deserialize(...)` to persist and reload that
  batch
- use `omc_transfer_payload_batch_replay(...)` to drive a host-side sink in
  stable payload order

For bounded serialized transfer package batches:
- use `omc_transfer_package_batch_build(...)` to build replayable carrier
  chunks from one `omc_store`
- use `omc_transfer_package_batch_serialize(...)` and
  `omc_transfer_package_batch_deserialize(...)` to persist and reload that
  package batch
- use `omc_transfer_package_batch_replay(...)` to drive a host-side sink in
  stable chunk order

For persisted transfer artifacts:
- use `omc_transfer_artifact_inspect(...)` to identify one persisted artifact
  and summarize its current public metadata; today that covers serialized
  transfer payload batches, transfer package batches, and persisted `JXL`
  encoder handoffs

## Current Gaps

The largest remaining gaps relative to the C++ library are:

- transfer stability and parity coverage across the newer bounded targets
- `EXR` transfer / host-export parity
- full validation parity beyond the current decode-summary + bounded `CCM`
  helper surface
- real C2PA verification backends
- broader parity work for newer upstream features
