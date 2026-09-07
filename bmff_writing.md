# Bounded BMFF writing

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
