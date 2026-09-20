# OpenMeta-c Porting Plan

Updated: 2026-09-20.

## Goal And Reference Baseline

Build a standalone C89/C90 metadata-processing library with substantially the
same portable core features as OpenMeta C++. Reading, decoding, interpretation,
structured non-fuzzy queries, concept resolution, creation, editing,
translation, transfer, and writing are in scope. Embedded-device use and
possible reuse underneath C++ are design goals.

Match metadata behavior through C-native APIs, not C++ class layouts. Owning
convenience objects, presentation, host SDK adapters and language bindings may
remain in C++. Fuzzy search is excluded. Native DNG metadata processing remains
in scope independently of the excluded Adobe DNG SDK integration.

This scope clarification supersedes the earlier restriction to read/write
primitives and safety-only interpretation. Rich metadata semantics are not
excluded merely because their current C++ API owns vectors or strings.
Temporary implementation overlap is expected while C catches up; later C++
reuse is incremental and must preserve behavior and acceptable resource cost.

The initial comparison used these public source snapshots:

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
The conversational estimate of about 75% used the narrower boundary and is
not a completion estimate for this clarified scope. Define feature cases for
interpretation and non-fuzzy query before publishing revised percentages.

## Upstream 0.5.10 Convergence Roadmap

This section supersedes the earlier next-batch order and the conditional
EXIF-only patch proposal. Completed B0-B4, RD0-RD6 and IPTC work remains the
foundation. The C++ reference has moved from 0.5.2 to 0.5.10, so the feature
inventory and the next C batches are refreshed here. This section started as a
source-reviewed plan; the 2026-09-20 execution now has a fresh committed-
reference baseline and focused UP1/primary-GPS results. The older accepted C
results below still belong to the 0.4.132 reference.

| Role | Version / revision | Treatment |
| --- | --- | --- |
| Current C implementation | 0.10.0, `58326d440d8fd7dd5288fdda83969246a8875efc` | Retain existing C APIs, defaults and acceptance evidence; the current working batch adds the first 0.5.10 GPS/UP1 slice |
| Last accepted C++ reference | 0.4.132, `7f0ec70617d7286a33b8d821c459241245520615` | Keep its source/library and regression results reproducible |
| Previous reviewed C++ target | 0.5.2, `646cc2773f9cb690ededb2a60639687a47d7b0c7` | Retain as the intermediate review pin and migration history |
| Current reviewed C++ target | 0.5.10, `8594030c5acf0bb930a02c13c874ae25845f087f` | Freeze this committed source/build for UP0; public tree was clean at review |
| Next upstream family | EXIF 3.1 development/correction fields `A40D`-`A412` | Track as a new UP4 slice; do not claim C parity until its C contract is implemented and tested |

The review uses the committed range `7f0ec706..8594030c`, including the breaking
patch release `561bab7`, container corrections `8b13f7b`, identity and capture
writeback through `646cc27`, and the 0.5.3-0.5.10 authoring and validation
families. Reference documents are `docs/migration_0_5.md`,
`docs/canonical_patching.md`, `docs/api_stability.md`,
`docs/host_adoption_profile.md`, `docs/translation.md`, and
`docs/writer_target_contract.md` in the C++ repository. Uncommitted C++ changes
must not be mixed into a reference build or described as released behavior.

### C++ 0.5.3-0.5.10 feature inventory

The following committed C++ changes are the current C-port input. They are
listed as behavior families so each C batch can have a bounded API, fixture set
and file-readback gate.

| C++ release | Added or completed behavior | C-port treatment |
| --- | --- | --- |
| 0.5.3 | Transactional APEX writeback with exact portable fractions and explicit unknown-brightness handling | Port in UP4 after UP2. Preserve exact numerator/denominator rules and old-packet regeneration requirements. |
| 0.5.4 | Focal-plane and subject metadata writeback with transaction failure preservation | Port in UP4 with native type/shape validation and companion-field rules. |
| 0.5.5 | Exact capture XMP values and managed sensitivity aliases | Share the C translation and native-writeback validation rules; do not trim or reinterpret exact source spelling. |
| 0.5.6 | Typed Add/Set/Remove editing and capture-environment writeback (`A405`, `A300`, `A301`, `9400`-`9405`) | Port the bounded typed transaction contract. Keep caller-owned storage and explicit status codes in C. |
| 0.5.7 | Encoding and composite capture writeback (`A500`, `9101`, `9102`, `A460`-`A462`) with byte-order preservation | Port exact scalar/byte-array serialization and composite companion/removal rules. |
| 0.5.8 | Structured capture writeback (`8828`, `A20C`, `A302`, `A40B`) and XMP text preservation | Port as a separate structured-capture slice. Keep this distinct from structured IPTC location construction. |
| 0.5.9 | Version-aware EXIF text and UserComment writeback, including type-129 and UTF-16/UTF-8 handling | Port bounded text decoding/serialization and explicit version policy; do not add automatic EXIF-version rewriting. |
| 0.5.10 | Shared validation for 64 reverse-translation targets, five legal Interop fields and three structural pointers; complete 32-tag GPS schema validation | Make the validation table a C source of truth for UP1/UP3/UP4. Reject malformed known native shapes without dropping valid existing XMP fallback. |

The C++ 0.5.10 handoff records 75 distinct capture tags across 16 authoring
APIs and 32 standard GPS tag IDs. It also records seven grouped regressions,
canonical serialization rollback, strict detached validation, and combined
capture/GPS fixtures. These figures describe the C++ reference denominator;
they do not convert into C completion percentages. The C port remains at its
accepted 0.10.0 implementation and has not accepted any of these newer
families.

### Breaking Changes And C Consequences

| C++ change | Consequence for the C port |
| --- | --- |
| `exif_tiff_patch.h` and `ExifTiffPatch*` removed without aliases | Replace the old W12/B5 target with `metadata_patch.h`. C has no equivalent old patch API to migrate; do not implement it first or add obsolete wrappers. |
| One `PreparedMetadataPatchPlan` / `PreparedMetadataPatchInstance` supports canonical EXIF and scalar XMP | Plan one C primitive for both payloads, with a shared all-or-nothing update batch. Existing transfer payload/package replay is not an equivalent patch worker. |
| `options.serialization` becomes `options.exif`; validation is shared; payload access takes a family | New C options separate EXIF serialization and XMP output policy while sharing validation, requests, handles and transaction results. Exact C names/layouts are an implementation decision. |
| Required host-issued nonzero 48-bit `plan_id`; no patch atomics, mutexes or global ID allocator | Preserve generation identity and explicit host synchronization. Zero/default ID is invalid; never silently allocate or wrap IDs. Do not require a C threads runtime. |
| ABI 2 becomes 3; `openmeta-3.dll`; CMake changes to same-minor compatibility | Rebuild C++ parity consumers against a matched 0.5 SDK. Do not mix old headers/libraries or copy the C++ ABI number into C. A C ABI/layout change needs its own release decision. |
| Host Adoption Profile v1 and target-specific Prepared Transfer Handoff remain separate | Preserve their portable read/state/diagnostic/replay semantics as C targets. C++ owning wrappers remain above C; standalone patching does not replace container operations or imply profile compatibility. |
| Translation APIs grow without widening existing option defaults | Add bounded C group domains and explicit status mappings; keep `omc_translate_xmp()` and the location/IPTC APIs stable. Do not silently extend their default masks. |

No existing C parity adapter includes the removed patch header. The immediate
compatibility task is reference/package qualification and new adapters, not a
mechanical rename in current C runtime code. Compare new error distinctions
explicitly, including incomplete/unsupported source shapes, precision, versions,
and encoding. Preserve the C distinct-source/output rule; any intentional API
ownership difference belongs in the fixture mapping rather than being hidden.

### Ordered Implementation Batches

| Batch | Work | Exit gate |
| --- | --- | --- |
| UP0: reference refresh | Freeze committed C++ 0.5.10; rebuild Clang 20 direct/parity targets in fresh directories; verify package/contract detection and the 0.5.10 schema counts. Retain 0.4.132 and 0.5.2 as history. | Record exact versions, source/build pairing, ABI, discovered tests, dependency flags and every difference. Separate compile/link failures from changed metadata behavior; never update expected results solely to make a test pass. |
| UP1: existing behavior corrections | Audit TIFF/BigTIFF malformed roots, JP2/JPH EXIF/XMP UUID replacement, terminal/extended boxes, TIFF deletion propagation, and scoped XMP whitespace/projection changes. Fix only demonstrated C differences. | **Started:** C++-matching classic/BigTIFF root outcomes pass through contiguous and callback C paths; scoped camera/lens/spectral whitespace now matches. JP2/JPH and deletion checks remain open. |
| UP2: unified prepared patch core | Implement the current EXIF/scalar-XMP contract through C data, explicit lifecycle and bounded preparation/worker storage. Reuse canonical serializers, typed values and validation. | Mixed-family rollback, generation/alias checks, exact escaped widths, stable payload storage, independent worker lifetimes, and zero allocations in successful/rejected patch, payload access and library replay. |
| UP3: complete GPS families | Primary position/altitude, UTC/navigation, destination, receiver quality, then encoded GPS text. Share exact rational, version, companion and conflict rules. | Per-family paired fixtures and JPEG/Classic TIFF/BigTIFF persistence cover the reference's 32 standard GPS tag IDs, including removals and companions. Projection/encoding/unit deltas travel with each group. |
| UP4: capture and identity convergence | Port the committed APEX, focal-plane/subject, capture-rational, Flash, LightSource, sensitivity, camera/lens/spectral text, LensSpecification, ImageUniqueID, environment, encoding, composite, structured-capture and UserComment groups. Then add the six EXIF 3.1 development/correction fields `A40D`-`A412`. Preserve existing basic capture mappings. | Coherent groups agree on types, aliases, ambiguity, sentinels, bounds and transaction failure. Validate, serialize, project, persist and reread each group; do not infer round-trip correctness from translation alone. |
| UP5: structured locations | Port reconciliation and explicit flat-to-structured construction separately. Preserve Created/Shown scope, dense record indexing, RDF Bag output and removal policies. | Paired structured fixtures retain unrelated records/qualifiers; malformed, mixed and partial inputs reject atomically. Flat IPTC writeback remains an independent operation. |
| UP6: snapshot and transfer convergence | Add bounded decoded-state persistence/reconciliation semantics and close the recorded broad transfer/lifecycle gaps by carrier. Keep raw-carrier entry links and tombstones across deferred operations. | Transactional snapshot v1 interoperation where promised, stable entry identity/order, explicit completion diagnostics, selected-family replacement/removal and actual file readback. C++ integration-profile compatibility requires its own consumer gate. |
| UP7: rich semantic stages | Continue I1/I2/Q1/Q2 by metadata family, sharing accepted normalization and applicability rules with translation/transfer. | Non-fuzzy interpretation, candidates, provenance, confidence, preference/conflict and unknown outcomes match the pinned reference. |

Default order is **UP0 -> UP1 -> UP2 -> UP3 -> UP4 -> UP5 -> UP6 -> UP7**.
UP2 and the translation families have no dependency on each other's completion;
the default brings the replacement patch contract forward before further
per-tag expansion. GPS remains the first translation batch after the patch
contract. Split UP3/UP4 into the listed coherent groups so each can be committed
and verified independently. The EXIF 3.1 development/correction family follows
the already committed capture groups; it is not a reason to defer GPS or the
patch primitive.
Do not build a general synchronization engine as a prerequisite.

Apply E1 resource checks to every changed module and qualify one declared
embedded configuration separately. Snapshot serialization is not a prerequisite
for initial patch or translation tests: use existing C stores and native file
readback first. Neither OIIO/iRAW integration nor an external checkout is a gate
for these C-library batches. The optional B5 C++-uses-C experiment follows an
accepted primitive and does not authorize changing C++ production dispatch.

### UP1 And Translation Drift Inventory

These are behavior changes to test, not blanket claims that every C path is
broken. The C TIFF scanner already checks the first IFD range; test the direct
EXIF decoder and positional entry points independently.

- C++ 0.5.1 rejects an out-of-range nonzero root IFD as Malformed. Zero remains
  an empty root. Preserve this distinction for classic TIFF, BigTIFF, both
  byte orders and nonzero positional source bases.
- JP2/JPH replacement removes all selected EXIF/XMP top-level and standard UUID
  carriers. Preserve unknown, IPTC, GeoTIFF and unselected UUIDs. Check extended
  size overflow, truncated UUIDs and insertion before a preserved size-zero
  final box. Boxed JPH metadata belongs to the existing JP2-family core scope;
  a distinct format enum is not a prerequisite. Raw codestream wrapping and
  pixel codecs remain host responsibilities.
- Test deletion of the last ExifIFD/GPS fields through actual TIFF/BigTIFF
  output and XMP stripping. Ordinary omission must still preserve target data.
  C++ snapshot deletion-marker handling is a reference for UP6, not an existing
  C snapshot capability.
- The six camera/lens/spectral text fields from 0.4.142, plus ImageUniqueID in
  0.5.2, preserve boundary whitespace in their exact namespaces/aliases. C's
  XMP decoder still uses general trimming paths. Cover description attributes,
  `rdf:resource` and element text; do not disable trimming globally.
- Bring canonical exifEX names and historical aliases into projection tests
  for sensitivity and camera/lens fields. LensSpecification uses exact fraction
  output, four rational components and narrowly defined unknown-aperture `0/0`
  slots. Generic validation must agree without relaxing all rational checks.
  ImageUniqueID requires 32 ASCII hex characters and preserves case/zeros.
- GPS destination distance unit N means nautical miles; the historical Knots
  spelling is only a distance-input alias. GPS speed unit N remains knots.
  GPS processing/area text projection must decode only supported encodings and
  retain unsupported native values without inventing display text.
- LightSource codes 1 and 25 must not collapse to the same reversible label.
  Preserve finite values versus sentinels in capture rationals, full Flash bit
  structure and sensitivity companion relationships in UP4.

### UP2 Contract And Resource Gates

The reusable behavior is in `metadata_patch.h` / `metadata_patch.cc`, with
serializer-recorded XMP slots and tests in `metadata_patch_xmp_test.cc`,
`exif_tiff_patch_test.cc` (its filename is historical) and
`metadata_patch_allocation_test.cc`. Port that contract without C++ owner
classes or application-visible TIFF/XML offsets.

- Prepare after validation and serialization. Bind EXIF by key, occurrence and
  native shape; bind XMP by emitted namespace URI/simple property identity after
  conflict resolution. Do not locate patch slots by sentinels or reparsing XML
  during execution. Only requested payload families are generated.
- Make preparation/worker capacity queries, ownership and teardown explicit.
  Prefer caller-provided storage for the C execution primitive; any allocating
  setup convenience must be explicit. Immutable plan data and independent
  worker payload/slot storage must permit workers to survive plan destruction.
  Existing C store/read allocations need separate E1 work.
- Follow the host-issued ID lifetime contract, including 48-bit exhaustion.
  Reject foreign/stale handles when IDs obey that contract; document that the
  library cannot detect a host assigning the same ID to different live plans.
  Do not serialize process-local handles in snapshots.
- Validate the full update batch before either payload changes. Reject
  duplicate handles and values aliasing either worker payload. Failed prepare
  or worker creation preserves the previous output and output handles.
- EXIF retains compiled kind/type/count/encoding and canonical little-endian
  storage. XMP takes logical ASCII/UTF-8, validates XML characters and measures
  escaped width, including `&amp;` and CR as `&#xD;`. Preserve leading zeros and
  fraction/subsecond spelling; reject padding, truncation, raw XML, structural
  paths, additions and resizing. Respect per-API rational/sentinel rules.
- Retain documented reference ceilings: 4096 default requests, 65534 hard
  handles, 64 MiB default EXIF and 16 MiB/65536-entry default XMP output bounds.
  C callers may choose smaller limits. Oversized preparation must not publish
  a partial plan. Freeze exact defaults with the implementation's reference.
- Verify stable payload addresses/lengths and zero allocations on successful
  and rejected patching, payload access and library replay. Replay is synchronous
  EXIF then XMP. Callback failure stops replay; prior host output effects are
  not rolled back. The host owns synchronization, framing, checksums and I/O.
- Include installed shared-library consumers, C90/Clang 20, sanitizers and
  native Windows x64/Win32. Measure preparation separately from patch/replay;
  record C-heap interception coverage rather than assuming a C++ allocation
  counter measures C. No workstation timing constitutes an embedded deadline.

### Current Upstream Watch List

UP0 is complete for the pinned commit. The first UP1 correction checks and the
primary UP3 GPS slice are implemented in the current working batch. The C++
0.5.10 source is committed and reviewed. The next upstream family
is the six EXIF 3.1 development/correction fields `A40D`-`A412`:
`DevelopmentType`, `DevelopmentTypeDescription`, `DistortionCorrection`,
`ChromaticAberrationCorrection`, `ShadingCorrection` and `NoiseReduction`.
Preserve the intentional `DevelopmentCharacterstic` XMP spelling, the type-129
description encoding, and an explicit host policy for the 0300/0310
`ExifVersion` source discrepancy. `LearningOptOutIn` (9287) and complete-file
mandatory profiles remain separate work. Recheck the handoff and commit history
before each batch, because a newer committed C++ version changes the reference
pin but does not automatically change C behavior.

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
cases still differ. The original default parity inventory contained nine BMFF fixtures with
primitive and richer summary differences. Version 0.8.0 closes those reports;
see the current checkpoint below for the remaining broad inventory.

See [authoring.md](authoring.md) for contracts and bounded coverage. The matrix
below retains source-review detail where broader acceptance remains open.

## Current Translation Checkpoint: Version 0.10.0

Batch 1 uses a frozen C++ 0.4.132 source and library at commit `7f0ec70`.
The fresh C 0.9.0 direct baseline passed 39/39 tests. Its default parity suite
exposed one shared projection difference: GPSVersionID emitted only its first
byte. This batch ports the four-component formatting rule from the reference.
The focused frozen C++ IPTC/transfer suite passed 48/48 cases.

`omc_translate_xmp_iptc()` combines all 20 flat IPTC groups in one bounded,
output-preserving transaction. It adds Headline, Instructions,
TransmissionReference, AuthorsPosition, CaptionWriter, Category,
SupplementalCategories and Urgency. The location API shares its planning engine;
legacy defaults remain unchanged. Repeated creators, keywords and supplemental
categories retain numeric index order through native updates and growth.
Native projection includes missing fields and corrects Country/CountryCode.
See [authoring.md](authoring.md) for exact limits and mask domains.

Verification passes 54/54 Clang 20 Release targets in both static/compression
and shared/no-compression builds, including 119 paired IPTC fixtures. The direct
combined target covers 120 fixtures plus six legacy ordering variants. Existing
70 location fixtures remain. ASan/UBSan and native MSVC x64/Win32 each pass
40/40 direct targets. Four new JPEG/TIFF replacement/removal cases cover all
20 groups, repeated growth, unowned data and stale raw IRB precedence. Native
Windows retains existing CRT/decoder warnings, with none from the new module.

At this checkpoint primary GPS was the next batch. The Upstream 0.5
Convergence Roadmap above now inserts UP0/UP1 and unified patching first; GPS
remains the next translation family. This checkpoint does not establish new
corpus, performance, embedded-device or whole-project parity.

## Current 0.5.10 convergence work: UP0, UP1 and primary GPS

UP0 now uses an isolated detached worktree at C++ commit
`8594030c5acf0bb930a02c13c874ae25845f087f` (version 0.5.10) and a fresh
Clang 20/libc++ build in `/tmp/openmeta-cpp-baseline-20260920-libcxx`.
The five C++ CTest targets and the focused `MetadataGpsTranslation.*` plus
`MetadataStandardValidation.*` run pass (22 tests). The installed dependency
set is zlib, Brotli and Expat; RapidFuzz, Adobe DNG, C2PA and OpenSSL are not
part of this baseline. The dirty main C++ checkout, including later EXIF 3.1
correction fields, is excluded from the reference.

The first UP1 slice adds `omc_test_up1`: classic and BigTIFF zero roots decode
as empty and nonzero out-of-range roots return Malformed through contiguous and
callback C paths. The same fixture covers the demonstrated XMP correction for
six camera/lens/spectral text properties in both EXIF and CIPA namespaces. Only
those scoped values retain boundary whitespace; unrelated text remains trimmed.
The existing C decoder already matched these root outcomes in the contiguous
path, so the code correction is limited to XMP value projection.

The first UP3 slice adds `omc_translate_xmp_gps()` for primary latitude,
longitude and altitude pairs. It has exact DMS/rational parsing, GPS 2.3/2.4
version handling, duplicate/incomplete source checks, conflict policies,
bounded limits, tombstone removal, output-preserving transactions and canonical
TIFF readback. `omc_test_gps_parity` compares primary and altitude-only cases
with the pinned C++ implementation. Navigation, destination, receiver/quality
and encoded GPS text remain later UP3 groups.

The fresh C tree passes 49 of 57 CTest targets. The eight failures are the
pre-existing broad inventory targets (`omc_test_parity`, remaining-source,
read/callback inventory and BMFF/box/TIFF source parity); their reported
0.5.10 deltas are LensSpecification namespace/fractions, rational display and
GPS destination-unit projection plus broader inventory changes. They are kept
as residuals and were not re-baselined. The new GPS, UP1, XMP and focused parity
targets pass.

## Previous Translation Checkpoint: Version 0.9.0

The 0.9.0 batch was pinned to C++ 0.4.128, commit
`ba99484b8be087012f9c44a1194ed828d060a0d5`. A frozen source snapshot and matching
library keep this gate independent of ongoing C++ changes. The fresh C 0.8.0
baseline passed 50/50 targets against that reference before implementation.

`omc_translate_xmp_location()` now covers five explicit flat IPTC mappings:
City, Location, State, Country and CountryCode. It uses the existing edit
transaction and preserves the legacy translation API defaults. Its fixed
planning records enforce group eligibility, ambiguity, native wire ordering,
conflicts, removals, provenance, UTF-8 promotion and cumulative resource limits.
See [authoring.md](authoring.md) for exact properties and API contracts.

The direct gate covers 70 fixtures; 69 have paired C++ cases. The additional
case rejects a C mapping bit with no equivalent C++ boolean option. Four
JPEG/TIFF replacement/removal cases persist files and reread native datasets,
including stale raw IRB precedence and unrelated caption preservation.

The Clang 20 Release static build with zlib/Brotli and the shared build without
them each pass 52/52 targets. Debug ASan/UBSan passes 39/39 direct targets.
Native MSVC Release x64 and Win32 each pass 39/39 without optional compression.
These Windows builds are functional gates, not warning-clean builds; existing
CRT and decoder warnings remain, with none from the new location module.
These gates retain the public reading inventories against the new reference.
They do not establish new corpus, performance or whole-project parity results.
The 0.8.0 resource and broad-transfer findings below remain open work.

## Reading/Decoding Checkpoint: Version 0.8.0

The [reading/decoding plan](read_decode_parity.md) records all RD0–RD6 batches
against C++ 0.4.127. RD5 adds the remaining bounded BMFF scene/derived-image
summaries, structured XMP, JUMBF semantics and corpus-discovered MakerNote fixes.
CMT3, standalone XMP and sparse Phase One metadata share positional input paths.
RD6 adds a reproducible corpus runner, native Windows x64/Win32 and resource
measurements. Rich interpretation and non-fuzzy queries remain in the C target.

Release with and without zlib/Brotli passes 50/50 targets; sanitizer and native
Windows x64/Win32 each pass 38/38 direct targets. The reading inventory matches
208/208 cases in each access mode; the BMFF subset matches 68/68 and ordered XMP
matches 35/35. The selected 69-file corpus matches 138/138 memory/callback runs.
The historical default parity target now passes. The broad transfer/persist
inventory retains 257 reports and remains separate from reading-stage progress.

These are measured case results, not universal stage percentages. Uncovered
vendor/model variants, complete C2PA verification and Unicode XML-name/UTF-16
support remain explicit limits. BMFF currently has a 199832-byte compiled stack
frame, so small-stack embedded acceptance requires a caller-workspace batch.
See the detailed checkpoint for dependency, corpus, platform and resource scope.
Version 0.8.0 extends `omc_xmp_limits`; consumers must rebuild and initialize
options with the public initializer.

## Prior Core Checkpoint: Version 0.3.0

The five authorized workstreams have an implemented bounded slice: fresh
baselines, typed authoring/validation/canonical EXIF, the five reverse groups
and paired IPTC dates, shared BMFF replacement, and positional input with an
initial JPEG/TIFF/BigTIFF/DNG conversion. This is not complete C++ core parity.

| Current gate | Result |
| --- | --- |
| Clang 20 Release direct tests | 36/36 passed |
| Focused authoring and positional C/C++ differential targets | 2/2 passed |
| Clang 20 Debug ASan/UBSan direct tests | 36/36 passed |
| Clang 20 Release without zlib/Brotli | 36/36 passed; compression-dependent cases remain conditional |
| Pinned C++ reference baseline | 2/2 CTest targets passed |
| Historical default differential inventory | Fails on nine BMFF fixtures |
| Historical `--all` differential inventory | Fails: 266 mismatch reports, compared with 268 at baseline |
| Corpus and native Windows acceptance | Not run in this checkpoint |

These results describe the 2026-09-07 implementation checkpoint. The scope
clarification changes planned coverage, not code or recorded test outcomes.

The `--all` count is an inventory, not a coverage percentage. Its final reports
comprise 91 metadata comparisons, 116 output-byte comparisons, and 59 status,
output-presence, or sidecar-path comparisons. Richer metadata summaries must
now be reviewed as semantic parity work; do not dismiss them as C++-only.
Separate presentation-only differences and excluded fuzzy/SDK behavior from
real lifecycle, metadata and interpretation gaps.
The new focused gates test the added APIs independently and do not hide these
reports. Whole-file BMFF byte identity is not promised by the append layout.

Use `ctest --test-dir <c-build> --output-on-failure -E '^omc_test_parity$'` for
the direct and focused passing gates. Run `omc_test_parity` and
`omc_test_parity --all` separately to retain the broad inventory. The dedicated
modes are `--core-authoring` and `--core-source`.

Positional callback reads currently cover JPEG and bounded TIFF/BigTIFF/DNG
metadata collection. TIFF values must fit the caller's TIFF-relative scratch
span; requested callback TIFF MakerNote enrichment returns unsupported.
JPEG MakerNote decoding remains available inside complete EXIF APP payloads.
See [positional_input.md](positional_input.md) for exact limits, I/O counters,
range semantics, and the next reader increments.

## Core Boundary And Decisions

The existing `omc_store`, arenas, scanners, decoders, edits, and transfer code
are the starting point. Extend these mechanisms; do not create a second store,
a general object framework, or a separate writer for each translation group.

| Layer | Intended responsibility |
| --- | --- |
| C portable core | Bounded input, metadata keys/values/origin, decode, semantic interpretation and normalization, structured non-fuzzy query/candidates/concept resolution, provenance/conflict/safety facts, typed validation, explicit edits and translation, canonical payload serialization, bounded carrier rewrite, transfer policy, payload/package wire formats |
| C++ layer | Owning objects and ergonomic wrappers over metadata behavior, presentation, fuzzy search, host adoption and SDK adapters, higher-level scheduling and file workflows, Python and other bindings |
| Host/platform boundary | Storage and I/O policy, image facts, worker scheduling, SDK integration, signing/trust services, publication/privacy policy |

Existing C file/persist helpers remain supported. A reusable algorithm must
also have a memory-oriented entry point; path handling must not become a
dependency of validation, translation, or serialization.

Rules for the proposed common core:

- Preserve flat C APIs, explicit lifetimes, capacities, errors, and resource
  limits. Current targets compile as C90 with a 64-bit integer shim; retain
  that compatibility for the C89/C90 port. A
  language-level change is a separate decision, not a prerequisite here.
- Use setup-time allocation/reservation where the existing API owns storage.
  Make scratch requirements explicit. Do not describe all current C code as
  caller-allocated or allocation-free. Promise allocation-free execution only
  for measured and tested replay/patch paths.
- Keep algorithms and wire contracts reusable without C++ types or a C++
  runtime. C++ can eventually wrap accepted C operations without duplicating
  their algorithms. Independent implementations and overlapping public APIs
  are acceptable during migration; shared differential fixtures limit drift.
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

### Embedded Use

Embedded support changes resource and platform contracts, not the meaning of
enabled metadata features. A constrained build may omit optional modules,
backends or registry families with explicit capability reporting; enabled
operations must retain their documented semantics. Module selection beyond
existing build options is planned work, not an available feature claim.

- Keep interpretation/query outputs in explicit records, source-entry IDs and
  bounded arrays, with documented ownership, capacities and unknown outcomes.
  Preserve confidence and conflict information where the reference defines it;
  these data do not require C++ objects or fuzzy matching.
- Separate filesystem, threads, SDKs and host policy from memory/source-based
  metadata operations. Do not require these services for semantic queries.
- Make workspace, allocation, depth and I/O limits explicit. Extend caller
  storage or allocator control where needed; existing store/decoder allocation
  and sizable parser stacks still need an embedded resource audit.
- Validate each supported toolchain and target data model. Current scalar
  types require 16-bit short, 32-bit int and a usable 64-bit integer shim.
  C89 syntax alone does not establish support for every MCU or freestanding
  environment. Existing desktop tests are not embedded acceptance.
- Measure code/read-only-table size, maximum stack, peak scratch/heap, I/O and
  execution cost on fixed cases. Record the enabled feature set and target.
  Include capacity rejection, allocation failure, malformed input and the
  dependency-disabled configuration in the relevant acceptance gates.

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

The matrix combines the C 0.10.0 implementation evidence with the 0.5.10
source review and UP0-UP7 plan. Earlier measured rows retain their recorded
reference pins; they have not been reaccepted against 0.5.10. Presence alone
does not establish complete reference parity:

- **Present**: implementation and direct tests exist for a bounded C contract.
  Broader reference parity needs the listed acceptance cases.
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
| R2 | Contiguous scan, payload assembly, decode | `omc_scan.h`, `omc_pay.h`, `omc_read.h`; direct EXIF/XMP/ICC/IPTC/IRB/JUMBF/EXR tests | Present, broad bounded coverage under the old pin. UP1 checks 0.5.1 TIFF root outcomes and scoped XMP whitespace changes; no universal camera/read-parity claim. |
| R3 | MakerNote/native RAW and modern-container enrichment | `src/read/omc_exif.c`, `omc_bmff.c`, naming and read tests | Bounded parity verified against the pin for the public inventory and selected corpus, including CMT3, vendor offsets and model-selected derived tables. Uncovered model/subtable variants remain planned; preserve raw/unknown values. |
| R4 | BMFF derived fields | Item semantics, properties, `ipma` associations and `grpl` summaries in `omc_bmff.c` | Implemented bounded scene/component, grid/overlay/identity/tile, primary/display-transform and property/reference summaries. 68 memory and callback cases match the pin. Read-side summaries do not imply writer remapping or unbounded graph support. |
| R5 | Positional source, read budgets, source ranges/windows | `omc_source.h`, `omc_read_source.h`, direct and focused C++ source tests | Implemented fixed-size memory/callback sources, exact reads, sticky budgets, shared scanners/payload extraction, direct TIFF/MakerNote values, modern containers, native RAW and standalone XMP. Explicit undeclared RAW-search and embedded-memory limits remain; see `read_decode_parity.md`. |
| R6 | Runtime capabilities, preview, CCM/DNG helpers | `omc_capabilities.h`, `omc_preview.h`, `omc_ccm_query.h` and direct tests | Present bounded helpers. Capabilities must report actual C support and enabled compression features. |
| R7 | Detached entry/store validation | `omc_store_validate.h`; bounded diagnostics, wire/value checks, singleton and image-context tests | Present initial schema. UP1 adopts the C++ 0.5.10 shared validation table for 64 reverse targets, five legal Interop fields and three structural pointers. UP4 adds the exact LensSpecification sentinel and related field-shape rules; preserve ordinary denominator rejection. |
| R8 | Decoded snapshots, source provenance and persistence | C callers retain stores/bytes; transfer packages retain output source ranges | Missing named decoded-state snapshot API; planned UP6. Preserve v1 wire/state semantics, entry identity and raw-carrier links through C lifecycle functions. Existing readers and UP2 do not depend on it. |

### Interpretation And Non-Fuzzy Query

These rows are part of the C completion denominator. Their scope is the
portable behavior in the pinned C++ reference, including rich metadata
semantics. C++ ownership and presentation APIs need not be reproduced.

| ID | Capability | Current C evidence | Status and decision |
| --- | --- | --- | --- |
| I1 | Tag meaning, units, enums, normalization and vendor semantics | EXIF/MakerNote naming and decode, BMFF facts, DNG/CCM fields | Partial. Inventory exact values, units, shapes and explicit unknowns by semantic family; names alone do not establish interpretation parity. |
| I2 | Structured interpretation records | Bounded CCM collection and selected derived metadata | Partial foundations; general equivalent record API is missing. Port normalized records and their source-entry relationships using C data and explicit storage. |
| Q1 | Non-fuzzy semantic queries and candidates | Bounded `omc_ccm_collect_fields()`; general query API is missing | Partial subset. Port exact/registered-alias matching, normalized candidates, confidence and provenance. Fuzzy fallback remains excluded and must be disabled in the reference comparison. |
| Q2 | Concept resolution and metadata applicability | Existing target-image facts and transfer diagnostics | Partial foundations; general concept API is missing. Port deterministic preference/conflict rules, structured concepts and RAW/transfer applicability. Retain ambiguity and unknown results; application policy remains explicit. |

### Creation, Translation, And Writing

| ID | Capability | C++ reference and current C evidence | Status and decision |
| --- | --- | --- | --- |
| W1 | Typed authoring | C++ `create_metadata_store()` preflights, copies, validates and publishes atomically. C has typed value makers, explicit array byte order, and candidate-based edit publication. | Implemented bounded typed helpers and output-preserving transactions; see `authoring.md`. Owning logical builders and FlatHost wrappers remain above C; reusable metadata construction and validation semantics remain C targets. |
| W2 | Canonical TIFF/EXIF serialization | C++ `serialize_exif_tiff()` is target-neutral and honors supported wire hints. C's public serializer builds typed TIFF; internal transfer payloads apply target framing above it. | Implemented `omc_serialize_exif_tiff()` with direct tests and exact C++ byte comparison. Carrier wrappers reuse canonical output; TIFF/BigTIFF retain target layout. |
| W3 | EXIF/IPTC to portable XMP | C has projection, conflict policies, custom namespaces and managed canonicalization | Partial against 0.5.10. Retain paired IPTC dates and the 0.10.0 fixes. Primary GPS native-to-XMP projection remains covered; UP1/UP3-UP5 cover exifEX names, exact lens fractions, GPS text/units, 75 capture tags and structured Bags. APEX is committed in the reference and is a C UP4 requirement. |
| W4 | Explicit XMP to native metadata | C exposes original groups, flat locations and combined 20-group IPTC | Partial against 0.5.10; 119 paired IPTC fixtures remain accepted at 0.4.132. UP3 now starts with primary position/altitude GPS; the remaining 32-tag GPS families, UP4 capture/identity groups and the next development/correction family follow. UP5 closes structured locations without widening old defaults. |
| W5 | Native IPTC-IIM emission | Internal `omc_transfer_build_iptc_iim()`; JPEG IRB and TIFF tag `33723` carriers | Present bounded mechanism with 0.10.0 charset, repeated growth, tombstone and stale-IRB persistence tests. Broader transfer remains UP6. A separate public IPTC writer is not a prerequisite. |
| W6 | Target image facts and transfer safety | C has target image spec, CompatibleFile/RenderedImage and diagnostics; C++ has wider source-processing classification and a RAW-data descriptor | Partial. C has no source descriptor or explicit lens/preview/general-processing audit categories. Verify selected fields through actual transfer paths; share classification with interpretation/query as those operations are ported. Full query completion need not block a bounded safety fix. |
| W7 | Prepare, compile, execute, persist | `omc_transfer.h`, `omc_transfer_persist.h` and direct tests | Present bounded pipeline; UP1/UP6 close deletion/lifecycle behavior. UP2 standalone payload patching is separate from target-specific handoff and container replay. |
| W8 | XMP carrier merge and lifecycle | C has destination embedded/sidecar stores, precedence, writeback and persistence options | Present controls, Partial lifecycle parity. Test modes/defaults, strip/overwrite/failure behavior and source/destination conflicts. |
| W9 | Payload/package artifacts | C has `OMTPLD01` v1, `OMTPKG01` v2, semantic views, replay, executed-output materialization and artifact inspection | Present bounded wire families. Test interoperation in both directions; matching version/magic does not establish complete builder/execution parity. |
| W10 | BMFF package item insertion | C has Exif/XMP/JUMBF/C2PA routes, ICC, synthesized `idat`, inserted 32-bit IDs and bounded method-2 references | Shared bounded materializer now replaces managed families and remaps unambiguous IDs. Append layout preserves existing media addresses; physical byte layout differs from C++. |
| W11 | Newer bounded BMFF writer rules | C++ compact `iloc`, self-contained `dref`, managed-item replacement/remapping and multiple `ipma` consolidation | Implemented bounded normalization, local `dref`, family replacement, `iref`/version-0 `grpl`/`ipma` remapping, and multiple-table ICC association consolidation. See `bmff_writing.md` for limits and validation. |
| W12 | Transactional canonical EXIF/scalar-XMP patching | C++ 0.5 replaces the removed EXIF-only API with `metadata_patch.h`; C has serializers but no equivalent patch plan/worker | Missing; planned UP2. One bounded C primitive must preserve mixed-family atomicity, host-issued generations, exact shapes/escaped widths and allocation-free execution. C++ classes remain outside the C API. |
| W13 | MakerNote trust and C2PA | C has conservative rendered filtering and bounded JUMBF/C2PA routes; C++ has richer MakerNote layout audits and optional verification | Partial safety facts. Keep opaque preservation distinct from verified relocation. Bounded OpenSSL verification logic is eligible as an optional C backend. Rendered C2PA invalidation/drop stays explicit; full asset binding, signing and trust remain outside the first writer milestone. |

### Excluded Integrations And Conditional Features

| Feature | Decision |
| --- | --- |
| Owning C++ query/interpretation objects and presentation helpers | C++ layer. Underlying candidates, confidence, provenance, concept resolution and interpretation semantics are C targets in I1/I2/Q1/Q2. |
| Fuzzy search, Unicode/transliteration policy and optional search indexes | C++ layer. Independent capability; not a metadata-core parity gate. |
| Owning logical builders, FlatHost object adapters and typed codec wrappers | C++ layer for object/host integration. Portable metadata construction, validation and reconciliation rules remain C targets; wrapper placement does not exclude their underlying semantics. |
| Host Adoption Profile descriptors, PreparedTransferHandoff and owning C++ plans | C++ wrapper shapes stay above C. Portable read/state/reconciliation and typed replay behavior remain UP6 targets; unified standalone patch semantics are UP2. Neither contract is implied by existing C payload replay. |
| OCIO, EXR host adapters, Adobe DNG SDK and LibRaw integration | C++ layer. EXR header read remains in C; host emission is Conditional, and full EXR file rewrite is outside scope. |
| OIIO adapter | Removed from the current C++ tree. Do not list it as an implemented feature to port; any future bridge is separate integration work. |
| Python/nanobind, CLI feature duplication and downstream host wrappers | C++ layer. A small C diagnostic test tool may be justified without a second product CLI. |
| Snapshot v1 serialization, raw-carrier provenance and deferred state | Planned UP6 as portable core behavior with explicit C ownership. Raw-carrier passthrough policy is separate; no owning C++ class is required. |
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
| PNG / WebP | Bounded EXIF/XMP/ICC lanes and format-aware packages | Canonical EXIF wrapping, preservation, XMP lifecycle and semantic readback |
| JP2-family including boxed JPH | Bounded JP2 EXIF/XMP/ICC lanes | UP1 adds selected UUID-family replacement and terminal/extended-box checks; JPH metadata qualification must prove codestream preservation. |
| JXL | EXIF including replacement of `brob(Exif)`, XMP/JUMBF, serialized encoder ICC handoff | Brotli on/off and wire interoperation; ICC handoff is not an in-place ICC writer |
| HEIF / AVIF / CR3 | Bounded EXIF/XMP/ICC rewrite and explicit package graph materialization | Newer `iloc`/reference/property rules; test direct rewrite and package materialization separately |
| EXR | Header decode | Maintain read parity; host-emitter work does not block writer convergence |

## Original Delivery Foundation (B0-B5)

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
| S1 | Shared semantic classification and normalization | Existing decode/model; stage by metadata family | Meaning, units, numeric shape, source IDs, ambiguity and unknown outcomes match pinned C++ cases |
| S2 | Non-fuzzy query, interpretation records and concept resolution | Accepted S1 families; reuse current store and semantic rules | Candidates, normalized records, preference/conflicts, provenance and applicability match with fuzzy matching disabled |
| E1 | Embedded resource and portability acceptance | Applies to each enabled B/S slice | Target/toolchain, code/table size, stack, scratch/heap and I/O measured; capacity/failure and dependency-disabled cases verified |
| B5 | Optional common-core reuse experiment | Selected B1-B4 or S1/S2 operation accepted; bounded experiment | Isolated C++ consumer delegates one operation to C with behavior/cost evidence; production switch is a separate decision |

The first implementation sequence was **B0 -> B1 -> B2 -> B3 -> B4**. Its
bounded slices and RD0-RD6 are recorded above; do not restart completed work.
The current order is UP0-UP7. Retain [read_decode_parity.md](read_decode_parity.md)
and its decoder-generated semantic fields as a regression inventory. S1/S2
and remaining transfer/lifecycle parity continue through UP6/UP7.
Apply E1 as those modules become candidates for embedded use. B5 can use any
accepted operation and does not gate semantic porting or require full parity.

Small fixture-backed read/name/projection corrections can travel with the
relevant batch. A useful writer milestone need not wait for every semantic
family, but it must not be reported as complete metadata-core parity.
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
  where the reference behavior needs them. Reuse these facts in S1/S2 rather
  than keeping separate rules for transfer and query indefinitely.
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
| B2e: flat IPTC locations (complete in 0.9.0) | City, Location, State, Country, CountryCode from C++ 0.4.128 | Exact groups, dirty/active duplicates, byte limits, code syntax, native wire order, charset budget, failure atomicity and JPEG/TIFF persistence |
| B2f: combined IPTC (complete in 0.10.0) | Eight additional flat fields and atomic 20-group API from frozen C++ 0.4.132 | Repeated index/native rank order, scalar Urgency, wire limits, shared conflicts/charset/budgets, provenance and JPEG/TIFF persistence |

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

The older C++ 0.4.132 pin already had primary GPS and structured location
writeback. UP3-UP5 now cover those contracts and their growth through 0.5.10,
after UP0-UP2. Pin and close one reference batch before advancing it.

### B3: Close Specific BMFF Deltas

Keep the package route/materialization boundary and direct XMP/EXIF/ICC paths.
The shared writer in `src/edit/omc_bmff_rewrite.c` now implements the bounded
rules below. Version 0.2.1 passed 35/35 Release direct plus focused authoring
parity targets and 34/34 ASan/UBSan direct targets. Synthetic table tests cover
one-to-one and ambiguous replacement, retained media offsets, method-2 links,
compact widths, local data references, mixed property tables, essential-bit
merging, invalid secondary indexes, and table ceilings. Direct and package
paths use the same writer. See [bmff_writing.md](bmff_writing.md).

Implemented sequence:

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

The public source contract and initial reader batch are implemented in version
0.3.0; see [positional_input.md](positional_input.md). The following sequence
remains the acceptance checklist for subsequent increments.

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

### S1/S2: Port Rich Metadata Semantics In Bounded Families

First inventory the pinned C++ query, interpretation and concept operations,
their shared classifiers/normalizers and their tests. Keep interpretation and
query as separately tracked stages even where they share implementation.
Do not infer coverage from tag counts or recreate a second metadata store.

1. Establish C records, source-entry references, explicit output/scratch
   capacities and failure contracts using the existing store/value types.
   Translate behavior rather than C++ vectors, strings or ownership graphs.
2. Begin with well-defined families already supported by C decode: orientation,
   geometry, capture values, DNG/CCM and descriptive metadata. Follow actual
   reference dependencies; shared classification and normalization feed query
   candidates, interpretation records and concept resolution.
3. Extend to source-processing/lens/RAW applicability, vendor semantics and
   richer BMFF relationships. Preserve source-declared units and shape beside
   normalized values where the contract requires both. Interpreting a curve
   or LUT does not authorize applying it to pixels.
4. Match non-fuzzy rules, confidence, provenance, candidate preference and
   conflict behavior on shared fixtures. Include incomplete/malformed values,
   duplicate and contradictory sources, ambiguous vendor facts, capacity
   failure and explicit unknown outcomes. Disable fuzzy matching on the C++
   side; do not silently count fuzzy-only results as semantic requirements.
5. Share accepted rules with validation, translation and transfer when their
   contracts agree. Keep interpretation read-only and native translation
   explicit; semantic inference must not silently alter source metadata.

Progress is recorded per family and operation with a defined fixture scope.
An implemented primitive is partial progress toward a rich semantic API, not
proof that every candidate or resolution outcome matches C++.

### E1: Accept A Defined Embedded Configuration

Select and record the target/toolchain and enabled metadata families before
claiming embedded support. Preserve a dependency-disabled, single-threaded
configuration. Audit allocator and workspace paths, fixed parser stacks,
recursion/depth bounds, integer-width/size overflow and required runtime
services. Add caller workspace or allocator control where the selected paths
need it, without replacing the existing store architecture.

Measure the resource quantities listed under Embedded Use on fixed valid and
malformed fixtures. Verify the selected compiler/data model and, where
available, actual target or emulator execution. A host build or cross-compile
alone does not establish target runtime behavior. Record limits per supported
configuration; avoid a blanket claim for all embedded devices.

### B5: Evaluate C As A C++ Implementation Core

After accepting a useful slice, build an isolated C++ consumer that delegates
one operation, preferably canonical serialization or a bounded scanner, to C.
Compare values/bytes, errors, allocations, copies, peak memory and throughput
against pinned native C++ on identical fixed fixtures.

Determine whether borrowed views suffice before adding conversion copies or
freezing a shared ABI. Keep RAII and owning convenience wrappers above C;
rich metadata algorithms themselves may be the delegated operation. If a call
needs repeated full-store copies, resolve ownership/layout costs before
replacing production code.

UP2 will implement the reusable EXIF/scalar-XMP patch primitive directly
against `metadata_patch.h`; it no longer waits for this reuse experiment.
B5 may later compare that accepted primitive or another accepted operation.
A successful experiment supports a later migration decision; it is not
authorization to switch C++ production code.

## Acceptance And Ongoing Upstream Tracking

First milestone: a **bounded writer core**, with B0-B3 accepted for selected
cases, including safety, XMP lifecycle, native translation and package
interoperation. The second adds **positional reads** through B4. Neither
requires all semantic families to be complete. The full metadata-core target
also includes S1/S2 interpretation, non-fuzzy query and concept semantics;
embedded acceptance requires E1 for a declared configuration. Fuzzy search
and excluded SDK adapters remain outside that denominator.

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
| Interpretation/query/concepts | [CCM subset](src/omc/omc_ccm_query.h), [naming](src/omc/omc_exif_name.h), existing decode/transfer facts; general APIs pending | `src/include/openmeta/metadata_query.h`, `src/include/openmeta/metadata_interpretation.h`, `src/include/openmeta/metadata_concepts.h` |
| Canonical serialization | [omc_exif_write.c](src/edit/omc_exif_write.c), [transfer tests](tests/test_omc_transfer.c) | `src/include/openmeta/exif_tiff_serialize.h`, implementation in `src/openmeta/metadata_transfer.cc`, `tests/metadata_authoring_serialize_test.cc`, `docs/canonical_serialization.md` |
| Translation/projection | [omc_xmp_dump.c](src/edit/omc_xmp_dump.c), [XMP tests](tests/test_omc_xmp_dump.c), [omc_transfer.c](src/edit/omc_transfer.c) | `src/openmeta/metadata_translation*.cc`, `tests/metadata_translation_test.cc`, `docs/translation.md`, `docs/xmp_sync_policy.md` |
| BMFF/packages/safety | [package API](src/omc/omc_transfer_package.h), [package tests](tests/test_omc_transfer_package.c), [diagnostic tests](tests/test_omc_transfer_diagnostics.c) | `src/openmeta/metadata_transfer.cc`, `tests/metadata_transfer_api_test.cc`, `docs/writer_target_contract.md` |
| Positional read | [source API](src/omc/omc_source.h), [source reader](src/omc/omc_read_source.h), [source tests](tests/test_omc_read_source.c) | `src/include/openmeta/random_access_source.h`, `src/openmeta/random_access_source.cc`, `tests/random_access_source_test.cc`, `docs/random_access_input.md` |
| Differential gate | [parity tests](tests/test_omc_parity.cc), [test configuration](tests/CMakeLists.txt) | `docs/development.md`, `docs/api_stability.md`, public format tests |
| Unified patch/reuse | Existing canonical serializers and typed values; new C plan/worker pending UP2 | `src/include/openmeta/metadata_patch.h`, `src/openmeta/metadata_patch.cc`, `tests/metadata_patch_xmp_test.cc`, `tests/metadata_patch_allocation_test.cc`, `docs/migration_0_5.md`, `docs/canonical_patching.md` |

Current priority: finish the remaining UP1 JP2/JPH and deletion checks, then
UP2 unified patching. The UP0 reference qualification and first UP1 correction
slice are complete. Primary position/altitude GPS is the first implemented UP3
translation slice; navigation, destination, quality and text follow. The
committed richer capture groups and the EXIF 3.1
development/correction family follow in UP4. Retain the reading/decoding inventories
and implemented B0-B4 foundation. S1/S2 remain part of UP7. Future C++ reuse
must not narrow the C library's standalone metadata-processing scope.
