# OpenMeta-c Read/Decode Layer — Security Code Review

**Scope**: All source files in `src/read/` plus supporting headers  
**Focus**: Correctness, memory safety, malicious input handling, integer overflow, buffer overflow, DoS  
**Date**: 2025-07-17

---

## Executive Summary

The OpenMeta-c read layer parses **untrusted binary file data** across 13 container
and metadata formats. The code is overwhelmingly well-written: every byte-read
helper validates offset + size against the buffer, overflow-checked multiplication
is used consistently in EXIF, all decoders expose configurable limits, and error
status is propagated faithfully. The architecture (scan → payload → decode) keeps
each stage isolated.

Three areas need attention:

| Severity | Count | Summary |
|----------|-------|---------|
| **HIGH** | 3 | EXIF IFD cycle detection, XMP allocation overflow, CIFF localtime() |
| **MEDIUM** | 6 | MakerNote depth, XMP u32 cursor, bubble-sort, SKILL.md contract items |
| **LOW** | 5 | Defensive depth, truncation style, minor clarification items |
| **INFO** | 5 | Positive observations and design notes |

No **CRITICAL** (remote code execution) vulnerabilities were found.  
The three HIGH items are bounded DoS or hardening gaps, not memory corruption.

---

## Per-File Findings

### 1. `omc_exif.c` — EXIF / TIFF Decoder (14 010 lines)

#### H-1 [HIGH] No IFD-offset cycle detection  
**Lines**: 13874-13900, 13960-13964  
The main loop pops tasks from a 256-slot ring buffer.  Each IFD's "next IFD"
pointer is pushed unconditionally (line 13895).  A crafted TIFF whose IFD0 sets
`next_ifd = offset_of_IFD0` will push the same offset repeatedly.

**Mitigation already present**: `max_ifds` (default 128) caps total IFDs
processed, and the 256-slot task queue rejects further pushes once full.  So the
loop is **bounded** — it terminates after at most 128 IFD visits — but all 128
visits re-parse the same data, wasting CPU.

```
Worst-case work = max_ifds × max_entries_per_ifd = 128 × 4096 = 524 288 entries
```
Further bounded by `max_total_entries` (200 000).

**Recommendation**: Track a small set of visited IFD offsets (even a fixed array
of 128 `omc_u64` values) and skip duplicates.  This converts the bounded DoS into
O(1) rejection.

---

#### M-1 [MEDIUM] MakerNote recursion has no explicit depth limit  
**Lines**: 2463-2503 (`omc_exif_decode_tiff_stream`)  
MakerNotes can embed full TIFF blobs. `omc_exif_decode_tiff_stream` creates a
child `omc_exif_ctx` (≈ 7 KiB on stack, including the 256-task array) and
processes it.  A recursive MakerNote (tag 0x927C → TIFF with tag 0x927C → …)
would nest contexts on the C stack.

**Mitigation already present**: Each child context has its own `max_ifds` /
`max_total_entries` limits, and the child inherits the parent's running entry
count (line 2449, `child.res = parent->res`).  So the global entry limit prevents
unbounded recursion depth.  Practically, 3-4 levels exhaust the entry budget.

**Recommendation**: Add an explicit `depth` counter passed through child contexts,
with a hard limit (e.g., 4).  The ~7 KiB stack frame per level means 4 levels ≈
28 KiB; more would risk stack overflow on embedded targets.

---

#### M-2 [MEDIUM] Bubble-sort of multi-part indices is O(n²)  
**File**: `omc_pay.c:521-539`  
```c
for (a = 0U; a < part_count; ++a) {
    for (b = a + 1U; b < part_count; ++b) { … swap … }
}
```
`part_count` is capped by `max_parts` (default 16 384).  Worst case is
16 384² / 2 ≈ 134 M comparisons.

**Recommendation**: Use insertion sort (same complexity class but practical
constant is lower for small n) or, if the cap stays ≤ 16 K, document the cost.

---

#### L-1 [LOW] SubIFD count fed directly from file-sourced `count`  
**Lines**: 13837-13862  
The loop `for (sub_index = 0U; sub_index < count; …)` iterates over the IFD
entry's TIFF count value.  Each iteration pushes a task.  The task queue cap (256)
and `max_ifds` prevent runaway, but a crafted `count = 0xFFFFFFFF` could spin
through the loop body (the `omc_exif_read_u32` call inside will fail when the
offset exceeds `raw_size`, returning 0 and aborting).  No issue in practice.

---

#### I-1 [INFO] Overflow-checked multiply used consistently  
**Lines**: 216-226 (`omc_exif_mul_u64`)  
All `count × type_size` products go through this helper.  Correct.

#### I-2 [INFO] All read helpers validate bounds  
**Lines**: 116-213  
`omc_exif_read_u16`, `_u32`, `_u64` all check `offset > size || N > (size - offset)`.
Every call site checks the return value.  Correct.

---

### 2. `omc_scan.c` — Container Scanner (≈ 4 200 lines)

#### L-2 [LOW] TIFF scanning delegates IFD traversal  
**Lines**: ≈ 877-923  
The scanner emits a single block covering the whole TIFF region; it does **not**
follow IFD next-pointers.  This is by design — IFD traversal is the EXIF
decoder's job.  No cycle risk here.

#### I-3 [INFO] All format scanners properly bounded  
- **JPEG** marker loop: advances by `seg_total_size ≥ 2` per marker; bounded by
  file size.  Zero-length markers rejected (line 636-639).
- **PNG** chunk loop: `chunk_size = 12 + data_size ≥ 12`; loop terminates when
  `offset + 12 > size` or at IEND.
- **GIF** sub-blocks: `omc_scan_skip_gif_sub_blocks` (line 268-293) advances by
  at least 1 per iteration; exits on block terminator (size 0) or OOB.
- **BMFF / JP2 / JXL** boxes: `omc_scan_parse_bmff_box` validates
  `off + box_size ≤ end ≤ size`; size-0 box uses remaining space; loop always
  advances by `box.size`.
- **RIFF / WebP**: chunk size validated, 8-byte minimum per chunk.

No infinite-loop or buffer-overrun paths found in any scanner.

---

### 3. `omc_pay.c` — Payload Assembly (596 lines)

#### I-4 [INFO] Copy range fully validated  
**Lines**: 46-89  
`omc_pay_copy_range` checks `data_offset + data_size ≤ file_size` before any
`memcpy`.  Truncation is handled gracefully.

#### L-3 [LOW] Zlib/Brotli decompression bombs limited  
**Lines**: 146-264, 267-361  
Both `omc_pay_inflate_zlib_range` and `omc_pay_brotli_range` enforce
`max_output_bytes` (default 64 MiB).  A decompression bomb produces at most that
much output before `OMC_PAY_LIMIT` is returned.  The scratch buffer (256 bytes)
is used for discard when the output buffer is full, so no unbounded allocation.

---

### 4. `omc_xmp.c` — XMP Parser (1 154 lines)

#### H-2 [HIGH] Integer overflow in allocation-size calculation  
**Lines**: 1092-1100  
```c
ctx->frame_cap = ctx->opts.limits.max_depth;          /* omc_u32 */
ns_cap         = ctx->frame_cap * 8U + 32U;           /* omc_u32 */
path_cap       = ctx->opts.limits.max_path_bytes + 32U;/* omc_u32 */

ctx->frames   = malloc((omc_size)ctx->frame_cap * sizeof(*ctx->frames));
ctx->ns_decls = malloc((omc_size)ns_cap          * sizeof(*ctx->ns_decls));
ctx->path_buf = malloc((omc_size)path_cap);
```

If the **caller** sets `max_depth = 0x20000001`, then:
- `ns_cap = 0x20000001 * 8 + 32` overflows `omc_u32` to **40**.
- `malloc(40 * sizeof(omc_xmp_ns_decl))` allocates ≈ 960 bytes.
- The code later indexes up to `ns_cap` (40) elements — fits the allocation.
- BUT `frame_cap = 0x20000001`, and `malloc((omc_size)0x20000001 * 52)`:
  on **32-bit** `omc_size`, this overflows to a small value, and
  `ctx->frames[frame_count]` writes out-of-bounds once `frame_count ≥ 1`.

Similarly `path_cap = 0xFFFFFFFF + 32` overflows to 31.

**Practical impact**: Default `max_depth = 128` is safe. Exploitable only when the
caller passes extreme limit values. Nevertheless, the library should validate:
```c
if (ctx->frame_cap > (omc_u32)(~(omc_size)0) / sizeof(*ctx->frames)) { … NOMEM … }
```

**Recommendation**: Add overflow guards on all three `malloc` size computations.

---

#### M-3 [MEDIUM] `omc_xmp_find_until` cursor is `omc_u32`  
**Lines**: 270-292  
```c
omc_u32 limit;
…
limit = (omc_u32)(ctx->size - tail_len);
```
If `ctx->size` exceeds `UINT32_MAX` (possible only on 64-bit with
`max_input_bytes` raised above 4 GiB), `limit` silently truncates and the search
terminates early — a **false negative**, not a buffer over-read.

With the default `max_input_bytes = 64 MiB`, this is unreachable.

**Recommendation**: Either assert `ctx->size <= UINT32_MAX` at init, or widen `pos`
/ `limit` to `omc_size`.

---

#### M-4 [MEDIUM] `malloc` in `omc_xmp_dec` violates SKILL.md hot-path contract  
**Lines**: 1096-1100  
The SKILL.md mandates "No allocation in hot paths" and "No hidden allocations."
`omc_xmp_dec` allocates three heap buffers on every call.  For a streaming or
batch workflow this is a per-file allocation.

**Recommendation**: Accept caller-provided workspace (like the EXIF decoder does
for IFD refs), or document this as an explicit initialization-phase allocation.

---

#### L-4 [LOW] DOCTYPE / ENTITY rejection is correct  
**Lines**: 979-982  
The parser scans for `<!DOCTYPE` and `<!ENTITY` and returns `MALFORMED`.  No
entity expansion occurs.  XXE / billion-laughs attacks are properly blocked.

---

### 5. `omc_bmff.c` — ISO-BMFF Parser (3 119 lines)

#### I-5 [INFO] Box parsing is robust  
**Lines**: 244-290 (`omc_bmff_parse_box`)  
- Size 0 → remaining parent space.  Size 1 → 64-bit extended size.
- `box_size < header_size` → reject.
- `off + box_size > end` → reject.
- Both `off` and `end` are bounded by `(omc_u64)size` (line 256).
- Since `off ≤ end ≤ (omc_u64)size` and `box_size ≤ end − off`, the addition
  `off + box_size ≤ end` cannot overflow `omc_u64`.

No integer-overflow or infinite-loop issues found.

#### L-5 [LOW] Recursive box scan depth-limited  
**Lines**: 2991-3037 (`omc_bmff_scan_for_meta`)  
`depth > max_depth` (default 16) returns `BMFF_LIMIT`. Box counter `max_boxes`
(default 16 384) also limits total work.

---

### 6. `omc_jumbf.c` — JUMBF / C2PA / CBOR (10 583 lines)

CBOR depth is bounded by `max_cbor_depth` (default 64).  CBOR item count by
`max_cbor_items`.  Box depth by `max_box_depth`.  String lengths by
`max_cbor_text_bytes` / `max_cbor_bytes_bytes`.  No `malloc` calls found — all
storage goes through the arena.  No issues found.

---

### 7. `omc_irb.c` — Photoshop IRB Decoder (1 588 lines)

IRB resource loop (line 1434): advances by at least `4 (8BIM) + 2 (id) + 2
(name pad) + 4 (len) = 12` bytes per iteration.  `padded_len =
omc_irb_pad2(data_len)` with `omc_irb_pad2(0) = 0`, so `p = data_off +
padded_len` always advances past the header.  `max_resources`, `max_total_bytes`,
and `max_resource_len` are all enforced.

IPTC sub-decoding (line 1552-1567): passes `data_len` (already validated against
`irb_size`) to `omc_iptc_dec`.  Safe.

No issues found.

---

### 8. `omc_iptc.c` — IPTC-IIM Decoder (223 lines)

Main loop (line 114) advances `p = value_off + value_len` each iteration.
`value_off = p + 3 + header_len ≥ p + 5`, so `p` always increases.
Extended-length encoding (bit 15 set) correctly reads 1-4 extra bytes.
All limits enforced.  No issues found.

---

### 9. `omc_icc.c` — ICC Profile Decoder (535 lines)

Header fields (offsets 0–100) validated against `icc_size ≥ 132`.  Tag table:
`128 + 4 + tag_count × 12 ≤ icc_size` checked (line 463).  Each tag's
`offset + size ≤ icc_size` checked (line 503).  `max_tags`, `max_tag_bytes`,
`max_total_tag_bytes` enforced.  No issues found.

---

### 10. `omc_ciff.c` — Canon CIFF Parser (1 562 lines)

#### H-3 [HIGH] `localtime()` is not thread-safe  
**Line**: 375  
```c
tm_parts = localtime(&raw_time);
```
`localtime` returns a pointer to static data.  Concurrent calls from multiple
threads corrupt the result.

**Recommendation**: Use `gmtime` (also not thread-safe in C89, but avoids
timezone lock contention) or accept a caller-provided buffer.  Document the
thread-safety constraint.

---

#### M-5 [MEDIUM] No CIFF directory-offset cycle detection  
**Lines**: 1460-1476  
`omc_ciff_decode_directory` recurses into sub-directories.  Depth is bounded at
32 (line 1368).  A circular directory chain (A → B → A) would recurse to depth 32
and then stop.  Each level carries a 32-byte stack buffer plus arguments — total
≈ 4 KiB stack for 32 levels.  Bounded but wasteful.

**Recommendation**: Track parent offsets in a small stack-allocated array and
reject revisits.

---

### 11. `omc_exr.c` — OpenEXR Metadata Parser (≈ 1 000 lines)

Null-terminated string scanning (lines 150-165) is bounded by `*io_offset <
(omc_u64)size`, with `max_bytes` limit on string length.  Attribute sizes are u32,
validated against file size (line 811).  Multi-part header parsing capped by
`max_parts`.  `max_total_attribute_bytes` enforced with wrap-around check (line
816).

All `(omc_size)offset` casts are safe because the offset is always validated
against `st->size` (which is `omc_size`) before the cast.

No issues found.

---

### 12. `omc_exif_name.c` — EXIF Tag Naming (864 lines)

Lookup table `k_names[]` is `static const` with 109 entries.  Linear scan
O(109) per lookup — negligible.  IFD name truncation to 64 bytes (line 464)
prevents buffer overflow; truncated names may mismatch, but this is a display
issue not a safety issue.  All switch statements have `default` cases.

No issues found.

---

### 13. `omc_read.c` — Read Orchestrator (1 822 lines)

Block iteration (line 1522): `i < res.scan.written` where `written ≤ block_cap`
(provided by caller).  Block pointer `out_blocks[i]` is within bounds.

All decoder calls receive `block_view.data` / `block_view.size` from
`omc_read_block_view`, which in turn calls `omc_pay_ext` — the payload assembly
layer validates all offsets.  No double-decode.

`omc_read_opts_init` initializes all sub-decoder options with safe defaults.
If caller passes `NULL` opts, defaults are used.

No issues found.

---

## Cross-Cutting Observations

### Integer overflow discipline

Every file uses the same defensive pattern for offset/size validation:
```c
if (offset > (omc_u64)size || needed > ((omc_u64)size - offset)) { … fail … }
```
This avoids `offset + needed > size` (which could overflow).  The pattern is
applied consistently across all 13 files — a strong positive signal.

The EXIF decoder additionally provides `omc_exif_mul_u64` for overflow-checked
multiplication.

### `omc_u64` → `omc_size` casts

Many functions accept `omc_size` (= `size_t`) buffer sizes but do arithmetic in
`omc_u64`.  Casts to `omc_size` occur when indexing arrays (e.g.,
`bytes[(omc_size)offset]`).  In every case reviewed, the u64 value has been
validated to be `≤ (omc_u64)size` where `size` is the `omc_size` buffer length.
Since the cast cannot truncate a value that fits in the original `omc_size`, these
casts are **safe**.

### Loop termination guarantees

| Module | Loop | Advance per iteration | Bounded by |
|--------|------|-----------------------|------------|
| JPEG scan | marker loop | ≥ 2 bytes | file size |
| PNG scan | chunk loop | ≥ 12 bytes | file size or IEND |
| GIF scan | sub-block loop | ≥ 1 byte | file size or terminator |
| BMFF scan/decode | box loop | ≥ 8 bytes (box.size ≥ header) | file size + depth + box count |
| EXIF decode | task queue | 1 task popped per iter | 256 tasks × max_ifds |
| IPTC decode | dataset loop | ≥ 5 bytes | iptc_size + max_datasets |
| IRB decode | resource loop | ≥ 12 bytes | irb_size + max_resources |
| ICC decode | tag loop | fixed (tag_count) | tag_count + max_tags |
| XMP parse | character loop | ≥ 1 byte | ctx->size + max_properties |
| CIFF decode | entry loop | fixed (entry_count) | entry_count × 10 + depth 32 |
| EXR decode | attribute loop | ≥ 1 byte (null-term) | exr_size + max_attributes |
| JUMBF/CBOR | recursive boxes + CBOR items | ≥ 1 byte | depth + item count limits |
| Payload | part sort | fixed (part_count) | max_parts (16 384) |

All loops are provably bounded.

### Status code consistency

Every decoder uses a per-module enum (`OMC_*_OK/MALFORMED/LIMIT/NOMEM/…`) and
a merge function that preserves the highest-severity status.  The `omc_read.c`
orchestrator merges sub-results via `omc_read_merge_*` helpers.  No silent
error swallowing observed.

---

## Consolidated Findings Table

| ID | Severity | File | Lines | Issue |
|----|----------|------|-------|-------|
| H-1 | **HIGH** | omc_exif.c | 13874-13900 | No IFD-offset cycle detection (bounded DoS via max_ifds) |
| H-2 | **HIGH** | omc_xmp.c | 1092-1100 | Integer overflow in ns_cap / frame_cap / path_cap `malloc` sizes when caller sets extreme limits |
| H-3 | **HIGH** | omc_ciff.c | 375 | `localtime()` not thread-safe |
| M-1 | **MEDIUM** | omc_exif.c | 2463-2503 | MakerNote child contexts lack explicit recursion depth counter |
| M-2 | **MEDIUM** | omc_pay.c | 521-539 | Bubble-sort of part indices is O(n²), n up to 16 384 |
| M-3 | **MEDIUM** | omc_xmp.c | 270-292 | `find_until` limit is `omc_u32`; truncates silently if input > 4 GiB |
| M-4 | **MEDIUM** | omc_xmp.c | 1096-1100 | `malloc` in every `omc_xmp_dec` call (SKILL.md: no hidden alloc in hot paths) |
| M-5 | **MEDIUM** | omc_ciff.c | 1460-1476 | No directory-offset cycle detection (bounded by depth 32) |
| M-6 | **MEDIUM** | omc_exif.c | 13837-13862 | SubIFD count loop feeds directly from file data; bounded by task queue |
| L-1 | **LOW** | omc_exif.c | 13837 | SubIFD count from file data (mitigated by task cap) |
| L-2 | **LOW** | omc_scan.c | 877-923 | TIFF scanner does not follow IFDs (by design) |
| L-3 | **LOW** | omc_pay.c | 146-264 | Decompression bombs capped at 64 MiB |
| L-4 | **LOW** | omc_xmp.c | 979-982 | DOCTYPE/ENTITY correctly rejected |
| L-5 | **LOW** | omc_bmff.c | 2991-3037 | Recursive box scan bounded by depth + count |
| I-1 | **INFO** | omc_exif.c | 216-226 | Overflow-checked multiply used consistently |
| I-2 | **INFO** | omc_exif.c | 116-213 | All read helpers validate bounds |
| I-3 | **INFO** | omc_scan.c | all | All scanner loops provably bounded |
| I-4 | **INFO** | omc_pay.c | 46-89 | Copy range fully validated |
| I-5 | **INFO** | omc_bmff.c | 244-290 | Box parsing robust against size=0, 64-bit size, overflow |

---

## Recommended Actions

### Must-do (HIGH)

1. **H-1**: Add visited-offset tracking to the EXIF task loop. A fixed
   `omc_u64 visited[128]` array (1 KiB) plus linear scan is sufficient
   given `max_ifds ≤ 128`.

2. **H-2**: Guard all three `malloc` calls in `omc_xmp_ctx_init` with
   overflow checks:
   ```c
   if (ctx->frame_cap > (~(omc_size)0) / sizeof(*ctx->frames)) { … NOMEM … }
   ```

3. **H-3**: Replace `localtime()` with `gmtime()` or a pure arithmetic
   formatter, and document thread-safety requirements.

### Should-do (MEDIUM)

4. **M-1**: Pass a `depth` counter through `omc_exif_decode_tiff_stream` and
   child helpers; fail at depth > 4.

5. **M-2**: Replace bubble-sort with insertion sort or document the O(n²)
   bound given max_parts = 16 384.

6. **M-3**: Assert `ctx->size ≤ UINT32_MAX` at XMP init, or widen the cursor
   type.

7. **M-4**: Consider accepting a caller workspace for XMP frame/ns/path
   arrays (matching the SKILL.md contract).

8. **M-5**: Track parent directory offsets in CIFF recursive descent.

### Nice-to-have (LOW)

9. Document the bounded-DoS characteristics of EXIF cycle + MakerNote
   recursion in the public header.

---

## Conclusion

The read layer demonstrates strong defensive coding: consistent bounds
validation, explicit limits, no raw pointer arithmetic without prior checks,
and careful status propagation. The three HIGH findings are all bounded DoS
or hardening items — none allows memory corruption under default limits.
The codebase is well-suited for processing untrusted input with the recommended
fixes applied.
