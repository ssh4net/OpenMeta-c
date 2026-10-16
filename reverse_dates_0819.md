# Reverse partial creation dates against C++ 0.8.19

C package `0.15.0`, local revision `0.15.0.015`. Validated 2026-10-17.

## Reference and scope

The immutable reference is C++ `e8240c3aed7cc858632e4a9cb5605d56b3922f10`
(0.8.19). The C starting revision is
`b30aec6337639d3d4d5b6f993314d6875565eac2`. The detached source is
`/tmp/openmeta-cpp-reverse-dates-0819`; its static Release build is
`/tmp/openmeta-cpp-reverse-dates-0819-build`, using Clang 20 and libc++ from
`/mnt/e/UBc/Release`. C++ tests, tools, Python, RapidFuzz and Adobe DNG SDK are
disabled. The live C++ checkout is not built or edited.

This increment accepts exact positive `YYYY` and valid `YYYY-MM` creation
values through the existing C `omc_translate_xmp()` API. Photoshop DateCreated
and XMP CreateDate project to IPTC 2:55 and 2:62, respectively, with literal
unknown month/day zeroes and no time dataset. Selected EXIF creation mappings
reject valid partial precision atomically. Technical ModifyDate parsing stays
unchanged. All failed generic reverse transactions now clear their four
provisional mutation counters. No public type, mapping mask or default changes.

See [the authoring contract](authoring.md#explicit-reverse-translation) for
selectors, ownership and failure behavior. This is a bounded date increment;
it does not declare full C++ creation-date contract 3 parity.

## Implementation and validation

The creation parser records internal precision and fills unknown components
with zeroes. The existing reconciler handles pair conflicts and stale active
time removal. The existing edit commit builds a private candidate, checks
reserve/clone/append results and publishes only after success. No second
staging path or new allocation was added. Direct tests exercise late parse,
operation/add/text limits and retained-reference commit failure with unchanged
source/output snapshots and zero committed mutation counts.

Fresh C baseline against 0.8.19: 67/75 Release targets, with the same eight
failures and diagnostics as the preceding 0.8.18 acceptance.

| Final check | Result |
| --- | --- |
| Clang 20 Release | 70/78; eight historical inventory targets still fail |
| ASan/UBSan with leak detection, direct C | 56/56 |
| Dependencies disabled, shared library, direct C | 56/56 |
| Reverse date C/C++ comparison | 88 paired cases pass |
| Native IPTC to stored XMP, JPEG reread, reverse canonical IIM | 12 cases pass, six for each mapping |
| JPEG/TIFF target replacement | 10 cases pass, including two last-carrier omission controls |
| Counterfactual | New partial round-trip assertion fails against unchanged C 0.14.0 and passes against this implementation |

Paired cases cover exact year/month/full dates, leap dates, absent and signed
offsets, malformed forms, fractional rejection, all conflict policies,
DirtyOnly/All, dirty deletions, duplicate sources, both selected pairs, limits,
late failure and partial EXIF rejection. They compare status, mutation counts,
failure mapping/source where the API contracts agree, and exact canonical IIM.
Empty successful output is checked as NoIptcData with zero output counts.
C failure checks retain the output structure, entry bytes and sentinel arena;
the C++ sentinel verifies entry count and value bytes, not every store internal.

Carrier tests first write and reread stale native date/time pairs. They then
replace from partial/full date-only sources that contain no native date/time,
verify time removal, and retain an unrelated caption and EXIF Make. Dirty pair
deletion also removes dates when a caption keeps the replacement nonempty.
When no active IPTC remains, the payload is omitted and the old carrier remains
readable. That last-carrier behavior is an explicit open gate, not removal
support.

The eight retained failures are `omc_test_parity`,
`omc_test_remaining_source_parity`, `omc_test_read_inventory`,
`omc_test_callback_inventory`, `omc_test_bmff_read_parity`,
`omc_test_bmff_callback_parity`, `omc_test_box_source_parity`, and
`omc_test_tiff_source_parity`. CTest retains 135 primary diagnostic records;
`--all` retains 176 parity reports, or 351 records including field mismatches.
Both diagnostic multisets match the fresh baseline. Full `--all` output also
matches after normalizing source paths and generated temporary timestamps.

## Remaining contracts

- EXIF creation values still need clean-value Dirty promotion and absent
  companion deletion intent. GPS already has its separate accepted lifecycle.
- C reconciles mappings in mask order; C++ parses all selected creation sources
  first. Mixed simultaneous errors can therefore choose different first errors.
- C duplicate-source diagnostics identify the duplicate; C++ identifies the
  first source. Generic C result/limit vocabulary stays intact, including no
  `source_properties` result counter and an invalid source ID for a source-count
  limit. The paired fixture explicitly accounts for these existing differences.
- Source/output must remain distinct for this generic C API. Commit validates
  retained references, including unrelated tombstones; malformed references can
  fail publication. Preparation still allocates and copies input state, and text
  limits are not a total memory cap.
- Explicit last-IPTC-carrier removal is absent from pinned C++ 0.8.19 too.
  During final validation the live checkout had uncommitted 0.8.20 prepared
  transfer changes introducing `remove_existing_iptc`; review them after commit.

Next, close EXIF creation lifecycle, then continue shared destination-profile
validation and UP4 basic capture/sensitivity/Flash/lens. Preserve the separate
explicit carrier-removal gate. No full upstream C++ test suite, native Windows,
embedded memory/device, private corpus or installed-consumer acceptance is
claimed by these WSL checks.

## Reproduction

From the workspace wrapper, with the pinned C++ static build above:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-reverse-dates-0819-release -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DOMC_WARNINGS_AS_ERRORS=ON \
  -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-reverse-dates-0819-build \
  -DOMC_OPENMETA_SOURCE_DIR=/tmp/openmeta-cpp-reverse-dates-0819
cmake --build /tmp/openmeta-c-reverse-dates-0819-release -j 2
ctest --test-dir /tmp/openmeta-c-reverse-dates-0819-release --output-on-failure
/tmp/openmeta-c-reverse-dates-0819-release/tests/omc_test_parity --all

cmake -S OpenMeta-c -B /tmp/openmeta-c-reverse-dates-0819-sanitizer -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Debug -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/openmeta-c-reverse-dates-0819-sanitizer -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/openmeta-c-reverse-dates-0819-sanitizer --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-reverse-dates-0819-disabled-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Release \
  -DOMC_WARNINGS_AS_ERRORS=ON -DOMC_BUILD_STATIC=OFF -DOMC_BUILD_SHARED=ON \
  -DOMC_USE_ZLIB=OFF -DOMC_USE_BROTLI=OFF
cmake --build /tmp/openmeta-c-reverse-dates-0819-disabled-shared -j 2
ctest --test-dir /tmp/openmeta-c-reverse-dates-0819-disabled-shared --output-on-failure
```

Private build logs, baseline comparisons, review and hashes are in workspace
`_handoff/c_port_reverse_dates_0819/`. Fixtures and the implementation contain
no private corpus data or external verification tools.
