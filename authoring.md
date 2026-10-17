# Typed authoring and native translation

The public C authoring operations use the existing store, arena, value, and
edit types. They require no file handle or C++ runtime.

The current execution target is C++ 0.8.14 (`fc2052b`). Implemented C
behavior and the historical 0.4.132/0.5.10 gates below retain their original
scope. The [active roadmap](porting_plan.md#upstream-0814-execution-baseline)
records accepted native selected edits and GPS v2 before further family
expansion. The [0.12.0 receipt](up6a_gps_v2_0814.md) defines that bounded gate;
the earlier resource baseline alone does not establish authoring parity.

## Lifetime and failure contracts

Initialize every output store before calling `omc_edit_commit()`,
`omc_store_compact()`, or any `omc_translate_xmp*()` call. The output must be distinct
from the source. These operations build a candidate and publish it only after
success. Failure preserves the source, previous output, and its borrowed views.
Success invalidates views into the previous output.

Values and keys supplied to an edit refer to that edit's arena until commit.
Commit copies their referenced bytes. Array values carry an explicit byte
order; scalar values use the typed union. EXIF decoding marks wire arrays with
their original byte order. Native array construction does not copy its input
arena reference.

`omc_edit_mark_dirty()` changes only the Dirty flag. It preserves the value,
key, origin, numeric byte order and wire hints, including on a deleted entry.
Out-of-range targets retain the edit API's no-op behavior.

Version 0.2.0 changes the layout of `omc_val` to add `byte_order`. Rebuild
consumers against the new headers and library. It is not ABI compatible with
0.1.0. The library retains its C90 build contract and existing 64-bit type shim.

## Detached validation and canonical EXIF

`omc_validate_entry()` and `omc_validate_store()` inspect keys, origins,
value storage, integer ranges, rational denominators, text, XMP names and
paths, TIFF wire hints, and the initial EXIF schema. Store validation also
checks singleton duplicates, CFA dimensions/pattern consistency, and supplied
image facts. Unknown EXIF tags are allowed by default; warning and error
policies are explicit. Diagnostics use a caller-provided buffer and bounded
issue accounting. Validation does not require a finalized-store state.

`omc_serialize_exif_tiff()` emits deterministic, unwrapped, little-endian
classic TIFF. Empty output measures the required size. Short output receives
the deterministic prefix and returns `OMC_EXIF_TIFF_OUTPUT_TRUNCATED`.
Other failures leave output bytes unchanged. Input and output must not overlap.

The serializer handles supported scalar and array TIFF types, ASCII text,
opaque bytes, standard pointer directories, page IFDs, and optional SubIFDs.
It regenerates directory pointers. Unsupported IFD families and 64-bit integer
TIFF values are skipped. Opaque MakerNotes are omitted by default. Opting into
their preservation does not prove that internal offsets can be relocated.
SubIFD inclusion and minimal DNG version injection are explicit options.
Explicit TIFF type-129 hints are supported for UTF-8 Text, including one retained
terminal NUL. The serializer checks UTF-8 even with detached validation disabled;
embedded NULs, double terminators and type-129 Bytes are rejected. This does not
complete the thirteen-field EXIF text family or destination-profile validation.

Carrier framing is applied above the serializer: JPEG/TIFF/DNG handoffs use
the EXIF signature, PNG/WebP use raw TIFF, and item-oriented handoffs use the
BMFF EXIF offset/signature prefix. The direct JP2/JXL writer retains its
container-specific four-byte offset framing.

The TIFF carrier writer supports classic TIFF and BigTIFF in both byte orders.
It appends changed directories and payloads, retains unchanged child offsets,
and preserves unrelated raw entries, image-data offsets and existing page/SubIFD
links. Canonical serialization remains little-endian classic TIFF; the carrier
writer converts its supported numeric payloads to the destination byte order.

## Native selected edits and transfer (0.12.0)

`omc_native_edit_intent` owns sorted bounded removal lists: seven IFD0 tags,
85 ExifIFD tags and GPS value tags 1-31. `omc_native_edit_intent_collect()`
selects Dirty|Deleted entries; any live source owner takes precedence and clean
tombstones have no destination authority. ColorSpace and GPSVersionID are not
removal selectors. Explicit whole-Exif/GPS clear conflicts with live payloads,
selected removal or merge in that directory. Interop content also conflicts
with whole-Exif clear.

Prepare and compile copy intent inline. Native `include_exif` defaults to one
and is independent of `embedded.packet.include_exif`, which controls EXIF-to-XMP
projection. Initialize options and rebuild callers: prepare, bundle and executor
layouts changed in 0.12.0. Existing XMP store references remain borrowed.

Selected merges preserve omitted destination fields. Supplied live IFD0/Exif
values still apply; GPS merge selects only supported Dirty value entries.
Live GPS updates require one supplied BYTE/count-4 GPSVersionID matching any
existing destination version. Removal-only edits preserve the target version
until the last non-version GPS value disappears. Unchanged Interop, opaque
MakerNote payloads and image data retain their original bytes and offsets.

Source ColorSpace is filtered; explicit `target_image_spec` ColorSpace is the
authority and requests Exif merge. Existing/template DNG retains its target
DNGVersion when the source version is clean. An eligible Dirty version may
replace it in CompatibleFile mode. A fresh scaffold has separate version policy.

Transfer execution builds both output arenas privately. Any API or transfer
failure preserves previous outputs and their views. On success both outputs
are replaced; the input may alias the previous edited output. Output arenas
must be initialized and distinct. Executed-output packages preserve the result
through serialization, deserialization, materialization and callback replay.
Generic TIFF/DNG payload/package emission rejects removals, GPS merge and
explicit clears because its schema has no destination-edit envelope. Exif
merge alone, including host ColorSpace, can emit fresh metadata.

This gate covers direct selected edits and executed-output packages. Full
prepared snapshots, remaining replacement/page/SubIFD operations and other
consumer contracts remain UP6. Empty edits retain C's successful-copy policy;
there is no C equivalent of the C++ `require_updates` option.

## Explicit reverse translation

Call `omc_translate_xmp()` with a mapping mask and optional target image facts.
Defaults select dirty properties and fail on native conflicts. Preserve and
replace policies are also available. All selected groups share one transaction;
an invalid date, ambiguous alias, resource limit, or geometry mismatch prevents
publication of every group.

Since 0.15.0, creation dates accept exact positive `YYYY` and valid `YYYY-MM`
for IPTC. Unknown components become literal zeroes (`YYYY0000` or `YYYYMM00`);
no time dataset is created. Malformed partial dates remain invalid. Selected
EXIF creation mappings reject valid partial precision with
`OMC_TRANSLATION_UNSUPPORTED_PRECISION`, preserving the previous output. Select
`OMC_TRANSLATE_CREATE_IPTC` without `OMC_TRANSLATE_CREATE_EXIF` to translate a
partial `xmp:CreateDate`. Technical `ModifyDate` retains its full-date syntax.
All failed `omc_translate_xmp()` transactions report zero `groups_translated`
and `entries_added`, `entries_updated`, `entries_removed`. Diagnostic and
preserved/unchanged counts retain their existing meaning. Source and output
must be distinct initialized stores; public layouts and masks are unchanged.
Since 0.16.0, accepted original/digitized EXIF creation groups promote matching
clean native values to Dirty. Matching ignores trailing NUL padding; promotion
preserves the complete value representation, key and origin. Missing companions
retain Dirty|Deleted intent: reuse a dirty marker, promote the first clean
deleted marker, or append an empty ASCII Text marker. Added markers inherit the
XMP source origin and a saturated field-order increment. Active duplicates and
stale companions are removed under Replace. Preserve leaves a group with active
native members untouched. Promotions/removals consume operation budget; appended
markers consume both operation and addition budgets. Failed calls preserve
output and report zero mutation counts. This lifecycle increment applies only
to `OMC_TRANSLATE_CREATE_EXIF` and `OMC_TRANSLATE_DATE_ORIGINAL`; technical
ModifyDate and other mappings retain their prior rules.

TIFF/DNG transfer consumes selected native intent against an existing target.
JPEG replaces its complete EXIF packet; retain unselected fields in the source
store when they must survive. See [the lifecycle receipt](exif_dates_0819.md).

The initial groups match the bounded C++ authoring surface:

- Dates: creation/digitized, original, Photoshop creation, and modification;
  native timezone and EXIF subsecond companions; paired IPTC date/time.
- Technical EXIF: Make, Model, and Software.
- Capture EXIF: ExposureTime, FNumber, ISO, FocalLength, and ExposureBiasValue,
  using exact integer/rational conversion.
- Descriptive IPTC: default-language title, description and rights, indexed
  creators and keywords, credit and source, byte limits, and UTF-8 declaration.
- Geometry: orientation and complete stored width/height groups, checked
  against caller-supplied target facts without an orientation-driven swap.

EXIF accepts up to nine fractional second digits. IPTC translation rejects
fractional seconds instead of rounding them. Dirty tombstones remove owned
native companions under the replace policy. Indexed repeated values use
numeric index order. UTF-8 declaration conflicts and unrelated ambiguous
high-bit IPTC payloads fail before publication.

Forward XMP projection now combines IPTC DateCreated/TimeCreated and
DigitalCreationDate/DigitalCreationTime. Invalid dates are skipped. Missing
or invalid time values produce date-only output. Existing XMP conflict policy
continues to control generated-versus-existing values.

## Explicit IPTC carrier removal (0.17.0)

Native transfer `include_iptc` must be zero or one and defaults to one. It
controls native carriers independently of `embedded.packet.include_iptc` and sidecar IPTC-to-XMP
projection. Prepare, bundle and executor layouts change; initialize options
and rebuild callers with matching headers and binaries.

Prepare derives `remove_existing_iptc` for JPEG/TIFF/DNG only when native IPTC
output is enabled, no active native dataset remains, and a native dataset is
Dirty|Deleted. All active datasets count, including version, charset and damaged
entries skipped by serialization. Omission, clean tombstones and XMP-only
deletions preserve the destination carrier. A stale opaque Photoshop resource
cannot resurrect a deleted native group.

Execution rechecks the policy against the borrowed source; changing its native
lifecycle requires preparation again. Disabling native IPTC output does not
filter tag 33723 already supplied through EXIF output. Explicit removal takes
precedence over that copied tag.

Execution removes every managed leading JPEG Photoshop resource `0x0404`,
preserving unrelated resources, duplicate IDs, names, padding and segment
placement. TIFF-family execution removes IFD0 tag 33723, with removal taking
precedence over a copied EXIF tag. Existing/template DNG uses the same rule.
The existing staged-output transaction protects both caller arenas on failure.
Payload-only handoffs cannot represent destination removal and reject it;
executed-output packages persist the resulting bytes with existing wire versions.
See [the pinned removal receipt](iptc_removal_0820.md) for validation and limits.

## Explicit IPTC location writeback (0.9.0)

Call `omc_location_translation_opts_init()` and then
`omc_translate_xmp_location(source, out, &opts)`. This separate entry point
matches the five location mappings introduced by C++ 0.4.128. Existing
`omc_translate_xmp()` options, result layouts and default mappings are unchanged.
The new call uses the existing edit transaction and output publication path.

| Exact XMP property | Native IPTC dataset | Maximum UTF-8 bytes |
| --- | --- | --- |
| `photoshop:City` | 2:90 | 32 |
| `Iptc4xmpCore:Location` | 2:92 | 32 |
| `photoshop:State` | 2:95 | 32 |
| `photoshop:Country` | 2:101 | 64 |
| `Iptc4xmpCore:CountryCode` | 2:100 | 3 |

CountryCode accepts exactly two or three uppercase ASCII letters. No country
lookup or name/code agreement is imposed. Indexed, qualified, GPS and structured
location properties do not select these mappings. Logical text must contain
valid UTF-8/XML characters and use the C ASCII or UTF-8 encoding label.

The default selects all five mappings and dirty groups, with fail-on-conflict
behavior. A dirty member makes the complete exact-path group eligible; two
active values are ambiguous even when one is clean. Dirty tombstones select
removal. Replace updates the first native entry by wire order and entry ID,
then removes duplicates. Preserve retains the complete existing native group.
New entries copy source provenance; updates keep native provenance.

Non-ASCII output adds IPTC CodedCharacterSet `ESC % G` when needed. Existing
charset declarations must agree. Unowned native IPTC must be ASCII before
promotion. The text budget includes active selected source text and native
IPTC bytes inspected for promotion, including native values being replaced.

Limits default to 1024 source properties, 6 additions (five values and charset),
4096 operations and 8 MiB of inspected text. Callers may reduce them. Planning
uses five fixed records and performs no allocation. Edit construction and
publication use the existing owning arenas. The call therefore does not
promise allocation-free execution. As with the existing translator, stores
are bounded to 200000 entries. Rejection preserves source, previous output
and its borrowed views. `OMC_TRANSLATION_VALUE_TOO_LONG` reports native wire
limits; `OMC_TRANSLATION_LIMIT` covers source, entry and operation budgets.

## Combined IPTC writeback (0.10.0)

Call `omc_iptc_translation_opts_init()` and then
`omc_translate_xmp_iptc(source, out, &opts)` for one atomic transaction across
20 groups. Use only `OMC_IPTC_TRANSLATE_*` masks with this API; its
`failed_mapping` uses the same separate mask domain. The default selects all
20 groups, dirty sources and fail-on-conflict behavior. The legacy translation
and location API masks, result layouts and defaults remain unchanged.

The combined call includes the seven descriptive and five flat location
mappings above, plus these eight fields from frozen C++ 0.4.132:

| Exact XMP property | Native IPTC dataset | Maximum UTF-8 bytes |
| --- | --- | --- |
| `photoshop:Headline` | 2:105 | 256 |
| `photoshop:Instructions` | 2:40 | 256 |
| `photoshop:TransmissionReference` | 2:103 | 32 |
| `photoshop:AuthorsPosition` | 2:85 | 32 |
| `photoshop:CaptionWriter` | 2:122 | 32 |
| `photoshop:Category` | 2:15 | 3 |
| `photoshop:SupplementalCategories[n]` | 2:20 | 32 per value |
| `photoshop:Urgency` | 2:10 | 1 |

Category accepts one to three ASCII letters and preserves case. Urgency accepts
one text digit or one signed/unsigned integer scalar from 1 to 8. It rejects
floating values, arrays and other numbers. The caller associates AuthorsPosition
with the first creator; translation does not create or infer that relationship.
Wire limits count UTF-8 bytes and never truncate. Date/time fields continue to
use the legacy translation call separately.

All selected sources are validated before native conflicts are evaluated.
Dirty eligibility includes all active members of a repeated group. Positive
numeric indexes set repeated value order; equal values at distinct indexes
remain distinct. Duplicate numeric indexes are ambiguous, including `[1]`
and `[01]`. Unindexed, zero, overflowing and qualified repeated paths do not
select a mapping. Selected dirty tombstones remove native values under Replace.

Native repeated entries are reconciled by `order_in_block`, then entry ID.
Updates retain native provenance. New repeated entries copy their source
provenance but share the final overlapping native rank, or zero when no native
entry exists. Entry IDs preserve append order even at the maximum rank. The
legacy creator and keyword paths now use this same ordering rule. New singleton
entries retain source provenance. One shared UTF-8 charset preflight protects
unowned IPTC bytes before promotion, using the location rules above.

Defaults and hard maxima are 1024 source properties, 1025 additions, 4096
operations and 8 MiB of inspected text. Callers can reduce each bound. Selected
sources share one budget; native inspection is also bounded. Planning uses
20 fixed group records and bounded scratch for eligible repeated groups. The
location API shares the engine with five fixed records and no planning heap.
Edit construction and publication still allocate through owning arenas.
Failure preserves both source and initialized output, including borrowed views.
This API is not an allocation-free embedded execution contract.

Forward native projection adds Urgency, Instructions, AuthorsPosition and
TransmissionReference. It corrects CountryCode to dataset 2:100 and Country to
2:101. EXIF GPSVersionID now emits all four components, such as `2.3.0.0`, and
skips arrays with the wrong length. Primary GPS writeback remains separate work.

## Primary GPS reverse translation against C++ 0.5.10

The first GPS slice is now implemented in `omc_translate_xmp_gps()`. It uses a
separate mask domain and preserves the existing `omc_translate_xmp()` defaults.
The slice covers complete dirty XMP groups for latitude, longitude and the
altitude/altitude-reference pair. It accepts exact D/M or D/M/S coordinates,
non-negative scalar or rational altitude values, duplicate and incomplete-source
checks, GPS 2.3/2.4 version handling, conflict policies, bounded operation and
text budgets, removal tombstones, and output-preserving edit publication.

Native coordinates use unsigned DMS rationals. GPS 2.4 altitude references are
stored as 2/3 while the XMP input remains 0/1. Existing origin wire-name
provenance is copied to generated entries. A complete native GPS group is
idempotent; removal deletes GPSVersionID only after the selected GPS values are
gone. The source and initialized output stores must be distinct, as required by
the C transaction contract.

The direct fixture `omc_test_gps_translation` covers exact fractions, poles,
typed altitude values, conflicts, limits, idempotence, removal and canonical
TIFF serialize/readback. `omc_test_gps_parity` originally compared primary and
altitude cases with C++ 0.5.10 and one complete case for each additional GPS group. The
current `omc_test_gps_groups` matrix covers all four additional group APIs, all
of their native members, UTF-8 GPS text, offset-normalized timestamps, removal
and canonical TIFF serialize/readback. These are the historical v1 gates.
Version 0.12.0 adds paired v2 lifecycle cases against C++ 0.8.14 and persistence
through JPEG, LE/BE classic TIFF/BigTIFF and existing/template native DNG.

The same C++ pin also exposed one UP1 XMP reader correction. Camera/lens/spectral
text values in the EXIF and CIPA namespaces now retain boundary whitespace for
attribute, `rdf:resource` and element forms; unrelated properties keep the
existing trim behavior. `omc_test_up1` covers this rule and the classic/BigTIFF
zero-root versus malformed nonzero-root result through contiguous and callback
EXIF paths.

## GPS navigation, destination, quality and text groups

The four additional GPS APIs are bounded transactions over the same initialized
source/output contract:

```c
omc_translate_xmp_gps_navigation(source, out, &opts);
omc_translate_xmp_gps_destination(source, out, &opts);
omc_translate_xmp_gps_quality(source, out, &opts);
omc_translate_xmp_gps_text(source, out, &opts);
```

Use the separate `OMC_GPS_NAV_TRANSLATE_*`, `OMC_GPS_DEST_TRANSLATE_*`,
`OMC_GPS_QUALITY_TRANSLATE_*` and `OMC_GPS_TEXT_TRANSLATE_*` masks. The
navigation group covers timestamp, speed, track and image direction. The
destination group covers latitude, longitude, bearing and distance. Quality
covers status, measure mode, DOP, differential and horizontal error. Text covers
satellites, map datum, processing method and area information. Inputs accept
bounded decimal/rational values, exact DMS coordinates, the C++ unit aliases and
canonical ASCII- or Unicode-prefixed GPS text. The implementation injects
GPSVersionID `2.3.0.0` for every active group, preserves equivalent native
encodings, and keeps source/output unchanged on rejection.

`omc_test_gps_groups` is the C-side paired transaction and TIFF persistence
gate. Together with `omc_test_gps_translation`, it covers all 32 standard GPS
IDs. `OMC_GPS_TRANSLATION_CONTRACT_VERSION` is now 2 for all five APIs. Exact
clean native matches gain Dirty unless PreserveExisting retains their owner;
repeated dirty matches are unchanged. Complete eligible deletions add typed
Dirty|Deleted entries for absent native keys, reuse dirty intent and promote
clean tombstones. Promotions, additions and duplicate removal consume planning
budgets before publication. Version cleanup is structural, after the last
non-version member disappears. No destination version is inferred or upgraded.

The per-API addition caps are primary 7, navigation 9, destination 9, quality 6
and text 5. The shared initializer and mapping defaults are unchanged; grouped
APIs interpret its legacy default `max_added_entries=7` as the family cap.
Use each family's mask to select all its groups. Paired tests cover source and
output rollback, limits, exact-value provenance/representation, deletion and
repeat calls; see the current batch receipt for the carrier matrix.

## Unified prepared metadata patch core

`omc_metadata_patch_prepare()` creates one bounded plan for canonical EXIF TIFF
and portable scalar XMP. Requests bind a key plus occurrence and retain the
compiled value shape. `omc_metadata_patch_instance_create()` makes an independent
worker payload. `omc_metadata_patch_apply()` validates the complete update batch
before changing either payload, enforces fixed EXIF widths and escaped XMP
widths, and rejects duplicate, foreign or stale handles. `omc_metadata_patch_replay()`
calls the host synchronously in EXIF-then-XMP order.

The C plan uses a caller-issued nonzero `plan_id`, bounded request/output limits,
canonical serializers and explicit reset functions. It is the first UP2 core
slice; alias detection, exact serializer-recorded slot binding for repeated raw
values and the C++ allocation-free worker gate remain open. Host synchronization,
I/O framing and rollback of callback side effects remain outside the library.
`omc_test_metadata_patch` covers mixed-family preparation, escaped replacement,
fixed-width EXIF replacement, batch failure and replay order.

## EXIF text and version family

`omc_translate_xmp_exif_text()` uses the separate
`OMC_EXIF_TEXT_TRANSLATE_*` mask domain for `ExifVersion` (0x9000),
`FlashpixVersion` (0xA000), `UserComment` (0x9286) and `ImageTitle` (0xA436).
It accepts exact four-digit versions, `FlashpixVersion` 0100, scalar or
`x-default` UserComment, and the exact exifEX ImageTitle path. ASCII comments
use the EXIF `ASCII` marker. EXIF 3 comments use UTF-8 after the `UNICODE`
marker; older versions use a `UNICODE` marker, UTF-16LE BOM and surrogate-pair
encoding. ImageTitle requires effective ExifVersion 0300 or newer. Conflict,
dirty-tombstone, text-budget and output-preservation rules are explicit in the
status/result structures.

The reference EXIF-text API has thirteen fields; nine remain beyond these four.
They are deferred to a later UP4 group so this first family can be validated
through canonical TIFF serialization and reread without widening the existing
translation defaults. `omc_test_exif_text` covers ASCII and UTF-16LE comments,
conflicts, idempotence and canonical TIFF readback.

## Verification and remaining scope

The Clang 20 direct suite covers failure preservation, typed arrays,
validation, canonical size/prefix behavior, native carrier readback, paired
date/time projection, and translation conflicts. The focused
`omc_test_authoring_parity` test compares canonical EXIF bytes and ordered IPTC
records with C++ 0.4.127 at the original authoring checkpoint.

The historical 0.9.0 gate used frozen C++ commit
`ba99484b8be087012f9c44a1194ed828d060a0d5` (0.4.128). The direct location target
covers 70 fixtures, immutable source bytes, rejected-output preservation,
default behavior and invalid output arguments. The optional
`omc_test_location_parity` compares 69 shared fixtures, including statuses,
failure mapping/source, counters, native values, ordering and provenance. One
additional C mapping-mask rejection has no C++ boolean-option equivalent.
JPEG and TIFF replacement/removal tests persist files and reread native IPTC;
they preserve captions and prevent stale raw IRB data from restoring locations.
Clang 20 Release static with compression and shared without compression each
pass 52/52 targets. ASan/UBSan and native MSVC x64/Win32 each pass 39/39 direct
targets. Native builds retain existing CRT/decoder warnings. Clang 20 `-O3
-fstack-usage` reports a 584-byte frame for `omc_translate_xmp_location()` on
WSL x64; this measures that function, not the full call stack or embedded use.

The 0.10.0 gate uses frozen C++ `7f0ec70` (0.4.132). Its 120 direct combined
fixtures include 119 paired C/C++ cases comparing statuses, failure attribution,
counters, native bytes, order and provenance. The extra case rejects unknown
C mask bits. Direct tests also check source/output preservation, idempotence,
six legacy creator/keyword order variants, and forward projection. Four new
JPEG/TIFF replacement/removal cases cover all 20 groups, repeated growth,
unowned datasets and stale raw IRB precedence. Existing location cases remain.

Clang 20 Release static with zlib/Brotli and shared without them each pass
54/54 targets. Debug ASan/UBSan and native MSVC Release x64/Win32 each pass
40/40 direct targets. Windows retains existing CRT/decoder warnings; the new
IPTC module emits none. No new corpus, performance or embedded acceptance is
claimed by this batch. The older stack measurement above predates the shared
engine and is not a current bound.

For a frozen reference build, set `OMC_OPENMETA_DIR` to its build directory
and `OMC_OPENMETA_SOURCE_DIR` to the matching source snapshot. The latter is
optional for the usual sibling checkout. The location and combined IPTC gates are available when
the reference headers expose their contracts. When changing reference versions
in an existing build, clear `OMC_HAVE_CPP_LOCATION_TRANSLATION` and
`OMC_HAVE_CPP_IPTC_TRANSLATION`, or use a fresh build directory.

The larger legacy transfer/persist differential inventory still has known
failures. `omc_test_parity --all` explicitly enables those historical cases.
The separate reading/decoding checkpoint covers structured XMP and bounded
BMFF summaries. Broader source-processing classification and native mapping
groups beyond the pinned reference remain separate acceptance work.

The previous focused C++ reference was 0.5.10 at `8594030c`. Since the 0.10.0
C gate, the reference added transactional APEX, focal-plane/subject, exact capture,
environment, encoding, composite, structured-capture and EXIF-text/UserComment
writeback, plus shared validation for 64 reverse targets, five legal Interop
fields, three structural pointers and all 32 standard GPS tag IDs. These are
source-reviewed C-port targets; the C 0.10.0 acceptance gate remains pinned to
C++ 0.4.132 and does not claim those features.

The fresh 0.5.10 Clang 20/libc++ build used for the new work is isolated at
`/tmp/openmeta-cpp-baseline-20260920-libcxx` and uses C++ commit
`8594030c5acf0bb930a02c13c874ae25845f087f`. Its five CTest targets and the
22 focused GPS/validation tests pass. The fresh C build at
`/tmp/openmeta-c-up0-20260920` passes 49 of 57 CTest targets; the eight failing
legacy inventory targets retain the known 0.5.10 LensSpecification, rational
display, GPS destination-unit and broader source-inventory differences. The
new direct GPS, paired GPS, UP1 and existing XMP targets pass. The committed
C++ tree was used; later uncommitted EXIF 3.1 correction fields were not mixed
into this comparison.

The historical [upstream 0.5.10 roadmap](porting_plan.md#upstream-0510-convergence-roadmap)
records the initial C `metadata_patch.h` EXIF/scalar-XMP patch core. The
removed C++ EXIF-only patch API will not be ported first. Current C translation
defaults remain unchanged. Paired GPS persistence, the remaining committed
capture/identity families, structured capture and the EXIF 3.1
development/correction family have separate planned batches. The intended XMP
name for the latter retains the C++ spelling `DevelopmentCharacterstic`; the
host policy for `ExifVersion` 0300 versus 0310 must be explicit.

## Native lifecycle implementation and remaining target

Translation now needs to preserve an edit's meaning when source and destination
metadata differ. The implementation gate covers each owning group and policy:

- Omitted or ineligible sources preserve destination values. PreserveExisting
  keeps an active native owner, including an exact match, without promotion.
- Accepted exact clean native values gain Dirty while retaining value bytes,
  encoding, rational components, wire hints/counts, origin and byte order.
- Complete eligible deletion retains native Dirty|Deleted intent even when
  native keys are absent. Reuse existing dirty intent and promote clean
  tombstones under the family's documented policy. Development/learning absent
  deletion requires ReplaceExisting; do not apply one generic policy to all APIs.
- Preflight counts promotions, additions and duplicate removal. Repeated calls
  do not grow intent; late failures preserve source and output. Separate C
  translation calls require host staging before aggregate publication.

The C GPS implementations and direct selected-edit foundation meet these
bounded lifecycle gates in 0.12.0. Remaining authoring families must adopt the
same contracts as they are ported. Source snapshots and the remaining prepared
stream/package consumers are separate UP6 gates; direct acceptance does not
establish them.

C++ 0.8.14 and this C gate have seven IFD0 removal tags, 85 ExifIFD removal tags
and GPS value tags 1-31. Keep whole-directory clearing distinct from selected removal.
GPS active merge requires a supplied version equal to the destination version;
removal-only updates preserve it while other GPS values remain. Source filtering
does not request destination redaction. Existing/template DNG keeps its target
version unless an eligible dirty replacement applies; only a minimal fresh
scaffold synthesizes a missing version.

Port the remaining nine EXIF-text fields and the current version/charset rules
with retained-NUL wire counts, type-129 text and BOM preservation. Then add the
six development fields and LearningOptOutIn with explicit host policy 300/310,
exact structured paths and checked numeric endian conversion. The five-field
profile route (ImageDescription, Artist, scalar Copyright, ColorSpace,
RelatedSoundFile) is an additional v2 transaction in the current C++ pin and
remains C work. LightSource remains contract v1. Detached validation
does not establish a complete destination EXIF profile, ICC or audio authority.

At the current 0.8.14 pin, profile translation is v2 and its native lifecycle is
implemented in C++. RelatedSoundFile raises the removal allowlist to 85.
ColorSpace is host-owned: explicit destination image facts may replace it,
source filtering/deletion cannot. Add the shared opt-in destination-profile
validator with UP4 and bounded structured XMP editing with UP5.

## Canonical IPTC-IIM and transfer (0.13.0)

`omc/omc_iptc_serialize.h` exposes contract 1 through
`omc_serialize_iptc_iim()`. Initialize limits with
`omc_iptc_iim_serialize_opts_init()`: 200,000 active datasets, 8 MiB per dataset,
64 MiB total output. Limits must be nonzero; total output must fit the arena's
32-bit span range and `omc_size`.

An empty output measures without allocating sort or payload storage. A short
output receives the exact canonical prefix after full preflight. Other errors
leave output bytes unchanged. Output may not overlap the source store or its
arena, entry or block storage. The C store has no finalized-state bit; callers
supply an initialized, structurally valid store and keep it immutable during
serialization. Writing allocates a bounded descriptor array during preparation.

Deleted entries and other metadata families are ignored. Active IPTC entries
must have byte-sized record/dataset keys, valid counts/spans and supported
Bytes, Text or Array values without unreadable/truncated flags. Serialization
preserves raw stored bytes, including empty values, duplicates and numeric
array byte order. It does not transcode or certify field semantics. Charset
1:90 sorts first, followed by source order, record, dataset and entry ordinal.
Lengths through 32767 use two bytes; larger lengths use `0x8004` and a big-endian
32-bit length.

JPEG/TIFF/DNG native IPTC rebuilds use the same wire engine. Transfer skips
malformed or unsupported native entries and reports their count through
`iptc_entries_skipped` in transfer/payload-build/package-build results. These
are build diagnostics; they are not persisted in package wire data. Resource
limits remain fatal. Clean opaque Photoshop resource 0x0404 retains precedence;
any dirty native IPTC entry, including deletion intent, suppresses that stale
fallback. Opaque fallback bytes are outside canonical dataset validation.
If no live native datasets remain, no IPTC update is emitted and the existing
destination carrier remains; source deletion alone is not destination redaction.
C empty payload preparation succeeds with no payload, while C++ reports
IptcPackFailed (fatal for IPTC-only preparation, a warning with successful other
families). The C skipped-entry counter reports damaged entries, not that
C++ mixed-family diagnostic policy.

JPEG replacement validates complete Photoshop 8BIM resource streams before
allocating the replacement output. Pure non-IPTC APP13 segments remain byte
identical. Each mixed carrier retains its own unrelated records, including
names, duplicate IDs and padding, in source order. Every old IPTC resource is
removed; the new carrier replaces the first affected carrier, or is inserted
before SOS/EOI if none exists. Carriers are not merged. Post-SOS bytes are copied
without inspection. With no IPTC update, resource inspection is bypassed.
Public execute failures preserve both output arenas, including input aliasing
old edited output. Executed-output packages retain the resulting bytes through
serialization, restoration, materialization and replay. This does not add a
C++-style in-place JPEG planner or full signed C2PA support.

## Native IPTC to materialized XMP (0.13.0)

`omc_translate_iptc_to_xmp()` implements contract 1 for the same twenty mapping
identities as `omc_translate_xmp_iptc()`. Initialize its separate options with
`omc_iptc_to_xmp_translation_opts_init()`. Defaults select all mappings and all
sources with FailOnConflict. DirtyOnly selects whole groups affected by native
edits or their block's dirty charset marker. Native date/time pairs are outside
this forward contract; the existing reverse date/time behavior is unchanged.

The source contains both native IPTC and existing destination XMP. On success,
`out` receives the complete translated store; its previous contents are replaced.
Source/output identity is supported through staged commit. Failure preserves
both stores. The operation allocates bounded planning storage and a temporary
store; it is not an allocation-free execution API.
Both stores must be initialized and structurally valid, including their value
enum fields. Irrelevant valid encoding/byte-order hints do not affect IPTC text
interpretation and remain intact on retained native entries; invalid enum
values can still be rejected by the core transactional clone.

The translator accepts ASCII or per-block `ESC % G` declared UTF-8, validates
selected values and field limits, preserves repeated values in block/order/
entry-ID order, and retains duplicates. Raw Bytes need no text encoding hints.
LangAlt mappings own only `x-default`; other languages and unrelated metadata
survive. New XMP values are Dirty UTF-8 with source provenance and no native
wire hints. Updates retain destination provenance. Fail, Preserve and Replace
policies govern existing destination groups; malformed selected source values
still fail under Preserve. Unsupported destination shapes fail unless the
policy preserves the existing group without inspecting its value.

Default hard ceilings are 1024 selected source properties, 1024 additions,
4096 destination values/operations and 8 MiB of selected source plus inspected
owned destination text. Aggregate text sizes are checked before UTF-8 scanning.
Late errors and budget failures publish no partial transaction. Use the result's
status and failed mapping/source to diagnose rejection.

## Native IPTC date/time to materialized XMP (0.14.0)

`omc_translate_iptc_dates_to_xmp()` implements the separate C++ 0.8.18 date
contract. Initialize `omc_iptc_date_to_xmp_translation_opts` with
`omc_iptc_date_to_xmp_translation_opts_init()`. Both selectors default to true,
with All sources and FailOnConflict.

| Selector | Native date/time datasets | XMP property | Failure mapping ID |
| --- | --- | --- | --- |
| `created` | 2:55 / 2:60 | `photoshop:DateCreated` | `OMC_TRANSLATE_DATE_CREATED` |
| `digital_creation` | 2:62 / 2:63 | `xmp:CreateDate` | `OMC_TRANSLATE_CREATE_IPTC` |

Dates contain exactly eight ASCII digits. Positive-year `YYYY0000` emits
`YYYY`; `YYYYMM00` emits `YYYY-MM`. Full dates must be Gregorian-valid. Unknown
years, a known day without a month, or time attached to a partial date report
UnsupportedPrecision. Invalid syntax or calendar values report InvalidDatetime.
Time accepts `HHMMSS` or `HHMMSS+/-HHMM`, with seconds through 60 and offsets
through 23:59. It preserves the offset sign, including `-0000` as `-00:00`.
No timezone conversion or leap-second-event validation occurs. Fractions,
whitespace and trailing NUL/padding are rejected. Numeric Bytes ignore text
hints; Text requires ASCII or UTF-8. IPTC charset markers do not affect selection
or interpretation of these numeric fields.

DirtyOnly selects a pair when either member is dirty and includes its clean
active companion. Multiple active dates or times are ambiguous even when equal.
Active members must share a valid block or the authored invalid-block sentinel.
A time without a date is incomplete. Clean tombstones are ignored. A dirty time
tombstone reduces an active pair to date-only; a dirty date tombstone requests
destination removal only if no active member remains. Active replacements win
over same-field tombstones. Missing inputs alone do not remove destinations.

Selected dirty tombstones count toward the source limit, but their payload,
origin and known damage flags are ignored. Unknown C entry flags still fail.
The date module retains tombstones and unrelated entries verbatim in a private
copy of the used arena, entry array and block array. Unlike generic edit commit,
it does not inspect ignored refs while copying. The C and C++ flag layouts are
different; use C flag constants, not C++ numeric flag values. Existing generic
edit and twenty-field forward-translation contracts are unchanged.

FailOnConflict accepts an absent target or exact lexical equality. Preserve
keeps a recognized root without inspecting its value, after source validation.
Replace updates/removes one exact supported scalar target. Duplicate targets,
indexed/structured descendants, damaged values, malformed refs and unsupported
encodings fail under Fail/Replace. Other roots and namespaces survive. New XMP
is Dirty UTF-8, with the date source's block/order and no native wire hints;
updates retain destination provenance.

The positive hard ceilings are 1024 selected native entries, two additions,
two operations and 8192 inspected source plus owned-destination text bytes.
Options may lower them. Both selectors may be disabled for a no-op copy.
Validation and size planning precede allocation/publication. The caller provides
initialized stores with valid aggregate storage; distinct stores own independent
storage. Source/output identity is supported. Failure preserves both stores,
and success replaces the output store. Preparation allocates in proportion to
the complete source store; the text/operation caps are not a total memory bound.

Serialize the materialized properties with `include_existing_xmp=1`,
`include_iptc=0`, `include_exif=0`. This operation does not create EXIF dates or
change the existing portable projection. Since 0.15.0, the reverse-date API
round-trips year/month-only XMP into native IPTC. EXIF creation output still
requires a full date and time. Replacing a nonempty IPTC payload removes stale
destination time datasets while retaining unrelated IPTC and EXIF values.
Deleting the last active IPTC dataset currently omits the replacement payload
and preserves an existing destination carrier; it does not request carrier
removal. See [the reverse-date receipt](reverse_dates_0819.md).
