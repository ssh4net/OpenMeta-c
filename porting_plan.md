# OpenMeta-c Porting Plan

Updated: 2026-10-17.

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

## Upstream 0.8.19 Reverse Partial Dates

The active target is C++ `0.8.19`, commit
`e8240c3aed7cc858632e4a9cb5605d56b3922f10`, starting from C
`b30aec6337639d3d4d5b6f993314d6875565eac2`. This bounded increment uses the
existing C `omc_translate_xmp()` API and mapping masks.

| Work | Acceptance gate |
| --- | --- |
| Pin and baseline | Detached Clang 20/libc++ reference; fresh C baseline 67/75 with eight historical failures. |
| Reverse partial precision | Exact positive year and valid year/month become zero-filled IPTC dates; EXIF creation selectors reject partial precision transactionally. Technical ModifyDate is unchanged. |
| Publication and carrier lifecycle | Failed generic transactions report zero mutation counts; existing staged commit bounds and distinct-store ownership remain. JPEG/TIFF replacement removes stale times, preserves caption/Make and records last-carrier omission. |
| Integration | Direct rollback and resource checks, paired C/C++ status/count/canonical-IIM cases, twelve forward/reverse lifecycle cases, sanitizer and dependency-disabled suites, unchanged residual inventories. |

The scoped implementation is validated in C `0.15.0.015`: sanitizer and
dependency-disabled shared suites each pass 56/56; 88 paired reverse-date
cases, twelve forward/reverse lifecycle cases and ten JPEG/TIFF carrier cases
pass. Full Release is 70/78 with the same eight failures. Diagnostic inventories
remain identical to the fresh baseline: 135 CTest records and 176 full-inventory
reports (351 with field mismatches). See
[the reverse-date receipt](reverse_dates_0819.md) for evidence.
This does not claim complete C++ creation-date contract 3 parity. Remaining
EXIF work includes clean-value Dirty promotion and absent-companion deletion
intent. C still reconciles each mapping before examining later mappings, so
mixed-error first-failure ordering can differ. Generic C diagnostic/limit
vocabulary, validation of retained references, and distinct-output ownership
remain unchanged.

Next, close the remaining EXIF creation lifecycle, then resume UP4 shared
destination-profile validation and basic capture/sensitivity/Flash/lens.
Explicit last-IPTC-carrier removal needs a separate transfer contract; it is
also absent from the pinned C++ 0.8.19 behavior. Reassess committed upstream
changes before selecting each new immutable reference.

During final validation the live C++ checkout still had HEAD `e8240c3`, with
uncommitted version 0.8.20 changes adding `remove_existing_iptc` to prepared
transfer. Review that explicit-removal contract after it commits; those changes
were neither built nor accepted as part of this pinned batch.

## Upstream 0.8.18 Native IPTC Dates

The preceding accepted target is C++ `0.8.18`, commit
`7712c21b3dfb66ed6a2592263e087920a3b70b44`, starting from C `fd7d0d5`.
The reverse partial-date work was uncommitted at batch start and landed during
validation as C++ `0.8.19`, `e8240c3aed7cc858632e4a9cb5605d56b3922f10`.
It is queued separately from this immutable reference.
The date API is explicit and separate from portable projection and reverse
translation; those existing policies remain unchanged.

| Work | Acceptance gate |
| --- | --- |
| Pin and baseline | Detached Clang 20/libc++ reference; fresh C baseline 64/72 with the same eight failures, 135 CTest and 176 full-inventory diagnostics. |
| Native date/time materialization | Two selected pairs, full/year/month dates, explicit or absent offsets including negative zero, bounded source/target validation and precise diagnostics. |
| Deletion and publication | Dirty tombstone payload/origin ignored and retained; known damage flags allowed on tombstones; unknown C flags rejected. A private owned store snapshot preserves offsets and publishes at most two actions atomically. |
| Integration | Paired C/C++ result/value/provenance checks, direct lifecycle/rollback/alias tests, stored-XMP and JPEG reread, full-date reverse canonical IIM and partial-date reverse rejection. |

The scoped implementation is validated in C `0.14.0.014`: direct sanitizer and
dependency-disabled shared suites each pass 54/54; 87 paired C/C++ cases and 12
lifecycle cases pass. Full Release is 67/75 with the same eight failures and
unchanged inventories (135 CTest diagnostics, 176 full-inventory parity reports;
351 records when field-level mismatches are included). See
[the date batch receipt](iptc_dates_0818.md). The next increment is recorded in
the 0.8.19 section above; this receipt retains its original pin and behavior.

## Upstream 0.8.17 IPTC Convergence Batch

The preceding accepted implementation target is C++ `0.8.17`, commit
`d281e041d940510d97c3e8539e2840a7fca6c679`. The accepted 0.8.14 receipts below
retain their original pin. Upstream date/time work was uncommitted at batch start
and landed as 0.8.18, `7712c21b3dfb66ed6a2592263e087920a3b70b44`, during the batch.
It remains outside this fixed reference. The C starting revision is
`cb9d91fbe6f791c5e0576b49e1d685b73f24f2bb`.

| Order | Upstream change | C implementation and acceptance gate |
| --- | --- | --- |
| 1 | Pin and baseline | Detached 0.8.17 reference; fresh Clang 20 direct and differential baselines; retain the exact broad-inventory residuals. |
| 2 | 0.8.15 canonical IPTC-IIM | Bounded C measure/write API and shared transfer wire engine; deterministic ordering, raw payload fidelity, duplicates, strict validation, limits, overlap and output preservation. |
| 3 | 0.8.16 JPEG Photoshop resources | Preserve pure APP13 segments and each mixed segment's unrelated resources at its source position. No merging; retain names, duplicates and padding. Check malformed rollback, aliasing and executed-package replay. |
| 4 | 0.8.17 native IPTC to XMP | Twenty mappings with per-block charset handling, bounded transactional publication, conflicts/deletions, repeated-value ordering and x-default ownership. Direct tests and paired C++ cases. |

These four scoped steps are accepted in C `0.13.0.013`: focused 11/11,
ASan/UBSan direct 52/52 and dependency-disabled shared direct 52/52. The full
Release schedule is 64/72 with the same eight historical failures; diagnostic
multisets remain 135 in CTest and 176 in `--all`. See
[the batch receipt](iptc_convergence_0817.md) for cases and limits. Its follow-up
is the 0.8.18 date/time batch above. RD9/RD10 remains a separate reader/decode
workstream.

## Upstream 0.8.14 Execution Baseline

The previous accepted comparison is C++ `0.8.14`, commit
`fc2052b9eac0c7819c09a067e0c698fe38a2a155`. The 0.8.11 source review below is
retained as history. Five further commits add these portable-core requirements:

| Change | Porting decision |
| --- | --- |
| Optional empty TIFF edits and directory-count guards | Verify existing C no-op behavior and reject malformed count tables before arithmetic/allocation. C has no matching `require_updates` control; reconcile that explicit policy with UP6a rather than silently copying a C++ default. |
| Profile translation v2 | Move the five existing profile fields into current lifecycle work. The ExifIFD removal allowlist is now 85; ColorSpace is separately host-owned and cannot be selected for deletion. |
| Destination-profile validation contract v1 | Add reusable opt-in validation to typed creation/editing, detached validation and serialization in UP4. Validate the assembled destination using explicit host image/version facts, after reconciliation. This is logical metadata validation, not proof of pixel/storage correctness. |
| Nikon E8700 model lifetime correction | Check model-dependent decisions before arena growth in affected reader work. |
| Structured XMP editing contract v1 | Add bounded ordered Add/Replace/Remove for the nine supported roots to UP5 and generic authoring. Preserve exact namespace/path, language members, whole-record replacement, tombstones, aliasing and aggregate input/expanded-operation budgets. Arbitrary RDF remains outside this API. |

UP0 and the focused UP1 resource/container slice are implemented and verified
against this pin; see [the execution receipt](up0_up1_0814.md). Broad inventory
residuals remain explicit and do not become accepted parity. C package 0.11.0
extends `omc_jumbf_limits` with a finite aggregate-work budget; rebuild consumers
and initialize options. Payload output limit zero now means unlimited, matching
C++; the default remains 64 MiB.

The **UP6a direct native-edit foundation and UP3 GPS v2 batch is accepted** in
C package 0.12.0 against this pin. See [the batch receipt](up6a_gps_v2_0814.md)
for validation and limits. Its owned intent supports seven IFD0, 85 ExifIFD
and 31 GPS removal selectors, explicit clear/merge conflicts, both TIFF byte
orders and layouts, target-version checks, and executed-output package replay.
All five GPS APIs now promote accepted clean values and retain absent deletion
intent. All 32 standard GPS IDs pass the 13-carrier readback matrix; 24 selected
TIFF edit cases compare against C++. These are scoped cases, not stage-wide
coverage percentages.

| Workstream | Current boundary | Next gate |
| --- | --- | --- |
| UP6a | Direct selected TIFF/BigTIFF and existing/template DNG edits, output rollback, executed-output package persistence | Broader replacement/page/SubIFD semantics and prepared snapshots remain UP6; generic payloads reject destination-only intent |
| UP3 GPS | All five v2 lifecycles, bounded planning, representation retention and carrier persistence | Keep these cases in each later transfer regression run |
| UP4 | Existing earlier-contract families retained | Dates/basic capture/sensitivity/Flash/lens lifecycle first, with shared destination validation; continue family order below using profile v2 |
| RD9/RD10 | Independent reader/decode expansion | Updated memory/callback inventories and selected corpus; eight existing broad parity failures remain open |
| UP5 / remaining UP6 | Structured XMP/locations, source applicability and snapshot/consumer contracts | Preserve intent across every supported consumer; reject unsupported routes before output/callbacks |
| UP2 / UP7 | Existing patch foundation and partial semantics | Remaining patch binding, normalized interpretation and exact non-fuzzy queries |

New upstream commits enter a delta queue; they do not change this batch's
immutable acceptance reference. RD9 reader batches can proceed independently
after their resource gate. The historical 0.8.11 gap statements below describe
the earlier checkout; the accepted 0.8.14 receipts supersede them.

## Upstream 0.8.11 Convergence Roadmap

Historical source review; the 0.8.14 execution section above controls current
acceptance and priorities. This review superseded the next-batch order in the historical
0.5.10 section below, while retaining completed C work and its exact evidence.
The 2026-10-11 review compared committed C++ `8594030c..0242459f` (25 commits),
current public headers, implementations, migration guides and tests. Both
tracked trees were clean at review. This is a source review and documentation
update; no 0.8.11 C/C++ build or runtime parity was established in this batch.

| Role | Exact baseline | Meaning |
| --- | --- | --- |
| Current C code | `6ddc2ab`, local revision `0.10.1.009`; CMake project version remains `0.10.0` | Strict C90 with extensions disabled. Local revision subjects are not the package version. |
| Last broad authoring/IPTC acceptance | C++ `0.4.132`, `7f0ec70617d7286a33b8d821c459241245520615` | Preserve the old reading/decoding and IPTC results under their recorded pins. |
| Last focused C++ comparison | `0.5.10`, `8594030c5acf0bb930a02c13c874ae25845f087f` | C Release and sanitizer suites recorded 47/47; GPS parity has two primary cases plus four complete group cases. This is representative v1 GPS coverage. |
| New source-reviewed target | `0.8.11`, `0242459f96a7b66b281d28f6cc565aaa86249a1b` | Unfrozen development ABI 4. Freeze this commit for new comparison builds; subsequent C++ commits enter a separate delta queue. |

Do not replace older successful measurements with an implied current pass.
The historical 208 public read cases per access mode and 69-file corpus remain
useful regression inputs; they do not cover the newer RAW families or lifecycle.
C++ test/corpus receipts are reference evidence, not C acceptance. Stage-wide
percentages need a case inventory against this pin before they can be updated.

### What Changed Since 0.5.10

| C++ change | Relevant source evidence | C-port consequence |
| --- | --- | --- |
| 0.5.11/0.5.12: six EXIF 3.1 development fields, LearningOptOutIn and five profile fields | `metadata_translation_development.cc`, `metadata_translation_learning.cc`, `metadata_translation_profile.cc`; public options and tests | Add complete bounded families, validation and portable projection. Native DNG/EXIF remains in scope; Adobe SDK integration does not. |
| 0.5.13/0.5.14: RAW applicability, exact MakerNote trust keys and CR3 compressor text | `metadata_transfer.cc`, `simple_meta.cc`, `docs/raw_read_parity_plan.md` | Reconcile safety decisions with explicit source descriptors; unknown context remains conditional. Same-numbered tags in the wrong IFD must not establish trust. |
| 0.6.1/0.6.2: CR3 CTBO locations and CRAW/CMP1 summaries; bounded BMFF growth | `container_scan.cc`, `simple_meta.cc`, `metadata_transfer.cc` | Add bounded read facts and scanner routes; preserve absolute CR3 locations or reject before output. Test direct writing and package materialization independently. |
| 0.6/0.7/0.8: explicit native deletion and selected destination merge | `PreparedTransferBundle`, `collect_tiff_*_removals`, TIFF edit validation in `metadata_transfer.cc` | Carry intent separately from serialized live EXIF through preparation, edit/stream/package paths. Omission must preserve destination fields. |
| 0.7.0-0.8.11: native lifecycle across timestamps, capture, GPS, spatial, identity, environment, composite, structured capture, text and development/learning | `metadata_translation*.cc`, `docs/translation.md`, `docs/capture_sync_milestone.md` | Port the latest lifecycle once: promote accepted clean exact values, retain absent-key tombstones, account for budgets, preserve representation and reject atomically. |
| 0.8.1: ordered repeated `dimg` inputs | BMFF reference rewriting in `metadata_transfer.cc` and transfer regressions | Preserve repeated targets and order. Reject removal of an input while its derived-image source remains; source removal can drop the whole relation. |
| 0.8.2: aggregate JUMBF semantic-work budget and strict decompression output limits | `jumbf_decode.h`, `jumbf_decode.cc`, `resource_policy.h`, `container_payload.cc` | Prioritize bounded work and exact-limit behavior before adding decode tables. Existing count/depth limits alone are not the new aggregate contract. |
| 0.8.3/0.8.7: legacy and mixed RAW expansion | `mrw_decode.cc`, `nikon_capture_decode.cc`, CIFF/TIFF and MakerNote decoders; `*_raw_gap_test.cc` | Reopen the read/decode denominator for MRW, embedded Sony MRI, Canon CIFF, Nikon/Sony layouts and the listed vendor-offset corrections. |
| 0.8.10/0.8.11: native text/version and learning wire fidelity | TIFF serialization/transfer and shared text/learning validators | Retain terminal NUL counts, type-129 UTF-8, BOMs, numeric byte order and unrelated opaque bytes; retain target DNGVersion for existing/template edits. |

The concrete writer inventory is now **7 IFD0 removal tags, 84 ExifIFD
merge/removal tags, and GPS value tags 1-31**. GPSVersionID is structural and
is cleaned only after the last non-version value disappears. These counts come
from the current transfer allowlists; they are not the number of all EXIF tags
or a completion percentage. The older 75-tag capture count is historical.
The documented seventeen-family capture table now contains 81 distinct tags;
LearningOptOutIn and the five-field profile API are separate from that table.

### API And Compatibility Decisions

- Rebuild parity consumers against matching 0.8.11 headers and libraries.
  ABI 4 is explicitly unfrozen: `PreparedTransferBundle` and `JumbfDecodeLimits`
  changed layout. `SameMajorVersion` package discovery can accept a later 0.x
  library for an older minimum request; it is not a binary-compatibility test.
  Check the exact source/build identity and required contract constants.
- All five GPS contracts are v2; general capture is v3. Dates, technical,
  camera text, Flash, sensitivity, APEX/settings, spatial/rational/identity,
  additional/environment, encoding/composite, structured capture, EXIF text,
  development and learning are v2. Profile and LightSource remain v1; do not
  infer uniform lifecycle coverage from similar signatures or an ABI label.
- Source snapshot wire version 1 remains unchanged, but it carries added MRW
  format/name contexts and native edit flags. Test supported state in both
  directions; an old consumer need not understand new enum values. Snapshot
  encoding is separate from prepared-bundle layout and C package schemas.
- Preserve C source/output separation, explicit capacities/status codes and
  host synchronization. C layouts/versioning need their own decision if new
  limits or intent storage change public structures. Do not copy C++ vectors,
  ABI numbers or in-place ownership behavior into C mechanically.
- Keep current mapping defaults and intentional exclusions. Fuzzy/RapidFuzz,
  Adobe DNG SDK, language/host wrappers and pixel codecs stay outside C.
  zlib, Brotli, Expat and OpenSSL logic remain eligible optional C backends;
  verification applications, including ExifTool, remain external only.

### Current Gaps Confirmed In C

`omc_translation_gps.c` and `omc_translation_gps_groups.c` count exact native
matches as unchanged without promoting clean entries to Dirty. Deletion walks
existing active native members; it does not synthesize complete absent-key
intent. Therefore the six successful 0.5.10 parity cases cannot establish the
new GPS v2 contract.

The C TIFF overlay is reusable, but `omc_exif_overlay.c` currently accepts only
little-endian destinations, treats Deleted entries as removal selectors without
requiring Dirty, and admits emitted wire types 1-12. The 0.8.11 path requires
explicit selection, LE/BE classic TIFF and BigTIFF, and type 129 where legal.
The C prepared-transfer API lacks equivalent explicit per-IFD removal lists and
merge controls. Audit direct overlay behavior and prepared consumers together;
do not describe deletion as wholly absent or assume the existing overlay is
already equivalent.

The four-field C EXIF-text API is implemented. **Nine** of the reference API's
thirteen fields remain to port, plus v2 lifecycle for all thirteen. New
development, learning and profile transactions are additional work. The C JUMBF
limit structure has no aggregate semantic-work field. The new reader/resource
and BMFF gaps are detailed in [read_decode_parity.md](read_decode_parity.md)
and [bmff_writing.md](bmff_writing.md).

### Revised Execution Order

Keep UP0-UP7 as workstream identifiers. Complete bounded slices below rather
than restarting completed work or waiting for every UP2 patch feature before
adding reader or translation coverage.

| Order / slice | Work | Acceptance gate |
| --- | --- | --- |
| 1. UP0 refresh | Freeze 0.8.11; build a matching Clang 20/libc++ reference and assertion-enabled C90 direct/differential targets. Inventory old residuals separately from new deltas. | Exact commits, versions, dependency flags, contract constants, discovered tests and mismatches recorded. Old 0.5.10 binaries are not reused with new headers. |
| 2. UP1 resource and container corrections | Add JUMBF aggregate-work accounting; verify strict zlib/Brotli output caps; reconcile ordered `dimg`, self-contained method-2 references, free-box/meta growth and CR3 relocation rejection. Include exact-IFD MakerNote trust. | Discriminating malformed/limit/duplicate/order cases; exact-cap and disabled-backend outcomes; reject before mutation/callbacks. Direct and package routes preserve media and opaque data. |
| 3. UP6a native edit foundation, then UP3 GPS v2 | Add bounded per-IFD edit intent and merge selection, destination version checks, live-over-tombstone precedence, whole-directory-clear conflict checks and LE/BE transfer support. Upgrade all five GPS APIs together. | Test clean promotion, absent deletion, omission, conflicts, repeated calls and every preflight budget; reread JPEG and classic/BigTIFF LE/BE, plus native existing/template DNG. Preserve unselected records, MakerNotes and media offsets/hashes. Source snapshots are not a prerequisite for the direct gate. |
| Independent after 1-2: RD7-RD10 | Refresh CR3 and legacy RAW readers, model-gated native tables, positional paths and selected corpus. Share existing source/range machinery. | Separate memory/callback inventories including MakerNote-disabled, unknown-layout, nonzero-base and forbidden-media-read cases. See the reader plan; do not wait for writing or snapshots. |
| 4. UP4 by lifecycle family | Dates/basic capture/sensitivity/Flash/lens first; APEX/settings and rational/spatial/identity next; then additional/environment, encoding/composite, structured capture and all thirteen EXIF text fields; finally development/learning and the existing profile v1 route. | Port current source types, projection, validation, sentinels and wire fidelity with each family. Each accepted family traverses UP6a transfer once; do not add old-contract translations only to rewrite them later. |
| 5. UP5 and remaining UP6 | Structured Created/Shown locations, source RAW applicability, snapshot v1 state and remaining deferred transfer/package reconciliation. | Native dirty/deleted intent survives snapshot restore and every supported consumer; unsupported consumers fail explicitly. Existing/template DNG retains target version; minimal fresh scaffold policy remains separate. |
| Continuing: UP2 and UP7/E1 | Finish serializer-recorded repeated/native-projected patch binding; port normalized interpretation and exact queries by accepted metadata family; qualify an explicit embedded configuration. | Existing patch allocation/alias gates remain. Semantic records retain provenance/unknowns without fuzzy matching. Record memory, stack, storage and I/O limits; desktop C90 builds alone are not embedded acceptance. |

UP6a is deliberately brought forward from the old late snapshot/transfer batch:
native intent is now shared by almost every authoring family. It is a bounded
extension of the existing transfer/overlay path, not a prerequisite general
synchronization engine. Reader work and remaining patch binding can proceed
independently with disjoint ownership. Validate a coherent field group once
against final code, including negative cases and carrier readback.

For GPS specifically, active merge needs one valid supplied GPSVersionID that
agrees with the destination; do not upgrade versions or reinterpret altitude.
PreserveExisting keeps an active native owner without promotion. Distinguish
source filtering from destination deletion: rendered-source CFA/encoding/RAW
filtering must not erase an unselected destination field. Keep source, prepared
state and destination lifetime explicit through plan/apply/replay.

C++ follow-up items are tracked separately: the five profile fields' full native
lifecycle, source-table semantics, malformed-original triage and the empty-update
`require_updates=false` audit remain upstream work. The existing profile v1
route can be ported with its documented limits; do not invent its future v2
behavior. OIIO/iRAW adoption, all-RAW coverage, SDK conformance and private offset
repair remain outside this batch. C++ does not need to freeze globally: each C
batch pins a commit and queues later changes without changing its acceptance
denominator mid-batch.

Source map: C++ `docs/migration_0_6.md`, `docs/migration_0_7.md`,
`docs/migration_0_8.md`, `docs/translation.md`,
`docs/capture_sync_milestone.md`, `docs/raw_read_parity_plan.md`,
`src/include/openmeta/metadata_translation.h`, `metadata_transfer.h`,
`jumbf_decode.h`, `container_scan.h`, and their implementation/tests.
`CHANGES.md` currently stops at 0.8.7; later lifecycle details were checked in
current code, commits and the updated translation/capture documentation.

## Upstream 0.5.10 Convergence Roadmap

Historical 2026-09-20/21 plan and execution record. The 0.8.11 section above
controls current work; the old ordering, counts and watch items below retain
their original reference context.

This section supersedes the earlier next-batch order and the conditional
EXIF-only patch proposal. Completed B0-B4, RD0-RD6 and IPTC work remains the
foundation. The C++ reference has moved from 0.5.2 to 0.5.10, so the feature
inventory and the next C batches are refreshed here. This section started as a
source-reviewed plan; the 2026-09-20 execution now has a fresh committed-
reference baseline and focused UP1/primary-GPS results. The older accepted C
results below still belong to the 0.4.132 reference.

| Role | Version / revision | Treatment |
| --- | --- | --- |
| Current C implementation | 0.10.0 plus the local UP1-UP4 batch | Retain existing C APIs and defaults; the local batch adds JP2/JPH replacement, the prepared patch core, all four GPS group families and the first EXIF text/version family |
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
| UP1: existing behavior corrections | Audit TIFF/BigTIFF malformed roots, JP2/JPH EXIF/XMP UUID replacement, terminal/extended boxes, TIFF deletion propagation, and scoped XMP whitespace/projection changes. Fix only demonstrated C differences. | **Implemented bounded slice:** classic/BigTIFF root outcomes pass through contiguous and callback C paths; scoped camera/lens/spectral whitespace matches; `omc_jp2_rewrite()` replaces selected direct/UUID carriers for JP2/JPH and preserves terminal size-zero boxes. The follow-up matrix now covers malformed short UUID payloads and classic/BigTIFF empty ExifIFD deletion with XMP preserve/strip readback. Full file-corpus deletion/readback remains an acceptance gate. |
| UP2: unified prepared patch core | Implement the current EXIF/scalar-XMP contract through C data, explicit lifecycle and bounded preparation/worker storage. Reuse canonical serializers, typed values and validation. | **Initial C core implemented:** mixed-family plan/instance lifecycle, host-issued generation tokens, all-or-nothing update validation, fixed EXIF/escaped XMP widths and synchronous EXIF-then-XMP replay. Exact serializer-recorded repeated-value binding, alias checks and the C++ allocation-free gate remain open. |
| UP3: complete GPS families | Primary position/altitude, UTC/navigation, destination, receiver quality, then encoded GPS text. Share exact rational, version, companion and conflict rules. | **Initial four-family C slice implemented:** navigation, destination, quality and encoded text APIs now share bounded rational/unit/version/conflict handling. The paired 32-tag C matrix and representative C/C++ group parity target are complete; JPEG/Classic TIFF/BigTIFF persistence remains the UP3 exit gate. |
| UP4: capture and identity convergence | Port the committed APEX, focal-plane/subject, capture-rational, Flash, LightSource, sensitivity, camera/lens/spectral text, LensSpecification, ImageUniqueID, environment, encoding, composite, structured-capture and UserComment groups. Then add the six EXIF 3.1 development/correction fields `A40D`-`A412`. Preserve existing basic capture mappings. | **First family implemented:** bounded ExifVersion, FlashpixVersion, UserComment and ImageTitle translation, including EXIF 3 UTF-8 and legacy UTF-16LE comments, conflicts, tombstones and TIFF reread. Remaining nine EXIF text fields and the other capture/identity groups remain open. |
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
with the pinned C++ implementation. The local batch now adds representative
navigation, destination, receiver/quality and encoded GPS text groups; paired
all-tag and persistence qualification remains open.

The fresh C tree passes 49 of 57 CTest targets. The eight failures are the
pre-existing broad inventory targets (`omc_test_parity`, remaining-source,
read/callback inventory and BMFF/box/TIFF source parity); their reported
0.5.10 deltas are LensSpecification namespace/fractions, rational display and
GPS destination-unit projection plus broader inventory changes. They are kept
as residuals and were not re-baselined. The new GPS, UP1, XMP and focused parity
targets pass.

## 2026-09-21 implementation batch: UP1 through the first UP4 family

The local C batch uses the same pinned C++ 0.5.10 source and keeps the dirty
main C++ checkout out of the comparison. Release configuration
`/tmp/openmeta-c-up14-20260921` uses Clang 20, C90, disabled parity tests and
warnings as errors. Its broad direct suite passes `47/47`; the focused UP1-UP4
set (`omc_test_up1`, `omc_test_jp2_rewrite`, `omc_test_gps_groups`,
`omc_test_exif_text`, `omc_test_metadata_patch`, `omc_test_gps_translation` and
`omc_test_xmp_write`) passes `7/7`. A fresh Clang 20 ASan/UBSan build in
`/tmp/openmeta-c-up14-asan-20260921` passes `47/47`.

UP1 now has a bounded JP2/JPH top-level replacement helper. It removes all
selected direct and standard UUID EXIF/XMP carriers, accepts normal and extended
box sizes plus a terminal size-zero box, inserts replacements before that
terminal box and preserves unknown/unselected boxes. Existing root/deletion and
scoped whitespace checks remain direct tests; a full carrier corpus gate is
still required.

UP2 now has `omc_metadata_patch.h` with explicit plan and worker lifetimes,
canonical EXIF and portable XMP payloads, host-issued plan IDs, typed request
shapes, bounded handles, fixed-width/escaped-width updates and synchronous
replay. The C core validates a complete batch before mutation. The follow-up
gate binds XMP slots by emitted namespace URI and simple property identity,
accepts variable-length logical text when escaped width is fixed, supports
typed EXIF arrays, rejects borrowed views that overlap either worker payload,
and keeps successful and rejected apply/replay calls allocation-free. The
remaining parity work is conflict-policy and serializer-record evidence for
all repeated/native-projected properties; the C API keeps its bounded
caller-owned model.

UP3 now exposes navigation, destination, receiver-quality and GPS-text reverse
translation groups. The groups share fixed bounded records, rational parsing,
unit aliases, version injection, conflict policies, dirty tombstones and edit
publication. The direct tests cover representative mappings; the exit gate is
still paired C++ coverage of all 32 standard GPS tags with file persistence.

The pinned C++ 0.5.10 `MetadataGps*` filter passes 64/64 tests. The C primary
GPS parity target passes both cross-language cases, and the C Release and
ASan/UBSan suites pass the existing GPS-group and transfer-persistence tests.
At that point the four additional C group APIs still had representative direct
tests only; the full 32-tag paired persistence matrix was the next UP3 gate.

## 2026-09-21 UP3 paired GPS group persistence batch

The next UP3 batch closes the C-side paired matrix for the complete standard
GPS tag range. `omc_test_gps_groups` now exercises navigation, destination,
receiver-quality and encoded-text writeback with all of their native members,
including `GPSVersionID`, and round-trips each result through the canonical
TIFF serializer and EXIF decoder. Together with `omc_test_gps_translation`,
the direct matrix covers tags `0x0000` through `0x001F`; the existing transfer
persistence fixture also checks the complete native GPS record.

The group implementation now maps navigation track to tags `0x000E` and
`0x000F`, emits version `2.3.0.0` for every active group, removes that version
only after the last GPS value is removed, and retains equivalent native text
terminators and UTF-16LE/BE encodings. Timestamp input accepts exact UTC
offsets, validates calendar boundaries, normalizes date changes, and retains
representable fractions. Quality aliases, typed integer modes, bearing/track
range checks, and UTF-8 GPS text conversion are covered by the direct tests.

The Release and ASan/UBSan Clang 20 suites pass `47/47` after this batch. The
pinned C++ 0.5.10 GPS filter remains `64/64`, and the primary cross-language
parity target passes its two cases. A dedicated C/C++ parity target for the
four group APIs and broader corpus evidence remain open; the C API keeps its
bounded group text storage and does not claim test-for-test C++ coverage.

## 2026-09-21 UP3 cross-language GPS group parity batch

`omc_test_gps_parity` now runs with assertions enabled in Release builds and
compares C with the pinned C++ 0.5.10 implementation for one complete
navigation, destination, quality and encoded-text source. Each case compares
the status counters, `GPSVersionID`, native types, exact rationals, DMS arrays,
references and encoded text bytes. The existing primary position and altitude
cases remain in the same executable.

The direct parity executable passes with Clang 20 and libc++. The namespace
fixture now uses its exact XMP URI length; this also makes the primary parity
checks active instead of silently accepting an empty C source in Release
builds. The remaining UP3 gate is transfer-lifecycle persistence through JPEG,
classic TIFF and BigTIFF carriers; this batch does not claim full C++ test-for-
test coverage.

UP4 starts with `omc_translate_xmp_exif_text()`: ExifVersion, FlashpixVersion,
UserComment and ImageTitle. It preserves ASCII, EXIF 3 UTF-8 and legacy
UTF-16LE UserComment forms, requires EXIF 3 for ImageTitle, and verifies
canonical TIFF readback. The nine remaining EXIF text fields and the other
capture/identity groups remain planned.

## 2026-09-21 follow-up qualification: UP1 carrier and deletion gaps

The pinned C++ 0.5.10 tests were rerun for the JP2 rewrite groups, the
BigTIFF boundary checks and `MetadataTransferApi.EmptyExifIfdRemovalIsExplicitAndSurvivesXmpStripping`.
The C port now matches the demonstrated malformed UUID rule: a `uuid` box with
fewer than 16 UUID bytes returns `OMC_JP2_REWRITE_MALFORMED` without output.
The JP2/JPH C matrix covers direct and standard UUID EXIF/XMP carriers,
unknown/IPTC/GeoTIFF UUID preservation, extended boxes, terminal size-zero
boxes and the selected replacement combinations.

The TIFF overlay now removes a child pointer when its resulting directory has
no records. `tests/test_omc_up1_deletion.c` exercises classic TIFF and
BigTIFF, explicit final ExifIFD deletion, XMP preserve and strip modes, and
decoded output readback. This closes the demonstrated UP1 deletion case while
leaving the full camera-file corpus and the paired GPS persistence matrix as
the remaining UP1/UP3 acceptance work.

Release Clang 20 C90 with zlib/Brotli passes 47/47 CTest targets, including
the new deletion test. The matching ASan/UBSan tree also passes 47/47. The
reference C++ filter passes all 8 selected JP2, BigTIFF and deletion tests.

## 2026-09-21 UP2 patch-core qualification

The C patch core now records the emitted XMP namespace URI and simple property
identity during preparation, then applies only compiled offsets. It rejects
structural XMP paths, invalid initial or replacement UTF-8/XML text, malformed
borrowed views, duplicate or foreign handles, and input views that overlap
either worker payload. XMP updates may change logical length when the escaped
width remains fixed. EXIF updates cover scalar, text, bytes and typed arrays;
all updates validate before either payload changes. A null replay callback has
its own result code and replay remains EXIF then XMP.

`tests/test_omc_metadata_patch.c` now covers worker lifetime after plan reset,
transactional failures, alias rejection across both families, host-generation
separation, namespace-prefix changes, all XML escape forms, variable-length
UTF-8, typed arrays and 1,000 repeated updates. The Linux test target links
malloc/calloc/realloc wrappers and observed zero allocations during the
successful apply loop and replay. Release Clang 20 C90 and ASan/UBSan each pass
47/47 CTest targets. The pinned C++ 0.5.10 reference passes all 14 metadata
patch unit tests plus its standalone allocation test.

The current main C++ checkout is now beyond the pinned patch reference: commit
`6e17c84` adds the EXIF 3.1 development/correction writeback family A40D-A412.
That family remains a later C-only roadmap slice and is not mixed into this UP2
qualification.

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

The matrix combines C 0.11.0 with the 0.8.14 execution baseline and revised
UP0-UP7/RD7-RD10 plan. Earlier measured rows retain their recorded reference
pins; only the focused resource/container slice has new acceptance in this
batch. Presence alone does not establish complete reference parity:

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
| R2 | Contiguous scan, payload assembly, decode | `omc_scan.h`, `omc_pay.h`, `omc_read.h`; direct EXIF/XMP/ICC/IPTC/IRB/JUMBF/EXR tests | Present under historical pins; focused TIFF-root and XMP whitespace corrections implemented. RD7/RD8 establish a fresh synthetic baseline and focused resource behavior against 0.8.14; reader expansion remains open. No universal camera/read-parity claim. |
| R3 | MakerNote/native RAW and modern-container enrichment | `src/read/omc_exif.c`, `omc_bmff.c`, naming and read tests | Partial against 0.8.14. MRW/MRI and CR3 CNCV/CTBO/CMP1 routes are missing; newer CIFF, Nikon Capture, Sony/A100 and vendor-offset layouts need RD9 family fixtures. Earlier CMT3/vendor support remains implemented. |
| R4 | BMFF derived fields | Item semantics, properties, `ipma` associations and `grpl` summaries in `omc_bmff.c` | Existing scene/component/grid/overlay/identity/tile summaries and 68 historical cases per access mode remain. Add current CR3-derived facts in RD9a and requalify tiled inputs. Read summaries do not establish safe writer edits. |
| R5 | Positional source, read budgets, source ranges/windows | `omc_source.h`, `omc_read_source.h`, direct and focused C++ source tests | Present shared bounded source machinery; RD9 adds current reader families without full-file copying. Test metadata boundaries, nonzero bases and forbidden media reads separately from contiguous behavior. Historical resource limits remain; see `read_decode_parity.md`. |
| R6 | Runtime capabilities, preview, CCM/DNG helpers | `omc_capabilities.h`, `omc_preview.h`, `omc_ccm_query.h` and direct tests | Present bounded helpers. Capabilities must report actual C support and enabled compression features. |
| R7 | Detached entry/store validation | `omc_store_validate.h`; bounded diagnostics, wire/value checks, singleton and image-context tests | Partial. Retain the 0.5.10 schema backlog and add current development/learning shapes, retained-NUL wire counts, type 129, sentinels and endian rules with UP4 families. Port the shared opt-in 0.8.13 destination-profile validator with explicit host facts; detached validation remains separate. |
| R8 | Decoded snapshots, source provenance and persistence | C callers retain stores/bytes; transfer packages retain output source ranges | Missing named decoded-state snapshot API; planned UP6 after direct native-intent support in UP6a. Snapshot v1 interoperability needs dirty/deleted intent, raw-carrier links and current format/name contexts; matching a version alone is insufficient. |
| R9 | Aggregate decode work and decompression bounds | C has collection/source limits, aggregate JUMBF work accounting and strict decoder output caps | Focused RD8/UP1 slice implemented against 0.8.14. Exact-cap, overflow, short-output, zero/unlimited payload and finite-zero JUMBF rules have direct tests; compression has a differential target. Algorithms need not consume identical work units. Embedded stack/storage acceptance remains open. |

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
| W2 | Canonical TIFF/EXIF serialization | C++ `serialize_exif_tiff()` is target-neutral and honors supported wire hints. C's public serializer builds typed TIFF; internal transfer payloads apply target framing above it. | Present with historical exact-byte comparisons. UP4/UP6a must reconcile type-129 text, retained terminal NULs and structured/learning numeric endian conversion across canonical output and LE/BE destinations. |
| W3 | EXIF/IPTC to portable or materialized XMP | Projection, custom namespaces and managed canonicalization; 0.13.0 adds twenty explicit native IPTC mappings and 0.14.0 adds both native creation-date pairs | The 0.8.18 date contract passes direct tests, 87 paired cases and 12 lifecycle cases. Native date precision, signed/absent offsets and deletion intent are covered. Exact fractions, aliases, sentinels, development/learning/profile and remaining capture/text projection remain separate UP4 families. |
| W4 | Explicit XMP to native metadata | Original groups, flat locations, combined IPTC, all five GPS v2 APIs and four EXIF text/version fields | 0.15.0 adds reverse partial IPTC creation dates and transactional EXIF precision rejection against 0.8.19. Remaining EXIF creation promotion/absent deletion and first-error ordering are tracked separately. GPS v2 promotion/absent deletion and representative carrier lifecycle were accepted in 0.12.0 against 0.8.14; retain that receipt. UP4 has nine further EXIF-text fields plus capture/development/learning/profile work. UP5 owns structured locations; old masks stay stable. |
| W5 | Native IPTC-IIM emission | Public `omc_serialize_iptc_iim()` and shared transfer engine; JPEG IRB and TIFF/DNG tag `33723` carriers | 0.13.0 adds canonical ordering, raw payload fidelity, strict bounded measure/write, transfer skip counts and per-carrier JPEG resource preservation against 0.8.17. See the scoped batch receipt; prepared IPTC patching and in-place JPEG planning remain separate. |
| W6 | Target image facts and transfer safety | C has target image spec, CompatibleFile/RenderedImage and diagnostics; C++ has wider source-processing classification and a RAW-data descriptor | Partial. Port descriptor-based RAW curve/linearity applicability, exact-IFD MakerNote trust and existing/template DNGVersion policy. Source filtering must preserve unspecified destination fields during merge; it is not redaction. Unknown context remains conditional. |
| W7 | Prepare, compile, execute, persist | `omc_transfer.h`, `omc_transfer_persist.h`, direct overlay and persistence tests | 0.12.0 accepted bounded native edit intent and GPS v2 lifecycle against 0.8.14. 0.13.0 adds canonical IPTC transfer and JPEG executed-output replay; 0.14.0 verifies materialized date XMP through JPEG readback. 0.15.0 verifies stale-time removal with nonempty IPTC replacement in JPEG/TIFF; last-carrier omission remains explicit. Extend this pipeline once per UP4 family; broader inventories and consumer-specific acceptance remain open. Preserve unselected native fields, opaque notes and media. |
| W8 | XMP carrier merge and lifecycle | C has destination embedded/sidecar stores, precedence, writeback and persistence options | Present controls, Partial lifecycle parity. Test modes/defaults, strip/overwrite/failure behavior and source/destination conflicts. |
| W9 | Payload/package artifacts | C has `OMTPLD01` v1, `OMTPKG01` v2, semantic views, replay, executed-output materialization and artifact inspection | Present bounded wire families. Carry or explicitly reject new native edit intent according to each consumer's schema. Qualify interoperation separately from C++ prepared-object layout and decoded snapshot v1. |
| W10 | BMFF package item insertion | C has Exif/XMP/JUMBF/C2PA routes, ICC, synthesized `idat`, inserted 32-bit IDs and bounded method-2 references | Shared bounded materializer now replaces managed families and remaps unambiguous IDs. Append layout preserves existing media addresses; physical byte layout differs from C++. |
| W11 | Newer bounded BMFF writer rules | C has compact `iloc`, local `dref`, managed replacement/remapping and multiple-`ipma` consolidation | Focused 0.8.14 correction implemented: retained `dimg` input loss rejects, mixed ordered repeats survive, direct/serialized packages are tested. Existing method-2/local/free/meta-growth fixture passes. Broader adjacent-free and CR3 no-relocation qualification remain open; byte layout can differ. |
| W12 | Transactional canonical EXIF/scalar-XMP patching | C++ 0.5 replaces the removed EXIF-only API with `metadata_patch.h`; C has `omc_metadata_patch.h` plan/worker/replay plus typed arrays and bounded borrowed views | Partial. The C primitive provides mixed-family atomic update validation, host-issued generations, emitted namespace-URI/simple-property binding, variable logical XMP text with fixed escaped widths, alias rejection, typed scalar/array EXIF updates, and allocation-free apply/replay checks. Full serializer-record evidence for repeated and native-projected properties remains open. |
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

UP2 now has an initial reusable EXIF/scalar-XMP patch primitive in
`metadata_patch.h`; it still needs repeated-value binding, alias checks and
allocation-free qualification before it can support the full C++ contract.
B5 may later compare that accepted primitive or another accepted operation. A
successful experiment supports a later migration decision; it is not
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
| Unified patch/reuse | Existing canonical serializers and typed values; initial C plan/worker in `src/omc/omc_metadata_patch.h` | `src/include/openmeta/metadata_patch.h`, `src/openmeta/metadata_patch.cc`, `tests/metadata_patch_xmp_test.cc`, `tests/metadata_patch_allocation_test.cc`, `docs/migration_0_5.md`, `docs/canonical_patching.md` |

Current priority: UP6a native edit intent and UP3 GPS v2 after the 0.8.14
UP0/focused-UP1 baseline. RD9 reader work can proceed independently; RD10 owns
expanded inventory/original acceptance. Continue UP4 family lifecycle,
UP5/remaining UP6, UP2 patch binding and UP7 semantics under the active order
above. Preserve completed B0-B4, earlier UP/RD slices and their pinned results.
Future C++ reuse must not narrow the C library's standalone metadata scope.
