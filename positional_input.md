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

## Initial reader conversion

`omc_read_source()` takes an `omc_read_source_workspace` containing metadata,
payload, block, IFD, and multipart-index buffers. It performs no allocation for
source collection. The existing store and decoders retain their normal bounded
allocation behavior. Source, workspace, and store storage must not overlap.

Memory sources use the existing contiguous reader for all its formats.
Callback sources currently support:

- JPEG: collect APP/COM segments, skip other segment payloads, stop at SOS/EOI,
  and reuse the existing multipart and metadata decoders. MakerNote enrichment
  inside complete EXIF APP payloads uses the existing decoder when requested.
- TIFF, BigTIFF, and DNG with TIFF magic 42/43: read directory tables, pointer
  arrays, and referenced tag values. Strip, tile, and preview-offset tags remain
  metadata values; their image addresses are not followed. Embedded metadata
  stored as tag values remains available to existing decoders.
- PNG (0.4.0): collect `eXIf`, `iCCP`, text and `caBX` metadata chunks, skip
  image bodies and stop at `IEND`. Chunk lengths include CRC storage, but the
  metadata scanner does not validate CRC values or require `IEND` before EOF.
- WebP (0.4.0): collect `EXIF`, `XMP `, `ICCP` and `C2PA` chunks within the
  declared RIFF extent, bounded by source size. Honor odd-length padding and
  skip image bodies. PNG/WebP reuse the contiguous family decoders, including
  compressed payload handling and split JUMBF assembly.

JPEG/PNG/WebP collection retains at most 1,024 metadata segments/chunks. Their
aggregate metadata and framing must fit `metadata_capacity`; logical payloads
must also fit the separate payload scratch. TIFF collection visits
at most 1,024 unique IFDs, also constrained by the EXIF limits (default 128 IFDs).
The TIFF scratch snapshot retains original TIFF offsets. Its highest referenced
metadata byte must fit `metadata_capacity`, even for a small value far into the
file. Only referenced ranges are requested from the host; gaps are initialized
in caller scratch. There is no whole-file callback fallback.

Callback TIFF MakerNote enrichment is explicitly unsupported when requested
and a MakerNote is present. Disable enrichment to retain its raw bytes. This
avoids interpreting uncollected vendor offsets as metadata. Source-native TIFF
value access and additional vendor offset layouts are the next TIFF increment.
Other callback container families return unsupported until converted.

Block offsets remain relative to the supplied range, matching the contiguous
reader and the C++ positional scanners. Collection/I/O failure leaves the store
unchanged. Once decoding starts, the existing partial-result contract applies:
check `decoded` statuses as well as the source-reader result. These are metadata
readers, not image-validity checks; the JPEG scanner retains its existing
acceptance of metadata ending at EOF before SOS/EOI.

## Verification and next increments

Clang 20 direct tests compare memory and callback results for both TIFF byte
orders, Classic/BigTIFF, DNG tags, raw MakerNotes, and JPEG MakerNote enrichment.
Callbacks reject attempted access to the fixtures' image-data ranges. Focused
C++ differential tests compare exact-read failures/accounting, cached views,
JPEG block coordinates, and canonical EXIF bytes after positional TIFF decode.
PNG/WebP tests compare all decoded C entries between memory and callbacks,
then compare scanner fields, logical payload bytes and decoded records with
C++. Split JUMBF chunks straddle a 3 GiB virtual image gap. Tests also cover
exact/insufficient scratch, minimal headers, odd padding, malformed lengths,
I/O budgets and preservation of populated stores on collection failure.

Fixed synthetic sources report a virtual size of 5 GiB:

| Fixture | Callback calls | Requested bytes | Metadata scratch span used |
| --- | ---: | ---: | ---: |
| Classic TIFF, either byte order | 5 | 100 | 180 |
| BigTIFF, either byte order | 6 | 166 | 180 |
| JPEG with EXIF/Nikon MakerNote | 7 | 158 | 150 |

The scratch-span column is not process peak memory. Tests provide 2 KiB metadata
and 2 KiB payload buffers; collection also has fixed structural arrays totaling
at most 32 KiB across active frames. Decoder/store allocations are separate.
The complete fixed-fixture test rounded to 0.00 seconds with `time -p`; this is
functional I/O evidence, not a throughput benchmark.

Next: replace the bounded TIFF scratch snapshot with direct value windows and
convert the first external MakerNote offsets; then add reusable source scanner
and multipart payload operations. JP2/JXL/BMFF, GIF/EXR, and native RAW readers
follow. See [read_decode_parity.md](read_decode_parity.md) for the pinned
reference, ordered batches and acceptance gates.
