# OpenMeta-c

OpenMeta-c is a native C89/C90 port of OpenMeta.

It follows the C++ library's general metadata model with a C-native API:
flat, explicit, caller-buffer-oriented, and designed around bounded read,
edit, and writeback steps. The porting plan tracks differences in behavior
and safety coverage.

## Status

The C port provides metadata reads, bounded edits, and transfer/writeback.
The goal is a standalone rich metadata-processing library with substantially
the same portable core features as C++, including interpretation, structured
non-fuzzy queries and concept resolution. Those semantic stages currently have
partial foundations and remain porting work. Fuzzy search, host SDK adapters
(including Adobe DNG SDK), owning C++ wrappers and bindings remain above C.
Native DNG metadata processing is in scope.

Embedded-device use requires explicit resource limits and platform boundaries.
C89/C90 compatibility alone does not establish embedded acceptance; current
allocation, stack use and target-toolchain requirements still need validation.
The C library may later serve as the C++ implementation core. Temporary
implementation overlap is expected while behavior is ported and verified.

The [porting plan](porting_plan.md) records the source-reviewed baseline as of
2026-09-07 against C++ `0.4.127`, the scope clarification of 2026-09-08, the
parity matrix, and acceptance gates. Earlier percentage estimates based on a
narrower core boundary do not measure this scope. The
[authoring contract](authoring.md) documents typed construction, detached
validation, canonical EXIF, and the five explicit reverse-translation groups.
Version 0.2.0 changes the `omc_val` layout; rebuild consumers.

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
  Transfer prepare/payload/package options now also carry a target image spec
  for target-owned dimensions, orientation, sample layout, compression, and
  EXIF color space. The C transfer path filters stale source image-layout
  EXIF/XMP fields before emitting target metadata, matching the C++ transfer
  contract for this bounded EXIF/XMP lane.
- A first serialized transfer-package batch API now also exists in C for
  bounded carrier-chunk handoff via the public `OMTPKG01` family, turning the
  current payload bridge into replayable `JPEG` segments plus inline or
  target-typed transfer blocks for the current non-`JPEG` carriers. The C
  package layer can also materialize an executed output as `OMTPKG01` using
  source-range chunks for unchanged bytes, JPEG segment chunks for changed
  leading JPEG metadata segments, TIFF-family pointer/tail chunks for
  append-style TIFF/DNG rewrites, PNG/WebP carrier chunks, top-level box
  chunks for bounded JP2/JXL/HEIF/AVIF/CR3 edits, and inline chunks for
  generic changed bytes.
  Replay now emits zero-copy semantic package views, matching the public C++
  package-view contract. This is still narrower than the full C++ edit-plan
  package builder.
- A generic persisted-artifact inspect API now exists in C for serialized
  transfer payload batches, transfer package batches, and `JXL` encoder
  handoffs.
- The `JPEG` lane now also has direct bounded `APP13` IPTC-IIM carriage via
  Photoshop IRB `0x0404`, while preserving unrelated IRB resources.
- Prepared transfer / persist APIs support bounded EXIF/XMP/ICC/IPTC lanes.
  Their behavior and coverage remain narrower than the current C++ core.
- An optional OpenSSL verification backend is eligible for C but is not yet
  implemented. Full C2PA trust/signing and C++ host-adapter surfaces remain
  outside the current C-core milestone.
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
suite, and the TIFF-family plus `JPEG` / `PNG` / `WebP` / `JP2` /
`HEIF` / `AVIF` / `CR3` lane now includes direct bounded ICC carrier rewrite.
Direct BMFF XMP/EXIF writeback also now emits bounded `iref/cdsc` metadata
item references when the target exposes a primary item through `pitm`, with
direct C coverage for both replacing existing metadata items and extending a
primary-only `meta` item graph through `iinf`, `iloc`, and `idat`.
This direct writeback path is separate from package materialization, whose
item routes currently append fresh item IDs.
`JPEG` now has a real direct bounded `APP13` IPTC transfer path that preserves
unrelated Photoshop IRB resources, and the TIFF-family now has direct bounded
tag `33723` IPTC carriage as well. For `JXL`, the first host-side bridge is now
a stable serialized encoder ICC handoff API rather than an in-place ICC rewrite
path.
On top of that, `OpenMeta-c` now has a first serialized transfer-payload batch
surface for bounded `EXIF` / `XMP` / `ICC` / `IPTC` carrier export across `JPEG`,
`TIFF` / `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, and bounded BMFF targets, plus
projected non-`C2PA` `JUMBF` carrier export for `JPEG`, `JXL`, and bounded
BMFF targets. That bridge now accepts target-owned image properties and filters
stale source image-layout EXIF/XMP fields before payload/package emission.
That payload bridge now has a bounded `OMTPKG01` package-batch
layer above it for replayable carrier chunks, plus a first executed-output
materializer that packages final edited bytes with source-range and inline
chunks, and now emits format-aware JPEG segment chunks for changed leading JPEG
metadata plus TIFF-family pointer/tail chunks for append-style TIFF/DNG
rewrites, PNG/WebP carrier chunks, and top-level box chunks for bounded
JP2/JXL/HEIF/AVIF/CR3 edits, with direct arena and bounded-buffer helpers to
materialize either a validated package batch or persisted `OMTPKG01` bytes back
into final output bytes. The C wire layout and replay view now match the public
C++ package-batch family. An explicit target-aware BMFF package materializer
supports Exif/XMP/JUMBF/C2PA item insertion and bounded ICC property rewrite,
including synthesized `idat`, inserted 32-bit item IDs, and bounded retained
method-2 references. This is narrower than the current C++ managed-item
replacement/strip, graph-reference remapping, compact `iloc` normalization,
self-contained data-reference handling, and multiple-`ipma` consolidation.

## Layout

- `src/omc/`: public headers
- `src/read/`: scan, payload, decode, and naming logic
- `src/edit/`: XMP dump, embed, write, and apply logic
- `src/core/`: store, key/value, and edit core
- `src/base/`: low-level arena support
- `tests/`: unit tests and optional parity tests

## Dependencies

The allowed optional runtime dependencies are **zlib, Brotli, Expat and
OpenSSL/libcrypto**, including the corresponding metadata processing logic.
The current C build wires zlib and Brotli. Expat and OpenSSL are eligible
optional backends, not implemented integrations; XMP currently uses the C parser.

**RapidFuzz and Adobe DNG SDK remain C++ only.** Other third-party development,
test and verification dependencies must not become C runtime requirements.
Under project distribution policy, ExifTool and other external verification
applications are invoked only as separately installed tools and are not
bundled in the Apache-2.0 project's source or binary releases.

See [Dependency And Distribution Policy](porting_plan.md#dependency-and-distribution-policy)
for backend and packaging boundaries.

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

Earlier validation covered the following configurations. These are historical
coverage claims; refresh the applicable builds before current acceptance:
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
- use `omc_transfer_package_batch_build_executed_output(...)` to package a
  completed `omc_transfer_execute(...)` edited output as `OMTPKG01`
- use `omc_transfer_package_batch_serialize(...)` and
  `omc_transfer_package_batch_deserialize(...)` to persist and reload that
  package batch
- use `omc_transfer_package_batch_materialize_to_buffer(...)` when the caller
  owns the final output buffer and wants no hidden allocation
- use `omc_transfer_package_batch_materialize(...)` when a caller wants the
  package bytes concatenated into one caller-owned arena
- use `omc_transfer_package_bytes_materialize_to_buffer(...)` when the caller
  has persisted `OMTPKG01` bytes and explicit temporary storage
- use `omc_transfer_package_batch_collect_views(...)` to collect zero-copy
  semantic views over package chunks into caller-owned storage
- use `omc_transfer_package_batch_replay(...)` to drive a host-side semantic
  package-view sink in stable chunk order

For persisted transfer artifacts:
- use `omc_transfer_artifact_inspect(...)` to identify one persisted artifact
  and summarize its current public metadata; today that covers serialized
  transfer payload batches, transfer package batches, and persisted `JXL`
  encoder handoffs

## Current Core Checkpoint

Version 0.7.0 includes typed values and atomic edits, detached validation,
canonical EXIF serialization, the five bounded native translation groups,
paired IPTC date/time projection, shared BMFF replacement, and bounded
positional input for JPEG, TIFF/BigTIFF/DNG, PNG, WebP, JP2, JXL, BMFF, GIF,
EXR, CIFF and native/declared RAF/X3F metadata. PNG `caBX` and WebP
`C2PA` discovery now supports split JUMBF metadata. See [authoring.md](authoring.md),
[bmff_writing.md](bmff_writing.md), and [positional_input.md](positional_input.md).

TIFF callback input now reads distant directories and values with one value
buffer; RW2/ORF headers and tested source MakerNote layouts are supported.
See [read_decode_parity.md](read_decode_parity.md) for residual coverage.

Clang 20 validation passed 44/44 Release direct plus focused C++ parity targets,
38/38 ASan/UBSan direct targets, and 44/44 direct/focused targets without
compression dependencies. Brotli discovery now enables the available backend, alongside
zlib. These are synthetic WSL gates against C++ 0.4.127. The historical broad
differential inventory still fails with the same 266 mismatch reports; it is
not part of the focused passing gate.

JUMBF decoded output now follows the reference: parent labels use `jumb_label`,
`c2pa.detected` is U8, an empty/ambiguous active-manifest prefix is omitted, and
`c2pa.verify.require_trusted_chain` reflects the option. Consumers matching the
old label key or scalar type must update. No crypto backend was added.

The next work follows [read_decode_parity.md](read_decode_parity.md): RD5
remaining decoder and standalone-input differences, then RD6 stage acceptance.

Remaining portable-core work includes:

- broader transfer-safety, lifecycle, structured XMP, and package parity
- additional MakerNote offset layouts and standalone metadata source routes
- full decoder semantics, failure-policy and resource acceptance
- selected BMFF primitive/read/naming differences and broader writer acceptance

An isolated C++ consumer can now evaluate the typed serializer or bounded
source contract. Rich host features, full C2PA trust, and full EXR rewriting
remain outside the first core milestone. See [porting_plan.md](porting_plan.md)
for the scoped matrix and remaining acceptance work.
