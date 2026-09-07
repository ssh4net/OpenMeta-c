# Typed authoring and native translation

The public C authoring operations use the existing store, arena, value, and
edit types. They require no file handle or C++ runtime.

## Lifetime and failure contracts

Initialize every output store before calling `omc_edit_commit()`,
`omc_store_compact()`, or `omc_translate_xmp()`. The output must be distinct
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

## Verification and remaining scope

The Clang 20 direct suite covers failure preservation, typed arrays,
validation, canonical size/prefix behavior, native carrier readback, paired
date/time projection, and translation conflicts. The focused
`omc_test_authoring_parity` test compares canonical EXIF bytes and ordered IPTC
records with C++ 0.4.127.

The larger legacy differential harness has known failures and is not a green
acceptance gate. `omc_test_parity --all` explicitly enables its historical
transfer/persist cases. Nested structured XMP decoding, broader source
processing classification, rich BMFF summaries, and later native translation
groups are not established by the authoring gate.

