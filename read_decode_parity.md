# Reading And Decoding Parity

Updated: 2026-09-08. Reference: C++ `0.4.127`, commit
`f11de3e06e93b35146b0ff54ae4a2c790f5bab98`. Initial C code: `71357c9`,
version `0.3.0`. This plan implements the reading/input and decoding stages
of [porting_plan.md](porting_plan.md).

## Target And Comparison Rules

Match the pinned C++ metadata read behavior through C89/C90 APIs: carrier
discovery, source-relative coordinates, logical payload extraction, typed
decoded entries, origin, limits and explicit residual/error outcomes. Cover
contiguous memory and positional callbacks separately. C++ itself retains
documented positional residuals; matching this reference does not imply
universal camera support or pixel decoding.

Use the current store, source contract, scanner and family decoders. Convert
one family at a time and share its decoding rules. Do not require a C++ runtime,
SDK integration or a new owning snapshot class to read metadata in C. Rich
fields already emitted by a reference decoder remain parity targets even when
their derivation needs a later batch. The general interpretation/query stages
remain separate planned work.

Acceptance compares status, format, block type/compression, offsets/sizes,
multipart group/order, key/type/count/value, duplicates and relevant origin.
Byte identity is required for logical payloads and promised canonical output,
not arbitrary rewritten files. Check malformed input and limits as well as
successful values. Keep every unexplained mismatch in the inventory.

## Input Inventory

These are starting states, not new acceptance results.

| Family / operation | C contiguous path | C callback path at 0.3.0 | Required convergence |
| --- | --- | --- | --- |
| Source, ranges, exact reads, windows | Present | Present, focused primitive differential gate | Preserve sticky failures and cumulative budgets; add cases with each converted reader |
| JPEG | Present | APP/COM collection; stop at SOS/EOI | Scanner-only probes and logical multipart payload reads without retaining all metadata at once |
| TIFF / BigTIFF / DNG | Present | TIFF-relative metadata snapshot | Direct value windows, distant values, shared nested-IFD decode and explicit scratch needs |
| RW2 / ORF and TIFF-based RAW | Existing TIFF/EXIF lanes | Special TIFF headers not converted | Match header variants and the relevant vendor offset rules |
| MakerNotes / nested TIFF payloads | Broad bounded decode | JPEG-contained notes; TIFF enrichment unsupported | Stage source-relative Nikon, Sony, Canon and remaining supported reference vendor layouts; retain explicit residuals |
| PNG / WebP | Present; C2PA carrier discovery missing | Unsupported | Chunk traversal, metadata-only collection, C2PA/JUMBF carriers, original coordinates, padding and compressed payload cases |
| JP2 / JXL / HEIF / AVIF / CR3 | Bounded scanners/decoders | Unsupported | Source-backed boxes, items/extents/properties and logical payloads; skip codestream and media ranges |
| GIF | Present | Unsupported | Extension framing/sub-block lengths and payload assembly; skip raster bodies |
| EXR | Header decode | Unsupported | Header traversal and selected attribute values; stop before image tables/data |
| RAF / X3F / CRW-CIFF | Bounded native/embedded lanes | Unsupported | Native directory/value reads and declared embedded JPEG/TIFF ranges; preserve reference residuals for undeclared fallback searches |
| Standalone metadata inputs | Existing simple-reader fallbacks | Not generally converted | Inventory reference-supported standalone inputs and add explicit bounded routes |

## Ordered Batches

| Batch | Work | Exit gate |
| --- | --- | --- |
| RD0 | Fresh Clang 20 baseline, pin enabled dependencies, separate read mismatches from historical transfer/write mismatches | Direct and focused gates recorded; broad inventory retained with reference/configuration |
| RD1 | PNG/WebP callback reads and their missing JUMBF carrier discovery; repair Brotli discovery | Memory/callback entries and original block coordinates agree; split JUMBF payloads agree with C++; pixel reads forbidden; padding, limits, compression on/off and failure preservation tested |
| RD2 | Direct TIFF values and special TIFF headers, then source-relative MakerNotes | Small distant metadata reads do not require scratch proportional to file offset; both endian forms, BigTIFF and supported vendor layouts match; unsupported nested paths are explicit |
| RD3 | Reusable positional scanner/payload operations; JPEG multipart and JP2/JXL/BMFF conversion | Metadata assembly across discontiguous ranges, bounded compressed input/output, original extent coordinates, no codestream/media reads |
| RD4 | GIF, EXR and native RAF/X3F/CRW readers | C/C++ native and declared embedded-container cases match; image bodies are skipped; undeclared fallback residuals are reported |
| RD5 | Remaining contiguous and callback decoder deltas by metadata family | Typed entries and failure behavior match for EXIF/MakerNote, XMP, IPTC/IRB, ICC, JUMBF and BMFF; no decoder-generated semantic fields silently excluded |
| RD6 | Stage acceptance and upstream tracking | All declared reading/decoding cases pass against pinned C++; corpus/platform/resource limitations listed separately |

RD1 is the first implementation batch. RD5 fixes needed by a converted reader
travel with that reader; it need not wait for RD4. The first BMFF decoder batch
should check typed `iref` roles/IDs and order, property-table counts and item
classification. Deeper scene/component/derived-image summaries follow as
explicit decoder batches, not exclusions. Structured XMP needs its own input
fixtures; optional Expat is eligible if it closes a demonstrated parser gap.

RD1 initially reuses the bounded metadata collection approach from JPEG.
Aggregate selected chunks must fit caller metadata scratch. This closes
callback functionality but is not acceptance of the later scanner/payload
API or minimum-memory parity. RD2/RD3 remove whole metadata-span requirements
where the reference supports windowed access. No callback path may silently
copy the entire image source as a fallback.

## Verification And Embedded Constraints

- Build C with Clang 20, C90, extensions disabled and warnings as errors.
  The C++ differential executable uses Clang 20/libc++ against the matching
  reference. Record actual zlib/Brotli/Expat availability, not just options.
- Reuse direct scanner/payload/read tests and the existing C++ parity harness.
  Keep a reading-only inventory independent of transfer/persist failures;
  retain the broad harness as a separate regression inventory.
- For each callback fixture, compare with contiguous C and the matching C++
  operation. Include nonzero source-range bases, metadata beyond large image
  gaps, end-of-container rules, short reads, cancellation/source change,
  malformed lengths, output limits and cumulative I/O budgets.
- Use callbacks that reject pixel/media access. Record requests, bytes and
  metadata scratch on fixed sparse fixtures. These counters do not establish
  throughput, whole-process peak memory or embedded acceptance.
- Run relevant direct and differential tests, the Release suite, sanitizer
  tests and enabled/disabled compression configurations for changed paths.
  Check allocation/capacity failures where storage changes.
- Preserve explicit source/workspace/store ownership. Audit stack and heap
  use as readers are converted. The existing 64-bit integer shim and 32-bit
  size behavior require target-specific validation. Native Windows, embedded
  targets and corpus acceptance are separate gates, not inferred from WSL.

## Progress Accounting

Record each family, operation, feature configuration, reference revision and
expected result. Separate implemented, directly tested, differentially matched
and residual cases. Establish this denominator before replacing rough stage
estimates with measured coverage. Passing test-executable counts and the old
full-harness mismatch count are not percentages of these stages.

## RD1 Checkpoint: Version 0.4.0

Implemented on 2026-09-08:

- PNG/WebP positional metadata collection, original block-coordinate remapping
  and reuse of the contiguous decoders. No image-body reads or new collection
  allocations. Aggregate metadata/framing must fit caller scratch; this is
  still a residual against the reference's scanner/payload access model.
- PNG `caBX` and WebP `C2PA` discovery in contiguous and callback paths. Logical
  JUMBF fragments can cross unrelated chunks and image data; an independent
  later JUMBF stream starts a new group. Capability queries now report bounded
  reading/structured decoding without advertising new transfer/write support.
- JUMBF output corrections: parent `.jumb_label` key, U8 `c2pa.detected`, the
  `c2pa.verify.require_trusted_chain` option field, and omission of the active
  manifest prefix unless unique. Measurement tracks the optional field without
  an unbounded prefix set. These are observable output changes from 0.3.0.
- Brotli discovery: remove empty normal variables that prevented CMake
  `find_path`/`find_library` searches. The enabled builds now compile and test
  both zlib and Brotli backends; disabled builds test explicit unsupported paths.

| Verified gate | Result |
| --- | --- |
| Fresh C baseline before edits | 36 direct + 2 focused targets passed; default inventory failed on nine BMFF fixtures |
| Pinned C++ Clang 20/libc++ baseline | 2/2 targets passed |
| C Clang 20 Release, C90 library with warnings as errors | 37/37 direct targets passed |
| Focused C/C++ authoring, source and new chunk targets | 3/3 passed |
| C Debug ASan/UBSan, zlib/Brotli enabled | 37/37 direct targets passed; LeakSanitizer required execution outside the ptrace sandbox |
| C Release, zlib/Brotli disabled | 37/37 direct + 3/3 focused C/C++ targets passed |
| Historical default differential inventory | Same nine BMFF fixture mismatches |
| Historical `--all` differential inventory | Same 266 mismatch reports; no added or removed report identifiers |

The broad inventory mixes reading, transfer and persistence. A complete separate
reading-only inventory is still pending; `--read-chunks` is the new focused
reading/decoding gate. Its disabled-zlib differential fixture omits compressed
PNG text so both libraries exercise the same enabled features; the direct
fixture still includes it and requires the explicit unsupported result.
This batch does not establish universal PNG/WebP coverage
or full JUMBF/C2PA parity. Native Windows, 32-bit/embedded targets, large corpus
acceptance and resource/throughput benchmarks remain untested.

The fixed chunk fixtures produce identical typed decoded records in C and C++.
Their callback variants use source-range base 37 and gaps of zero and 3 GiB;
callbacks reject access to image bodies. Scanner fields, logical payload bytes,
memory/callback origins and remapped blocks are checked separately.

| Fixture | Callback requests | Requested bytes | Metadata scratch bytes |
| --- | ---: | ---: | ---: |
| PNG, either gap | 18 | 462 | 444 |
| WebP, either gap | 15 | 510 | 500 |

These measurements exclude separate payload buffers, collection structural
arrays, decoder stack and store allocations. They demonstrate sparse access,
not an embedded memory budget or throughput result.

Local verification artifacts are under `/tmp/openmeta-read-20260908/`:
`c-all-baseline.log`, `cpp-baseline-tests.log`, `rd1-release-tests.log`,
`rd1-sanitize-tests-unrestricted.log`, `rd1-nodeps-tests.log`,
`rd1-default-parity.log` and `rd1-all-parity.log`. These temporary paths are
session evidence, not distributed test dependencies.

Reproduce with Clang 20 and the pinned C++ build/package directory:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-read-release-20260908 -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-read-reference-20260908
cmake --build /tmp/openmeta-c-read-release-20260908 -j 8
ctest --test-dir /tmp/openmeta-c-read-release-20260908 --output-on-failure \
  -E '^omc_test_parity$'
/tmp/openmeta-c-read-release-20260908/tests/omc_test_parity --read-chunks
/tmp/openmeta-c-read-release-20260908/tests/omc_test_parity --all
```

The final command intentionally returns failure while the inventory remains
open. RD2 is next: replace TIFF-relative scratch spans with direct value windows
before adding special TIFF headers and source-relative MakerNote layouts.

## RD2 Checkpoint: Version 0.5.0

The callback TIFF path now uses the existing typed IFD traversal with positional
structural reads and one caller value buffer. It no longer allocates scratch up
to the greatest TIFF offset. Inline values use an eight-byte local buffer.
GeoTIFF parameters share a checked combined value buffer. Revisited IFD offsets
are bounded and suppressed, with the reference GPS/Interop exception. Values
over the configured byte limit are emitted as truncated without reading them.

The public `omc_exif_dec_source` API reports value scratch needs and nested
residuals. High-level TIFF input decodes into a candidate store and preserves the
original store on I/O, scratch or allocation failure. Direct decoding retains
its documented partial-result contract. Source-relative vendor routes cover
Canon base selection, Nikon nested TIFF, Sony, Panasonic, Fuji, old Olympus
subtables, Casio type 2 and Ricoh theta pointers. Payload-local vendor decoders
remain shared. This is tested fixture coverage, not acceptance of every vendor
layout: candidate selection, uncommon external-reference layouts and malformed
value continuation remain RD5 differential work. Unknown undecoded notes and
failed source routes report residuals/status; they do not imply full enrichment.

Validation on Clang 20: 41/41 Release direct/focused targets, including a new
80-case C callback versus C++ contiguous TIFF/MakerNote gate; 41/41 with zlib and
Brotli disabled; 37/37 ASan/UBSan direct targets with LeakSanitizer outside the
ptrace sandbox. Fixed sparse fixtures test both byte orders and classic TIFF,
BigTIFF, RW2 and ORF. Directories and values are shifted beyond 2 GiB (classic)
or 8 GiB (BigTIFF), source base is 1000, value scratch is 12 bytes, and callbacks
reject gap reads. These tests compare typed C memory/callback entries and original
IFD offsets; the 80-case gate separately compares C++ output. Neither gate is a
complete C++ callback error-policy or vendor-offset inventory.

Artifacts: `/tmp/openmeta-rd2-build.log`, `/tmp/openmeta-rd2-tests.log`,
`/tmp/openmeta-rd2-nodeps-tests.log`, `/tmp/openmeta-rd2-sanitize-tests.log`.
Reproduce the differential gate with `tests/omc_test_parity --rd2` in the existing
Clang 20 Release build. RD3 follows with reusable source scanner/payload APIs.

## RD3 Checkpoint: Version 0.6.0

Reusable `omc_scan_source`/measurement and `omc_pay_ext_source`/measurement APIs
now share the existing memory parsers. A private borrowed input adapter uses
exact structural reads with a 32-byte cache. BMFF structural decoding also uses
this adapter, including item information, properties and typed references.
The high-level JPEG/PNG/WebP/JP2/JXL/BMFF path decodes one logical payload at a
time, preserves original coordinates and uses a candidate store on callbacks.
No aggregate metadata snapshot or whole-image fallback remains. PNG text still
needs one complete text chunk; many small header/string reads can reach the
request budget sooner than collection. These are explicit resource residuals.

JPEG extended XMP discovery, GUID/offset assembly and decoding were added.
Missing/overlapping fragments fail before reads. Deflate/Brotli feed buffers
are caller-owned and bounded. An optional Samsung local MakerNote postpass that
returns no additional table no longer creates a false incomplete-source result.

Validation: 42/42 Release direct/focused targets and 42/42 without zlib/Brotli;
37/37 ASan/UBSan direct targets. `--rd3` checks 17 C memory/callback decoded
fixtures and eight C/C++ callback scanner/payload fixtures. JP2/JXL metadata and
a two-extent BMFF item cross 8 GiB gaps with source base 37. JPEG ICC and extended
XMP arrive out of order. Payload prefixes, descriptor fields, malformed overlap,
short reads, cancellation and request limits are checked. Existing scanner and
payload tests additionally compare callback/memory results, including method-1/2
BMFF layouts and seven-byte compressed feeds. TIFF auto-detection is tested
against forbidden reads immediately after an eight-byte classic TIFF header.

The historical default nine BMFF mismatch identifiers and all 266 broad mismatch
identifiers are unchanged from RD1. This closes source access for the tested
RD3 lanes, not richer BMFF decoder parity. Fixed PNG/WebP request/scratch counters
are recorded in [positional_input.md](positional_input.md).

Artifacts: `/tmp/openmeta-rd3-release-tests.log`,
`/tmp/openmeta-rd3-nodeps-tests.log`,
`/tmp/openmeta-c-read-sanitize-20260908/Testing/Temporary/LastTest.log`,
`/tmp/openmeta-rd3-container-parity.log`, `/tmp/openmeta-rd3-default-parity.log`
and `/tmp/openmeta-rd3-all-parity.log`. RD4 follows with native formats.
