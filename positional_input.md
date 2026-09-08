# Positional input

Version 0.3.0 adds `omc_source.h` and `omc_read_source.h`. Sources borrow either
contiguous memory or a synchronous host `read_at` callback. The source has a
fixed size and must remain unchanged for the operation. The library owns no
file handle and installs no locks. Each concurrent operation needs its own
state, windows, workspace, and destination store.

## Exact reads and views

Initialize `omc_source_state` before each operation. Its first failure is sticky.
Exact reads check ranges and budgets before calling the host, make no short-read
retry, and account requested and completed bytes separately. A callback may
partially overwrite its destination on failure. Unknown callback status codes
and reported byte counts exceeding the request are contract violations.

Default limits are 65,536 requests, 64 MiB total requested bytes, and 16 MiB per
request. Zero disables the corresponding ceiling; accounting overflow still
fails. Memory copies consume the same exact-read budget. Borrowed memory views
and window cache hits consume no I/O budget.

`omc_source_read_view()` uses caller-owned cache storage for callbacks. Its
read-ahead size is explicit and is capped by storage, range, and I/O limits.
Reset the window when changing its source/range or starting another operation.
A refill invalidates earlier cache views. Memory views retain the input's
lifetime. Argument/range checks and preparatory view failures identify the
range-relative request. Exact-read budget failures and dispatched I/O failures
identify the absolute backing-source offset.

## Reader and payload APIs at 0.8.0

`omc_scan_source()` shares the memory scanners' framing, item and extent rules.
Pass `OMC_SCAN_FMT_UNKNOWN` for format selection or select a supported family.
`omc_scan_meas_source()` counts descriptors without storing them. Structural reads
use fixed local buffers and bounded existing BMFF tables. They never prefetch
unknown image bodies. Limits remain cumulative across all reads; byte-wise
string parsing can consume more requests than metadata collection did.

`omc_pay_ext_source()` assembles range-relative descriptors into caller output.
Uncompressed extraction reads only the accepted prefix. Measurement needs no
uncompressed payload bytes. GIF framing and compressed streams require reads.
A nonempty `omc_pay_source_workspace.stream` supplies bounded zlib/Brotli feeds;
the output limit bounds expansion. Backend allocations retain their existing
behavior. JPEG ICC/JUMBF sequence parts, extended-XMP GUID/offset parts and
BMFF file extents share the memory extraction rules. Extended-XMP parts must
cover the declared logical range without gaps or overlap.

`omc_read_source()` supplies the high-level path. Its workspace contains metadata,
payload, descriptor, IFD and multipart-index buffers. Callback decoding uses a
candidate store to preserve the original store on I/O, capacity or allocation
failure. Store cloning and metadata decoders can allocate; scanner/payload access
does not allocate a source snapshot. Source, workspace and stores must be disjoint.
Memory sources retain the existing contiguous reader and its partial results.
Inspect all decoder statuses and residual fields; top-level success does not
establish complete nested enrichment or container validity.

Supported high-level callback families:

- TIFF/BigTIFF/DNG, RW2 and ORF: direct directories and tag values. Pixel pointers
  remain metadata. Inline values use eight local bytes; one value or combined
  GeoTIFF parameters use metadata scratch. Tested source-relative MakerNotes
  reuse typed decoding. `value_scratch_needed` and `nested_payloads_skipped`
  report incomplete access; uncommon vendor layouts remain parity work.
- JPEG: metadata segments and multipart payloads; stop at SOS/EOI and skip
  nonmetadata segments. Embedded uncompressed TIFF uses positional values.
- PNG/WebP: chunk traversal and logical metadata assembly, including split
  JUMBF/C2PA. No aggregate chunk snapshot. PNG text framing currently requires
  one complete text chunk in metadata scratch. CRC contents are not validated.
- JP2/JXL: box and UUID metadata, ICC, EXIF, XMP and compressed metadata.
  Skip codestream boxes. Compression remains optional.
- BMFF HEIF/AVIF/CR3: bounded `iinf`, `iloc`, `iref`, `dref`, property and item
  tables with construction methods 0/1/2 and remapped extents. Scene/component,
  grid/overlay/identity/tile and primary/display summaries share memory decoding.
  CR3 CMT3 uses typed Canon decoding when MakerNotes are enabled.

- Standalone XMP: a bounded prefix probe followed by one packet in metadata
  scratch. XMP input, namespace, attribute and arena limits remain enforced.
- GIF: comment, XMP and ICC application sub-blocks share payload assembly.
  Image sub-block bodies are skipped using their declared lengths.
- EXR: `omc_exr_dec_source()` traverses multipart headers and decodes one
  attribute value in metadata scratch. A NULL store measures attributes without
  fetching their bodies. Stop before image offset tables and pixel data.
- CRW/CIFF: traverse directory records, nested directories and declared value
  leaves at their original offsets. Use eight local bytes or one caller value
  buffer; no whole-directory scratch span. CIFF raw leaves below the configured
  value limit are retained, as in C++; this is not a pixel-tag exclusion policy.
- RAF/X3F: native header, directory and property values plus declared embedded
  JPEG/TIFF metadata. RAF supports both native directories and both TIFF range
  pairs; X3F supports header extensions and directory-declared PROP and JPEG
  image sections. Embedded JPEG traversal stops at SOS/EOI.

For callback RAF/X3F, `undeclared_searches_skipped == 1` reports omission of the
optional memory prefix search. It does not assert that an entry is missing.
The fixed RAF TIFF probe at offset 160 remains supported when no TIFF range is
declared. Native-only input can have `scan.written == 0` and decoded EXIF entries;
the high-level scan status becomes OK if native decoding produced entries.
Native fields use an empty source block, matching C++. Memory RAW reads can now
include native fields and an additional native block. Consumers must rebuild
for the added source-result field in 0.7.0.

Block coordinates are relative to the supplied range. Nested blocks are remapped
once. Exact-read I/O failure offsets are absolute in the backing source. There
is no whole-image callback fallback.

## Verification

Clang 20 Release passes 44 direct/focused targets; the compression-disabled build
passes the same 44 targets, and ASan/UBSan passes 38 direct targets. LeakSanitizer
runs outside the ptrace sandbox. Existing scanner and payload tests now compare
memory and callbacks, including seven-byte compressed feeds. The RD2 gate has
80 C callback/C++ contiguous TIFF fixtures. RD3 adds 17 C memory/callback decoded
container fixtures plus eight C/C++ callback scanner/payload cases: JP2/JXL,
split BMFF extents at zero/8 GiB gaps, JPEG ICC and extended XMP. Callbacks reject
pixel/media reads. RD4 adds 18 C memory/callback versus C++ decoded fixtures,
including five comparisons against the public C++ EXR source decoder, plus five
C/C++ callback RAW/GIF scanner and payload cases. Nine direct access fixtures
cover EXR, CIFF, RAF and X3F at zero/3 GiB gaps and GIF raster skipping.
Tests include partial output, overlap, malformed framing,
I/O failure, cancellation, budgets and preservation of populated stores.

Fixed PNG/WebP counters after RD3 (zero or 3 GiB gap):

| Fixture | Requests | Bytes requested | Metadata scratch used |
| --- | ---: | ---: | ---: |
| PNG | 70 | 525 | 31 |
| WebP | 44 | 533 | 0 |

Payload, structural stack, backend and store allocations are separate. These are
sparse-access observations, not throughput or embedded-memory acceptance.
See [read_decode_parity.md](read_decode_parity.md) for the pinned C++ reference,
logs and remaining acceptance work.

The final RD4 run fixes an RD3 failure in the compression-disabled build:
a later unsupported backend no longer hides an earlier payload-capacity failure
from the high-level source transaction. The saved RD3 log was 41/42, despite the
previous passing note. Current validation includes this regression.


## RD6 resource and platform checkpoint

Version 0.8.0 matches 208 public C++ reading cases through positional input and
all 69 selected corpus files. Native Windows x64 and Win32 pass the 38 direct
C targets without compression backends. Win32 retains size-conversion warnings
in EXIF helpers. See [read_decode_parity.md](read_decode_parity.md)
for the full gate scope and the 138 memory/callback corpus executions.

A Phase One MakerNote spanning a 2 GiB image gap requires 21 requests, 162 bytes
and 32 bytes of metadata scratch. The shared reader fetches its header,
directory and individual values without materializing the intervening image.
The existing 5 GiB TIFF/BigTIFF/JPEG sparse cases retain 12/12/36-byte scratch.
Callbacks reject gap and image-body reads in these fixtures.

These access counters exclude decoder stack and owning-store allocations.
Clang 20 `-O3` reports a 199832-byte BMFF frame and a 59912-byte JUMBF frame;
maximum call-chain stack use is not measured. Explicit caller structural
workspace is still required before small-stack embedded qualification.
XMP and JUMBF measurement also allocate temporary stores; they are not
allocation-free sizing APIs. Current 32-bit Windows success does not qualify a
microcontroller ABI or memory budget.
