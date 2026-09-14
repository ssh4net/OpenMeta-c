# Typed authoring and native translation

The public C authoring operations use the existing store, arena, value, and
edit types. They require no file handle or C++ runtime.

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

Carrier framing is applied above the serializer: JPEG/TIFF/DNG handoffs use
the EXIF signature, PNG/WebP use raw TIFF, and item-oriented handoffs use the
BMFF EXIF offset/signature prefix. The direct JP2/JXL writer retains its
container-specific four-byte offset framing.

The TIFF carrier writer appends replacement root, EXIF, GPS, and interoperability
directories while retaining unrelated raw entries, image-data offsets, and
existing page/SubIFD links. It supports little-endian classic TIFF and BigTIFF.
Big-endian TIFF carrier rewriting remains unsupported. Canonical serialization
and byte-order-aware decoding are separate from that carrier restriction.

## Explicit reverse translation

Call `omc_translate_xmp()` with a mapping mask and optional target image facts.
Defaults select dirty properties and fail on native conflicts. Preserve and
replace policies are also available. All selected groups share one transaction;
an invalid date, ambiguous alias, resource limit, or geometry mismatch prevents
publication of every group.

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

The [upstream 0.5 roadmap](porting_plan.md#upstream-05-convergence-roadmap)
now targets `metadata_patch.h` for a future C EXIF/scalar-XMP patch primitive.
The removed C++ EXIF-only patch API will not be ported first. This is planned
UP2 work; the current C serializers and transfer replay are not patch workers.
Current C translation defaults remain unchanged. Later GPS, capture/identity
and structured-location contracts have separate planned batches.
