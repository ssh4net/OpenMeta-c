# Bounded BMFF writing

## 2026-10-14 correction

The retained-`dimg` gap below is fixed against C++ 0.8.14 `fc2052b`. A retained
source cannot lose a nonzero input during remapping; removing the source drops
its relation. Mixed ordered repeated targets survive two edits with `iref`
versions 0 and 1. Direct and serialized package success matches the low-level
writer; failure leaves the target unchanged. The low-level writer preserves
pre-existing output on failure; package materializers reset output and publish
no candidate on failure, as their existing API contract requires.

The same fixture requalifies bounded method-2/local-`dref`, old-meta/free,
metadata growth and media preservation. This does not qualify every C++
adjacent-free or CR3 relocation case. See [the receipt](up0_up1_0814.md).

## 2026-10-11 comparison with C++ 0.8.11

The implementation described below retains its older acceptance pin. The new
[roadmap](porting_plan.md#upstream-0811-convergence-roadmap) requires these
focused checks before current writer parity can be claimed:

- C already preserves retained reference order and repeated occurrences, but
  `omc_bmff_build_iref()` skips every endpoint that remaps to zero. C++ 0.8.1
  rejects a removed input of a retained `dimg` source. Add that rejection and
  retain the existing whole-relation removal when its source is removed.
  Verify repeated grid/overlay inputs and repeated edits in direct and package
  routes; do not deduplicate an ordered image relation.
- Requalify method-2 references to method-0/1 items with validated local
  `dref`, the newer adjacent free-box/meta-growth cases, association order and
  managed endpoint remapping. C's append layout can satisfy preservation with
  different output bytes; compare semantics and source ranges, not C++ layout.
- Qualify CR3 against its newer CTBO/CRAW location facts. The C++ writer rejects
  any relocation of retained top-level CR3 boxes. C's appended metadata usually
  retains old offsets, but that alone does not prove every route or referenced
  box identity. Check old absolute targets and media ranges, rejecting unsafe
  edits before publishing output. Reading CTBO/CMP1 does not authorize rewrite
  of opaque Canon records or private checksums.

The first item is a confirmed source-level behavior gap. The remaining items
need differential fixtures; this review did not run new writer tests. Existing
compact `iloc`, multiple-`ipma`, local-reference and append-layout work remains
implemented.

## Existing bounded C behavior

HEIF, AVIF, and CR3 EXIF/XMP/ICC writes and transfer-package materialization
share one private writer. Inserting EXIF, XMP, JUMBF, or C2PA replaces existing
items in that family. A family with exactly one removed and one inserted item
gets an ID mapping. Other removed IDs are dropped from retained references,
version-0 entity groups, and property associations. New metadata gets `cdsc`
links to the primary item when one exists.

The writer accepts compact `iloc` fields, normalizes output offset/length
widths to four or eight bytes, and preserves retained construction methods,
bases, and self-contained `url `/`urn ` data references. External references,
omitted lengths, recursive method-2 sources, and method-2 links to removed
items remain unsupported. Package item insertion requires an existing primary
item graph. Direct metadata writers retain support for minimal metadata-only
containers.

ICC replacement rebuilds `ipco` and consolidates every supported `ipma` table.
Rows and associations retain first-seen order. Duplicate associations merge
their essential bits. Index widths and item-ID widths expand as required.
Removed ICC properties map to the new profile for affected items and the
primary item. Index zero means no association. Malformed secondary tables
fail the operation.

The output appends a new `meta` box and turns the old one into `free`.
Retired table bytes are cleared; live method-0 bytes retain their original
file addresses. Method-1 data remains relative to the copied `idat` payload.
Replaced payload ranges in that copy are cleared. This is metadata replacement,
not a secure-erasure API. Unreferenced opaque bytes and older free space can
remain. Repeated writes grow the file; compaction is a separate future operation.
This layout deliberately does not promise byte identity with the C++ writer.

Bounds are 64 MiB per metadata box, 65,536 input-plus-new item records,
1,048,576 extents, 4,096 property-association tables, 1,048,576 combined source
rows, 4,194,304 associations, 255 associations per output row, and 32,767
properties. Multiple `ipco` boxes and replacement of the primary item are
unsupported. Only validated candidate output is published by the shared writer.

Version 0.2.1 validation used Clang 20: 35/35 Release direct plus focused
authoring differential targets and 34/34 ASan/UBSan direct targets passed.
Synthetic checks parse the resulting item locations, references, groups, and
property associations. The broader historical differential inventory and native
Windows acceptance remain separate gates.
