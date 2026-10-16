# Native IPTC dates against C++ 0.8.18

C package `0.14.0`, local revision `0.14.0.014`. Validated 2026-10-17.

## Reference and scope

The immutable reference is C++ `7712c21b3dfb66ed6a2592263e087920a3b70b44`
(0.8.18). The C starting revision is
`fd7d0d5bd7733d295fd039cdb1459afb3d375fc9`. The detached reference source is
`/tmp/openmeta-cpp-iptc-dates-0818`, with static Release build
`/tmp/openmeta-cpp-iptc-dates-0818-build`, Clang 20 and libc++ from
`/mnt/e/UBc/Release`. C++ tests, tools, Python, RapidFuzz and Adobe DNG SDK
integration are disabled. The mutable main C++ checkout is not built or edited.

This batch covers explicit native IPTC date/time to materialized XMP contract 1:
2:55/60 to `photoshop:DateCreated` and 2:62/63 to `xmp:CreateDate`. It adds C API
types/functions without changing existing option/result layouts, reverse-date
policy, portable projection or persisted wire versions. Reverse partial-date
work landed during this batch as C++ 0.8.19, `e8240c3`; it is outside this pin
and heads the next-batch queue.

See [authoring.md](authoring.md#native-iptc-datetime-to-materialized-xmp-0140)
for date precision, conflict/deletion, bounds, ownership and error contracts.

## Design and validation

The fixed two-pair plan validates selected native entries, destination roots,
combined text budgets, dates and requested operations before publication.
The generic C edit clone validates every tombstone payload/origin; the reference
explicitly ignores those fields. This module therefore makes a fresh owned copy
of the complete used arena, entries and blocks, preserving offsets and retained
fields, then applies at most two planned XMP actions. No generic clone/edit
semantics or existing forward translator are changed.

Fresh C baseline against 0.8.18: 64/72 Release targets, with the same eight broad
inventory failures and normalized diagnostic multisets (135 CTest, 176 `--all`)
as the preceding 0.8.17 acceptance.

| Final check | Result |
| --- | --- |
| Clang 20 Release | 67/75; the same eight broad inventory targets fail |
| ASan/UBSan with leak detection, direct C | 54/54 |
| Dependencies disabled, shared library, direct C | 54/54 |
| Native date C/C++ comparison | 87 paired cases pass against the pinned static C++ library |
| Stored-XMP/JPEG/reverse-IIM lifecycle | 12 cases pass, six for each mapping |
| Existing IPTC/transfer focus plus the new targets | 14/14, extracted from the full Release schedule |

The eight retained failures are `omc_test_parity`,
`omc_test_remaining_source_parity`, `omc_test_read_inventory`,
`omc_test_callback_inventory`, `omc_test_bmff_read_parity`,
`omc_test_bmff_callback_parity`, `omc_test_box_source_parity`, and
`omc_test_tiff_source_parity`. CTest retains 135 primary diagnostic records;
`--all` retains 176 parity reports, or 351 records including field-level
mismatches. Their diagnostic multisets match the fresh baseline. The full
`--all` output also matches after normalizing source paths and test-generated
temporary timestamps. These residuals remain open; the scoped date batch does
not qualify the broader inventory.

The acceptance matrix includes direct date/calendar/offset/error cases,
All/DirtyOnly, known/unknown flags, tombstone payload/origin retention,
active-replacement precedence, destination roots, conflicts, aggregate budgets,
failure rollback and source/output aliasing. The nine direct test groups also
exercise exact text-budget boundaries. Paired tests compare status,
failure mapping/source, result counters, lexical values and provenance against
the pinned C++ library. Lifecycle tests serialize stored XMP, decode it, embed
and reread JPEG, check canonical reverse IIM for full dates, and confirm the
existing partial-date reverse rejection preserves output. Review found and
corrected two failure-order differences: source parsing must precede logical
destination UTF-8/XML validation, and each pair must resolve its conflict before
the next pair is parsed. Dedicated direct and paired cases exercise this order,
including later destination-budget failure taking precedence over bad text.

## Reproduction

From the workspace wrapper:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-dates-0818-release -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DOMC_WARNINGS_AS_ERRORS=ON \
  -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-iptc-dates-0818-build \
  -DOMC_OPENMETA_SOURCE_DIR=/tmp/openmeta-cpp-iptc-dates-0818
cmake --build /tmp/openmeta-c-iptc-dates-0818-release -j 2
ctest --test-dir /tmp/openmeta-c-iptc-dates-0818-release --output-on-failure
/tmp/openmeta-c-iptc-dates-0818-release/tests/omc_test_parity --all

cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-dates-0818-sanitizer -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Debug -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/openmeta-c-iptc-dates-0818-sanitizer -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/openmeta-c-iptc-dates-0818-sanitizer --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-dates-0818-disabled-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Release \
  -DOMC_WARNINGS_AS_ERRORS=ON -DOMC_BUILD_STATIC=OFF -DOMC_BUILD_SHARED=ON \
  -DOMC_USE_ZLIB=OFF -DOMC_USE_BROTLI=OFF
cmake --build /tmp/openmeta-c-iptc-dates-0818-disabled-shared -j 2
ctest --test-dir /tmp/openmeta-c-iptc-dates-0818-disabled-shared --output-on-failure
```

Logs and review evidence remain outside the public repository in workspace
`_handoff/c_port_iptc_dates_0818/`. No full upstream C++ suite, native Windows,
embedded device/memory budget, private corpus, or installed-consumer acceptance
is claimed. Preparation allocates and copies the input store; this is not an
allocation-free API or a general RDF/date normalization service.
