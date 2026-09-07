# OpenMeta-c Porting Plan

Updated: 2026-09-07.

## Goal And Reference Baseline

Bring the portable C metadata core into behavioral parity with the relevant
C++ core. Keep it suitable for possible reuse underneath the C++ library.
C++ API coverage is not the completion criterion: owning convenience objects,
rich queries, host adapters, search, and language bindings can remain in C++.

This revision compares these public source snapshots:

| Repository | Revision | Meaning |
| --- | --- | --- |
| OpenMeta C++ | `f11de3e06e93b35146b0ff54ae4a2c790f5bab98`, version `0.4.127` | Reference through multiple-`ipma` BMFF consolidation |
| OpenMeta-c | `422795e8c2f63bfd77a742fcd2491ac971ee90ed` | C implementation through bounded `iloc` method-2 reference support |

Evidence comes from public headers, implementations, tests, and API docs.
The original matrix was a source review. The execution checkpoint below records
fresh builds and synthetic tests; it does not establish a corpus audit or
performance result. Private inputs and reports are not part of this note.
Source paths below are relative to the named public repository.

The earlier plan recorded Release results of `30/30`, including parity, and
older Debug, RelWithDebInfo, and ASan results of `27/27`. These are historical
snapshots, not acceptance of the revisions above on a new machine. Existing
build caches must not substitute for a freshly configured baseline.

The old estimates of `80-85%` read coverage and `60-65%` overall public surface
do not define a current core-parity denominator. Track the scoped matrix and
its acceptance cases instead. Do not average C++ product percentages into C
completion or count intentionally excluded C++ features as C defects.

## Execution Checkpoint: Typed Writer and Translation

Fresh Clang 20 builds use C++ 0.4.127 as the pinned reference. The original
C direct baseline passed 29/29 tests; C++ passed 2/2 CTest targets. The original
C sanitizer run exposed test-only diagnostic string overreads, now corrected.
The initial default differential run also found a missing Photoshop IPTC byte
count, now corrected, and BMFF differences.

The authoring batch adds typed makers and array byte order, atomic edit
publication, detached validation, canonical TIFF serialization, all five
bounded reverse-translation groups, and paired forward IPTC date/time
projection. Canonical output now feeds the carrier writers. TIFF/BigTIFF
metadata updates preserve unrelated entries and image-data offsets. PNG
writers calculate CRCs independently of optional compression support.

The Release direct plus focused authoring differential gate passed 34/34;
the corresponding Debug ASan/UBSan direct gate passed 33/33. The no-compression
build passed 33/33; PNG ICC emission asserts unsupported without zlib, and
the dependent PNG ICC persistence case is omitted in that configuration. These are synthetic
fixture gates, not a corpus or native Windows acceptance result. The public
value layout changes in version 0.2.0; consumers must rebuild.

The historical full differential harness remains a failing inventory. Its
previously disabled transfer/persist block is now available through
`omc_test_parity --all`. Nested structured XMP and broader transfer lifecycle
cases still differ. Default parity still includes ten BMFF fixtures with
primitive and richer C++ summary differences. The focused authoring gate does
not suppress or relabel those failures. BMFF writer closure and positional
input remain the next implementation batches.

See [authoring.md](authoring.md) for contracts and bounded coverage. The matrix
below retains source-review detail where broader acceptance remains open.

## Core Boundary And Decisions

The existing `omc_store`, arenas, scanners, decoders, edits, and transfer code
are the starting point. Extend these mechanisms; do not create a second store,
a general object framework, or a separate writer for each translation group.

| Layer | Intended responsibility |
| --- | --- |
| C portable core | Bounded input, metadata keys/values/origin, decode, typed validation, explicit edits and translation, canonical payload serialization, bounded carrier rewrite, transfer policy primitives, payload/package wire formats |
| C++ layer | Owning objects and ergonomic APIs, rich interpretation/query/concept aggregation, fuzzy search, host adoption and codec adapters, higher-level scheduling and file workflows, Python and other bindings |
| Host/platform boundary | Storage and I/O policy, image facts, worker scheduling, SDK integration, signing/trust services, publication/privacy policy |

Existing C file/persist helpers remain supported. A reusable algorithm must
also have a memory-oriented entry point; path handling must not become a
dependency of validation, translation, or serialization.

Rules for the proposed common core:

- Preserve flat C APIs, explicit lifetimes, capacities, errors, and resource
  limits. Current targets compile as C90 with a 64-bit integer shim; retain
  that compatibility while the project is referred to as the C99 port. A
  language-level change is a separate decision, not a prerequisite here.
- Use setup-time allocation/reservation where the existing API owns storage.
  Make scratch requirements explicit. Do not describe all current C code as
  caller-allocated or allocation-free. Promise allocation-free execution only
  for measured and tested replay/patch paths.
- Keep algorithms and wire contracts reusable without C++ types or a C++
  runtime. C++ should eventually wrap selected C operations without
  reimplementing their validation or policy.
- Keep translation explicit. XMP projection during transfer is not permission
  to rewrite native EXIF/IPTC. Preserve exact types, rational/date precision,
  source provenance, target image authority, and deletion/conflict behavior.
- Share immutable prepared bytes; give each worker separate mutable state.
  Do not copy C++ ownership classes or impose a threading runtime on C.
- Reuse public registry facts and small synthetic fixtures where appropriate.
  Check semantic contracts before sharing tables; matching tag names alone
  does not establish matching values or write safety.
- Treat future C++ use of C as a separate migration milestone. This plan does
  not switch the C++ implementation, freeze a new ABI, or add C++ dependencies
  to the C library.

## Dependency And Distribution Policy

The C port may use zlib, Brotli, Expat and OpenSSL through their C APIs and
port the corresponding OpenMeta logic. These dependencies remain optional;
their use does not require a C++ wrapper or a C++ runtime in the C library.

| Dependency | C-port policy | Current state and work boundary |
| --- | --- | --- |
| zlib | Allowed optional C dependency | Already wired through `OMC_USE_ZLIB`. Keep bounded Deflate handling and port matching metadata read/write logic as needed. |
| Brotli | Allowed optional C dependency | Already wired through `OMC_USE_BROTLI`. Keep bounded compressed-metadata decoding and matching JXL behavior. |
| Expat | Allowed optional C dependency | Not wired into the C build; C currently has its own XMP parser. An Expat-backed path and the relevant C++ XMP decode logic are eligible for porting. Compare behavior and resource limits before changing parser selection. |
| OpenSSL / libcrypto | Allowed optional C dependency | Not wired into the C build. Bounded signature/certificate verification logic is eligible for an optional C backend; this does not establish full C2PA asset binding, signing or trust-policy support. |
| RapidFuzz | C++ only | Fuzzy search remains above the C core. |
| Adobe DNG SDK | C++ only | SDK integration remains above the C core; native C DNG metadata support is independent. |

Expat/OpenSSL eligibility is a scope decision, not a claim that these backends
are implemented. Add explicit feature discovery, capability reporting and
enabled/disabled tests when introducing either backend. An unavailable backend
must produce the documented unsupported/disabled result. Preserve the existing
dependency-free paths where applicable. Optional parser/verification work must
not make unrelated scan, EXIF decode or transfer operations depend on it.

Other third-party tools and frameworks used for tests, verification or
development remain outside the C runtime dependency set. Keep their headers,
libraries and executables out of the installed C API and runtime dependency
exports. A C++ parity executable may link C++ test/reference dependencies
without making the C library depend on them.

Under the project's distribution policy, ExifTool is an external verification
tool only. Do not vendor, link, copy, install or bundle ExifTool or other
external verification applications with OpenMeta-c source releases, libraries
or binary packages. Tests may discover and invoke separately installed tools.
Their absence must not prevent building or installing the C library.
Keep the public project Apache-2.0 and retain applicable notices for approved
dependencies included in a distribution.

## Parity Matrix

Status describes inspected source, not a passing test run:

- **Present**: implementation and direct tests exist for a bounded C contract.
  Current reference parity still needs the baseline gate.
- **Partial**: an existing C mechanism needs specific behavior or coverage.
- **Missing**: no equivalent public C operation was found. Internal reusable
  code may still exist.
- **C++ layer**: intentionally excluded from the core-parity denominator.
- **Conditional**: defer until a concrete C consumer or the reuse experiment
  demonstrates a need. It does not block the first core milestone.

### Model, Reading, And Validation

| ID | Capability | Current C evidence | Status and decision |
| --- | --- | --- | --- |
| R1 | Store, keys, values, origins, edits | `omc_store.h`, `omc_val.h`, `omc_edit.h`; add/set/tombstone and reserve operations | Present and tested: typed makers, array byte order, candidate publication on commit/compact, and failure preservation. |
| R2 | Contiguous scan, payload assembly, decode | `omc_scan.h`, `omc_pay.h`, `omc_read.h`; direct EXIF/XMP/ICC/IPTC/IRB/JUMBF/EXR tests | Present, broad bounded coverage. Promote C++ differences by fixture; no universal camera/read-parity claim. |
| R3 | MakerNote/native RAW and modern-container enrichment | `src/read/omc_exif.c`, `omc_bmff.c`, naming and read tests | Partial against the newer C++ tree. Preserve raw/unknown values; port safety-relevant facts and small proven read deltas before long-tail descriptive enrichment. |
| R4 | BMFF derived fields | Item semantics, properties, `ipma` associations and `grpl` summaries in `omc_bmff.c` | Partial. C++ has deeper scene/component, derived-image and display-transform summaries. Keep parsing/preservation facts in C; broad semantic aggregation may stay in C++. Read-side summaries do not imply writer remapping. |
| R5 | Positional source, read budgets, source ranges/windows | Current C scan/decode APIs take complete byte spans; `omc_read_simple` is file-oriented | Missing public source abstraction. Port a bounded `read_at` primitive and readers in stages; an output package source range is not an input source API. |
| R6 | Runtime capabilities, preview, CCM/DNG helpers | `omc_capabilities.h`, `omc_preview.h`, `omc_ccm_query.h` and direct tests | Present bounded helpers. Capabilities must report actual C support and enabled compression features. |
| R7 | Detached entry/store validation | `omc_validate.h` exposes file/read diagnostics and CCM checks | Implemented initial detached schema in `omc_store_validate.h`; bounded diagnostics, wire/value checks, singleton and image-context tests. |
| R8 | Decoded snapshots, source provenance and persistence | C callers retain stores/bytes; transfer packages retain output source ranges | Missing named snapshot API; Conditional. C++ snapshot v1 exists. Positional input does not require its owning or serialized snapshot object first. |

### Creation, Translation, And Writing

| ID | Capability | C++ reference and current C evidence | Status and decision |
| --- | --- | --- | --- |
| W1 | Typed authoring | C++ `create_metadata_store()` preflights, copies, validates and publishes atomically. C has typed value makers, explicit array byte order, and candidate-based edit publication. | Implemented bounded typed helpers and output-preserving transactions; see `authoring.md`. Logical builders and FlatHost import remain above the core. |
| W2 | Canonical TIFF/EXIF serialization | C++ `serialize_exif_tiff()` is target-neutral and honors supported wire hints. C's public serializer builds typed TIFF; internal transfer payloads apply target framing above it. | Implemented `omc_serialize_exif_tiff()` with direct tests and exact C++ byte comparison. Carrier wrappers reuse canonical output; TIFF/BigTIFF retain target layout. |
| W3 | EXIF/IPTC to portable XMP | C already has projection, all three conflict policies, custom namespaces and managed-namespace canonicalization | Paired IPTC creation/digital-creation projection implemented and tested. Broader structured XMP parity remains partial. |
| W4 | Explicit XMP to native metadata | C++ has date, technical, capture, descriptive and target-bound geometry translation. C exposes these groups through `omc_translate_xmp()`. | Implemented all five bounded groups in `omc_translation.h`, with one transaction and focused C++ EXIF/IPTC differential tests. Broader mappings remain separate batches. |
| W5 | Native IPTC-IIM emission | Internal `omc_transfer_build_iptc_iim()` emits datasets; JPEG IRB and TIFF tag `33723` carrier paths exist | Present bounded mechanism. Reuse it for descriptive/date translation; add charset, repetition, tombstone and stale-IRB checks. A separate public IPTC writer is not a prerequisite. |
| W6 | Target image facts and transfer safety | C has target image spec, CompatibleFile/RenderedImage and diagnostics; C++ has wider source-processing classification and a RAW-data descriptor | Partial. C has no source descriptor or explicit lens/preview/general-processing audit categories. Verify selected fields through actual transfer paths; port needed safety facts without the rich query system. |
| W7 | Prepare, compile, execute, persist | `omc_transfer.h`, `omc_transfer_persist.h` and direct tests | Present bounded pipeline. Existing `omc_transfer_compile()` does not imply parity with C++ compiled worker/handoff APIs. Extend the pipeline rather than replacing it. |
| W8 | XMP carrier merge and lifecycle | C has destination embedded/sidecar stores, precedence, writeback and persistence options | Present controls, Partial lifecycle parity. Test modes/defaults, strip/overwrite/failure behavior and source/destination conflicts. |
| W9 | Payload/package artifacts | C has `OMTPLD01` v1, `OMTPKG01` v2, semantic views, replay, executed-output materialization and artifact inspection | Present bounded wire families. Test interoperation in both directions; matching version/magic does not establish complete builder/execution parity. |
| W10 | BMFF package item insertion | C has Exif/XMP/JUMBF/C2PA routes, ICC, synthesized `idat`, inserted 32-bit IDs and bounded method-2 references | Present bounded materializer. It appends item IDs/entries; this is not full C++ managed-item replacement/strip parity. Do not schedule the insertion foundation again. |
| W11 | Newer bounded BMFF writer rules | C++ compact `iloc`, self-contained `dref`, managed-item replacement/remapping and multiple `ipma` consolidation | Partial. C package `iloc` accepts 4/8-byte offsets/lengths and rejects nonzero data references; graph/property handling is narrower. Extend current paths, with separate replace/strip and direct/package tests. |
| W12 | Prepared canonical TIFF patching | C++ `exif_tiff_patch.h` has plan-scoped handles, fixed-width typed transactions and independent workers | Missing; Conditional for the first writer milestone. Useful later as a small reusable execution primitive after W1/W2, without C++ owner classes. |
| W13 | MakerNote trust and C2PA | C has conservative rendered filtering and bounded JUMBF/C2PA routes; C++ has richer MakerNote layout audits and optional verification | Partial safety facts. Keep opaque preservation distinct from verified relocation. Bounded OpenSSL verification logic is eligible as an optional C backend. Rendered C2PA invalidation/drop stays explicit; full asset binding, signing and trust remain outside the first writer milestone. |

### Features That Stay Above The C Core

| Feature | Decision |
| --- | --- |
| Rich query candidates, confidence/provenance presentation, concept resolution and interpretation records | C++ layer. Port only primitive facts needed by C read/write/safety operations; do not copy the full query graph or its containers. |
| Fuzzy search, Unicode/transliteration policy and optional search indexes | C++ layer. Independent capability; not a metadata-core parity gate. |
| Logical convenience builders, FlatHost mapping/reconciliation and typed codec operations | C++ layer. C typed data/validation/edits can provide their eventual foundation. |
| Host Adoption Profile, PreparedTransferHandoff, generic adapter views and owning compiled plans | C++ layer. Wire payloads and narrow replay primitives remain the C bridge. |
| OCIO, EXR host adapters, Adobe DNG SDK and LibRaw integration | C++ layer. EXR header read remains in C; host emission is Conditional, and full EXR file rewrite is outside scope. |
| OIIO adapter | Removed from the current C++ tree. Do not list it as an implemented feature to port; any future bridge is separate integration work. |
| Python/nanobind, CLI feature duplication and downstream host wrappers | C++ layer. A small C diagnostic test tool may be justified without a second product CLI. |
| Snapshot v1 serialization, raw-carrier provenance and deferred snapshot ownership | Conditional. Revisit after positional readers for a concrete persistence or deferred-prepare workflow. |
| Full prepared-bundle serialization, arbitrary container graph editing, full RDF and general cross-family sync | Not current C commitments. Several also exceed the bounded C++ contract. |
| Full C2PA asset binding, signing/resigning, external-signer packages and trust policy | Outside this core milestone. Keep structural decode and safe bounded transfer behavior. |
| Pixel decoding/encoding, color transforms and application of RAW curves/LUTs | Host responsibility. Metadata about a processing operation is not authorization to apply it. |

### Carrier Baseline To Preserve

These are implemented C lanes with direct synthetic tests, not newly verified
format-wide parity. Check each metadata family, route and writeback mode.

| Target | Existing C lane | Next acceptance focus |
| --- | --- | --- |
| JPEG | EXIF/XMP, ICC APP2, IPTC APP13 IRB `0x0404`, bounded JUMBF packages | First typed author/edit/translate/write/readback fixture; preserve unrelated IRB resources |
| TIFF / BigTIFF | EXIF/XMP, ICC `34675`, IPTC `33723`, executed-output pointer/tail chunks | Native types, companion deletion, retained directories/previews and rendered safety |
| DNG | Existing/template/minimal-fresh-scaffold modes, ICC/IPTC carriage | Target-owned layout/calibration; no-target fresh output and persist parity |
| PNG / WebP / JP2 | Bounded EXIF/XMP/ICC lanes and format-aware packages | Canonical EXIF wrapping, preservation, XMP lifecycle and semantic readback |
| JXL | EXIF including replacement of `brob(Exif)`, XMP/JUMBF, serialized encoder ICC handoff | Brotli on/off and wire interoperation; ICC handoff is not an in-place ICC writer |
| HEIF / AVIF / CR3 | Bounded EXIF/XMP/ICC rewrite and explicit package graph materialization | Newer `iloc`/reference/property rules; test direct rewrite and package materialization separately |
| EXR | Header decode | Maintain read parity; host-emitter work does not block writer convergence |

## Fastest Delivery Route

Close shared behavior once and reuse it across targets. Do not rebuild every
C++ header or wait for all read enrichment before shipping a useful writer
milestone. Relative scope below is an engineering estimate, not a measured
schedule. Establish timings in B0 before assigning calendar estimates.

| Batch | Deliverable | Dependency / relative scope | Exit gate |
| --- | --- | --- | --- |
| B0 | Fresh baseline and pinned differential inventory | First; small unless failures appear | Current direct C and explicitly enabled parity tests run; dependencies/skips recorded; mismatches classified |
| B1 | Typed construction, transaction safety, initial detached validation, canonical EXIF and required safety facts | B0; medium/large shared foundation | Types, permitted unknown tags, rationals, text/arrays, wire hints, exact size, failure behavior and selected safety rules tested |
| B2 | Five reverse-translation groups plus relevant forward-XMP deltas | B1; medium, split by mapping group | Exact native JPEG/TIFF readback, conflicts/tombstones/provenance and failure atomicity agree with reference |
| B3 | Bounded BMFF replacement, preservation and packages | B0; medium/large, independent of translation logic | HEIF/AVIF/CR3 direct and persisted-package graph fixtures agree on retained and changed semantics |
| B4 | Positional source and incremental reader conversion | B0; small primitive, large staged conversion | Memory/callback parity, budgets, cancellation/short reads; measured I/O skips image payloads |
| B5 | Optional common-core reuse experiment | Selected B1-B4 operations accepted; bounded experiment | Isolated C++ consumer delegates one operation to C with behavior/cost evidence; production switch is a separate decision |

Default order is **B0 -> B1 -> B2 -> B3 -> B4**, then evaluate B5. Safety or
existing-test regressions in B0 preempt features. Small fixture-backed
read/name/projection corrections can travel with the relevant batch. Move B3
ahead of B1/B2 if a concrete consumer is blocked on BMFF preservation.
Do not delay B2 for a complete positional-reader port or rich semantic parity.
Expat-backed XMP decoding may be introduced with a relevant read/projection
batch when it closes a demonstrated parity gap. Optional OpenSSL verification
is a separate bounded batch; its availability does not block B1-B4.

### B0: Establish A Usable Comparison Loop

1. Configure fresh native build directories for both pinned repositories.
   Record compiler, standard-library ABI, build type, compression features,
   reference revision, configure command and actual tests discovered.
2. Start with Clang 20 Release, direct C tests, and `OMC_BUILD_PARITY_TESTS=ON`
   against a fresh reference through `OMC_OPENMETA_DIR`. The current Clang
   parity target explicitly adds `-stdlib=libc++`; a dependency prefix alone
   does not select libstdc++. Use a matching libc++ C++ reference for this
   first gate. A libstdc++ matrix needs an explicit supported target setup.
3. Run direct C tests under Debug with ASan/UBSan where supported. Keep the
   C90/public-header smoke contract. Inspect Release test checks so a disabled
   assertion cannot turn a skipped operation into a reported pass.
4. Reuse `tests/test_omc_parity.cc` and direct C fixtures. Add focused cases
   there first; do not build a new corpus framework before it is needed.
   Diagnose reference artifact/header skew separately from C behavior.
5. For each matrix row, record supported cases, failures, deferred cases and
   latest verified revisions. Exclude intentional C++-layer features from the
   denominator; retain unexplained mismatches. Update capability claims only
   after matching direct and differential tests pass.

Use semantic comparison for metadata and equivalent rewritten files: key,
kind/type/count, values, duplicates, deleted state and relevant provenance.
Require byte equality only where a shared canonical/wire contract promises it.
Whole-file hashes are not a general metadata-parity oracle.

### B1: Build The Shared Typed Write Foundation

Start with `omc_val`, `omc_store`, `omc_edit`, `omc_exif_write` and the internal
IPTC emitter. Do not route authoring through a fabricated target image file.

- Complete needed scalar makers, notably signed integers and signed/unsigned
  rationals. Rational kinds already exist; the gap is helpers and end-to-end
  typed handling, not a new value model.
- Define borrowed input/copied output lifetimes for entries and arrays,
  preserving wire hints, counts, duplicate policy and origin.
- Add bounded detached validation for the initial schema: key/value shape,
  rational denominator, text/namespace/path validity, TIFF type/count/IFD,
  singleton duplicates, and supplied image facts. Unknown/private tags remain
  allowed under explicit policy; do not wait for a complete registry.
- Preserve previous output on failure in new authoring/translation operations.
  Current `omc_edit_commit()` resets `out` before fallible reserve/copy work.
  It cannot establish output-preserving transaction parity. Use a candidate
  result with explicit storage and publish on success, or preflight storage
  with an equally strong documented guarantee. Test aliasing, invalid
  operations and allocation/capacity failures.
- Extend fixed-field serialization through a reusable typed TIFF payload path,
  preserving existing carriers. Signed exposure bias, date timezone/subsecond
  companions and Software are absent from the current field selector. They
  must reach native output, not merely exist in the translated store.
- Expose canonical TIFF measurement/write: deterministic little-endian layout,
  supported wire hints, MakerNote policy, limits and unsupported outcomes.
  Match the C++ short-buffer contract when promised: a deterministic truncated
  prefix is allowed there, unlike failure-atomic authoring/translation.
- Apply carrier wrapping once above the canonical payload. Compare JPEG,
  PNG, WebP, JP2/JXL/BMFF and TIFF/DNG handoff bytes with the current reference.
  Do not treat old target-specific framing as the new canonical contract.
- Audit source-processing safety needed by these writes, including target-owned
  dimensions/layout, RAW/rendered source facts, lens/preview-related fields,
  ICC, MakerNotes and C2PA. Add narrow classification/descriptor inputs only
  where the reference behavior needs them; do not import its query model.
  Preserve capture facts separately from instructions that reprocess pixels.

First B1 slice: typed makers, detached validation and transaction failure tests.
Next: canonical serialization plus native readback and selected safety cases.
Full fixed-width patch workers are not required to complete B1.

### B2: Catch The Current C++ Translation Contract

Translation is explicit before transfer. Reuse bounded group-selection,
conflict, tombstone, provenance and commit helpers. Do not create a general
expression engine or five independent transaction frameworks.

| Slice | Existing C++ behavior to port | Required edge cases |
| --- | --- | --- |
| B2a: dates and technical EXIF | Creation/original/digitized and ModifyDate groups; Make, Model, Software | Gregorian validation, timezone and up to nine EXIF fractional digits, IPTC precision rejection, ASCII/NUL rules, companion deletion |
| B2b: capture EXIF | ExposureTime, FNumber, ISO, FocalLength, ExposureBiasValue | Exact integer/rational conversion; native SHORT/RATIONAL/SRATIONAL; aliases, overflow, zero denominators, no floating approximation |
| B2c: descriptive IPTC | Title, description, creators, keywords, rights, credit, source | Default language, numeric index order, byte limits, UTF-8 charset declaration/conflicts, duplicate sources and stale raw IRB precedence |
| B2d: target geometry | Orientation and complete stored width/height groups | Host target spec, consistent aliases, partial/deleted groups, no orientation-driven dimension swap |

All slices use the C++ `DirtyOnly` default and explicit PreserveExisting,
FailOnConflict and ReplaceExisting semantics for complete native groups.
Require immutable source and unchanged previous output on rejection. Test
unchanged fields, dirty tombstones, aliases, capacity limits and origin/wire-name
lifetime. A dirty repeated-group member must not discard unchanged active ones.

In B2a, add paired IPTC DateCreated/TimeCreated and
DigitalCreationDate/DigitalCreationTime projection into XMP, including invalid
or missing time handling and generated-vs-existing precedence. Custom namespace
support already exists in C; audit compatibility instead of implementing it
again. Native EXIF/IPTC emission stays independent of XMP projection toggles.

The current C++ next direction is broader bounded native synchronization.
Track that as upstream growth after these five groups. Do not label it already
implemented, guess the next mapping group, or postpone C until a general sync
engine exists. Pin and close one reference batch before advancing it.

### B3: Close Specific BMFF Deltas

Keep the package route/materialization boundary and direct XMP/EXIF/ICC paths.
Current source shows these distinct gaps:

- Package `iloc` accepts only 4/8-byte offsets and lengths, with 0/4/8-byte
  bases; it retains widths rather than normalizing compact forms.
- Any nonzero `data_reference_index` is rejected, including self-contained
  references accepted by the newer C++ contract.
- Package insertion copies existing `iinf` entries and assigns fresh IDs.
  Its `iref/cdsc` handling is not managed-family replacement/remapping.
  Untouched `grpl`/`ipma` children do not establish graph-preservation parity.
- ICC rewriting updates `ipco` while copying other `iprp` children; it does
  not implement the C++ property-index remap/multiple-`ipma` consolidation.

Implement and verify in this order:

1. Normalize valid omitted/narrow offsets and narrow lengths when insertion
   requires wider extents. Preserve method-0/1/2 meaning; omitted lengths stay
   unsupported. Fold bases only when all relevant retained extents permit it.
2. Parse the bounded self-contained `dinf/dref` forms. Keep external-resource
   references unsupported; this is not an external I/O feature.
3. Add managed-family replace/strip semantics, then remap unambiguous replaced
   IDs in retained `iref`, version-0 `grpl` and `ipma`. Strip/ambiguous
   replacement removes stale links instead of guessing. Retained method-2
   references must remain resolvable.
4. Remap ICC property indexes and merge multiple supported `ipma` tables.
   Preserve first-seen order, deduplicate associations, OR essential bits and
   retain aggregate table/entry/association and per-item bounds. Reject malformed
   secondary tables. Multiple competing `ipco` tables remain unsupported.

Do not rebuild synthesized `idat`, inserted 32-bit IDs or the existing method-2
foundation. Use `omc_transfer_package_bmff_materialize()` and its persisted-byte
counterpart for target-aware merge. Generic deserialize/materialize must not
acquire hidden target parsing or file I/O.

Test replace/strip separately from append, and direct execution separately from
package materialization. Verify unrelated image boxes, properties/groups/
relations, source-range offsets, essential bits and malformed rejection.
Include HEIF, AVIF and CR3 fixtures; one HEIF roundtrip does not accept all
paths. Add explicit accepted self-contained version-0 `url `/`urn ` data-reference
fixtures and rejected external-reference fixtures. Lock retained `iref`, `grpl`
and `ipma` semantics after managed replacement. Test ICC property-index remapping
independently of multiple-`ipma` consolidation so one cannot mask the other's
failure. No arbitrary scene graph editing is planned.

### B4: Add Positional Input Without A Second Decoder Stack

Port the small C++ source contract as a flat descriptor: fixed size, context,
synchronous `read_at`, explicit status, per-operation limits/accounting,
source-relative ranges and caller-owned windows. Start with a memory adapter.
Test short reads, cancellation, source changes, overflow and budgets before
converting formats. The callback is a narrow I/O boundary.

Convert in bounded batches:

1. TIFF/BigTIFF/DNG and JPEG, including payload offsets and the first supported
   MakerNote source layouts.
2. PNG/WebP/JP2/JXL/BMFF scanners and logical payload extraction, including
   multipart/compressed metadata with explicit scratch limits.
3. GIF/EXR and native RAW readers, then remaining source-relative MakerNotes.

Keep shared decode/validation logic and contiguous fast paths. Never implement
callback support by silently reading the complete source into memory. Record
bytes requested, calls, peak scratch, residual/skipped nested payloads and time
on fixed fixtures. C++ itself has positional enrichment residuals; preserve
explicit unsupported outcomes rather than guessing offset bases.
Snapshot ownership/serialization and rich diagnostics are not prerequisites.

### B5: Evaluate C As A C++ Implementation Core

After accepting a useful slice, build an isolated C++ consumer that delegates
one operation, preferably canonical serialization or a bounded scanner, to C.
Compare values/bytes, errors, allocations, copies, peak memory and throughput
against pinned native C++ on identical fixed fixtures.

Determine whether borrowed views suffice before adding conversion copies or
freezing a shared ABI. Keep RAII and rich APIs above the C boundary. If a call
needs repeated full-store copies, resolve ownership/layout costs before
replacing production code.

Add a C fixed-width patch primitive only when repeated-frame measurements or
the consumer requires it. Preserve plan-scoped handles, typed/width/alias
validation, atomic patches, immutable plans and per-worker storage. Do not
expose private TIFF offsets as a shortcut. A successful experiment supports a
later migration decision; it is not authorization to switch C++.

## Acceptance And Ongoing Upstream Tracking

First milestone: a **bounded writer core**, with B0-B3 accepted for selected
cases, including safety, XMP lifecycle, native translation and package
interoperation. The second adds **positional reads** through B4. Neither
requires rich C++ query, search or adapter parity.

For each completed batch:

- Record the exact two revisions, supported slice and meaningful direct/
  differential results. Test rejection as well as successful emission.
  Historical review findings require current-code verification.
- Run relevant direct tests and affected parity cases, then the Release suite.
  Run sanitizers for touched parsing, ownership and write code. Expand to
  Debug/RelWithDebInfo/MinSizeRel, compression variants, shared-library consumers,
  native Windows and 32-bit compatibility at applicable portability/release
  milestones. WSL does not accept Windows behavior.
- Test both directions of `OMTPLD01`/`OMTPKG01` interoperation, including
  output/source offsets, input/output sizes, block indexes, semantic routes and
  bounded rejection. Keep JXL ICC handoff distinct from file rewrite.
- Keep external validation tools optional and separate from library
  dependencies and distributable artifacts, as required by the dependency
  policy. Check enabled/disabled optional backends when affected. Public
  regressions use independently constructed fixtures; private corpus checks
  remain separate milestones with private artifacts.
- Measure changed hot paths or the reuse experiment on the same fixed fixture
  set, feature flags and compiler/ABI. Report preparation separately from
  execution, including memory/copy costs.

When C++ advances, inspect its public change list, contract, implementation and
tests. Classify relevant changes as shared-core correctness/safety, additive
core features, or C++-layer features. Safety corrections take priority;
additions join the appropriate bounded batch. Do not restart the roadmap or
widen C merely because a new convenience API appears.

## Source And Test Map

C++ paths refer to the public OpenMeta repository at the revision above.

| Area | C entry points | C++ reference entry points |
| --- | --- | --- |
| Model/transactions | [omc_val.h](src/omc/omc_val.h), [omc_edit.c](src/core/omc_edit.c), [store tests](tests/test_omc_store.c), [edit tests](tests/test_omc_edit.c) | `src/openmeta/metadata_authoring.cc`, `src/openmeta/metadata_store_validate.cc`, `docs/generic_authoring.md` |
| Canonical serialization | [omc_exif_write.c](src/edit/omc_exif_write.c), [transfer tests](tests/test_omc_transfer.c) | `src/include/openmeta/exif_tiff_serialize.h`, implementation in `src/openmeta/metadata_transfer.cc`, `tests/metadata_authoring_serialize_test.cc`, `docs/canonical_serialization.md` |
| Translation/projection | [omc_xmp_dump.c](src/edit/omc_xmp_dump.c), [XMP tests](tests/test_omc_xmp_dump.c), [omc_transfer.c](src/edit/omc_transfer.c) | `src/openmeta/metadata_translation*.cc`, `tests/metadata_translation_test.cc`, `docs/translation.md`, `docs/xmp_sync_policy.md` |
| BMFF/packages/safety | [package API](src/omc/omc_transfer_package.h), [package tests](tests/test_omc_transfer_package.c), [diagnostic tests](tests/test_omc_transfer_diagnostics.c) | `src/openmeta/metadata_transfer.cc`, `tests/metadata_transfer_api_test.cc`, `docs/writer_target_contract.md` |
| Positional read | [scan API](src/omc/omc_scan.h), [read API](src/omc/omc_read.h), [payload API](src/omc/omc_pay.h) | `src/include/openmeta/random_access_source.h`, `src/openmeta/random_access_source.cc`, `tests/random_access_source_test.cc`, `docs/random_access_input.md` |
| Differential gate | [parity tests](tests/test_omc_parity.cc), [test configuration](tests/CMakeLists.txt) | `docs/development.md`, `docs/api_stability.md`, public format tests |
| Conditional patch/reuse | Existing C payload/package views and replay | `src/include/openmeta/exif_tiff_patch.h`, implementation in `src/openmeta/metadata_transfer.cc`, `docs/canonical_patching.md` |

Immediate action: execute B0, then the first B1 slice. Do not begin by porting
rich C++ APIs or replacing the already implemented C BMFF insertion foundation.
