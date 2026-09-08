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

## Reader and payload APIs at 0.6.0

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
  tables with construction methods 0/1/2 and remapped extents. Existing bounded
  structural interpretation is shared. Richer C++ semantic output differences
  remain in RD5; source conversion does not remove them.

Block coordinates are relative to the supplied range. Nested blocks are remapped
once. Exact-read I/O failure offsets are absolute in the backing source. There
is no whole-image callback fallback. Native GIF/EXR/RAF/X3F/CRW integration is RD4.

## Verification

Clang 20 Release passes 42 direct/focused targets; the compression-disabled build
passes the same 42 targets, and ASan/UBSan passes 37 direct targets. LeakSanitizer
runs outside the ptrace sandbox. Existing scanner and payload tests now compare
memory and callbacks, including seven-byte compressed feeds. The RD2 gate has
80 C callback/C++ contiguous TIFF fixtures. RD3 adds 17 C memory/callback decoded
container fixtures plus eight C/C++ callback scanner/payload cases: JP2/JXL,
split BMFF extents at zero/8 GiB gaps, JPEG ICC and extended XMP. Callbacks reject
pixel/media reads. Tests include partial output, overlap, malformed framing,
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
