# EXIF creation-date lifecycle against C++ 0.8.19

C package `0.16.0`, local revision `0.16.0.016`. Validated 2026-10-17.

## Reference and scope

The reference remains C++ `e8240c3aed7cc858632e4a9cb5605d56b3922f10`
(0.8.19), using the clean detached checkout
`/tmp/openmeta-cpp-reverse-dates-0819` and its previously validated Clang 20
libc++ static library in `/tmp/openmeta-cpp-reverse-dates-0819-build`.
The reference library hash is recorded in the private receipt. The C baseline
is `f398199ae509b7b539410692fd56e67ad6c57de8`, rebuilt from an archive into
`/tmp/openmeta-c-exif-dates-0819-baseline`. Live C++ 0.8.20 carrier-removal work
was uncommitted at batch start and landed during final validation as
`0f492554fd57ee65381e6092debd88efc88c1285`. It is queued separately and was
not built or modified here.

This increment covers original and digitized EXIF creation groups in
`omc_translate_xmp()`: date/time (`9003`/`9004`), offset (`9011`/`9012`) and
subsecond (`9291`/`9292`) in `exififd`. Existing public masks, structures,
defaults, status vocabulary and distinct source/output ownership remain.
Technical ModifyDate and IPTC retain their previous lifecycle rules.

## Behavior and implementation

Accepted clean native values gain Dirty while preserving their complete Text
or Bytes representation, padding, key and provenance. Matching ignores trailing
NUL bytes. Missing companions retain deletion intent: an existing Dirty|Deleted
marker is reused, the first clean deleted marker is promoted, or an empty ASCII
Text marker is added with source-derived provenance and saturated field order.

Preserve leaves groups containing active native entries untouched. Fail rejects
non-exact active groups, but accepts exact clean values for promotion. Replace
updates the first active value and removes duplicates or stale companions.
An absent group can acquire deletion markers under every policy. Repeats under
Fail/Replace are unchanged. Preserve reapplies a marker-only deleted group with
zero entry mutations and one translated group, matching the pinned reference.

Promotions and removals consume operations. New entries, including deletion
markers, consume both additions and operations. C retains its existing private
edit queue and checks limits before publication; this is not a total memory cap
or an allocation-free preflight. The sole edit commit constructs a candidate
store and publishes only on success. Failed calls preserve output and report
zero mutation counters. No new public API, dependency or second store-staging
path is introduced.

## Acceptance

The fresh baseline passes 70/78 Release targets, with the same eight failures
as C 0.15.0. Its diagnostics match the previous receipt: 135 CTest records and
176 full-inventory parity reports (351 including field mismatches).

| Final check | Result |
| --- | --- |
| Clang 20 Release | 73/81; eight historical inventory failures remain |
| ASan/UBSan with leak detection, direct C | 58/58 |
| Dependencies disabled, shared library, direct C | 58/58 |
| C/C++ lifecycle comparisons | 146 paired calls, including repeats and failures |
| Carrier lifecycle | 50 JPEG/TIFF/BigTIFF cases |
| Previous partial-date regression coverage | 88 paired cases and 12 forward/reverse round trips remain passing |

The eight retained failures are `omc_test_parity`,
`omc_test_remaining_source_parity`, `omc_test_read_inventory`,
`omc_test_callback_inventory`, `omc_test_bmff_read_parity`,
`omc_test_bmff_callback_parity`, `omc_test_box_source_parity`, and
`omc_test_tiff_source_parity`. Final diagnostic multisets match the fresh
baseline: 135 CTest records, 176 full-inventory reports and 351 detailed records.
Complete `--all` output also matches after normalizing source paths and generated
temporary timestamps.

The paired matrix compares statuses, counters, failure mapping/source where
contracts agree, exact key/value bytes, flags, value representation, provenance
and saturated order. It exercises both mappings, all policies, raw and padded
native values, clean/dirty markers, repeats, duplicates, partial rejection,
entry/operation limits and late selected-source failure. Limit diagnostics keep
the existing C status/ID conventions rather than asserting C++ layouts or caps.

The carrier matrix has 50 cases: both mappings, five lifecycle modes, and JPEG
plus classic TIFF/BigTIFF in both byte orders. It creates and verifies stale
target groups before translation, then checks promoted full timestamps,
missing companions, whole-group deletion, clean-marker promotion and offset-only
timestamps. TIFF retains the unselected date family, Make, ModifyDate and opaque
MakerNote/pixel sentinels from the destination. JPEG replaces the complete EXIF
packet, so its retained other date family is explicitly supplied in the source.
These cases do not establish JPEG destination-only merging or caption/XMP
preservation.

The corrected carrier fixture fails against the unchanged C 0.15.0 library at
the clean-promotion assertion. An earlier fixture mistakenly required JPEG to
collect TIFF-native intent; that initial failure is preserved separately and is
not counted as a library regression. An initial paired fixture also assumed
identical C/C++ flag bit values; it was corrected to map flags and contextual
name kinds by meaning. No production change was needed for these fixture fixes.

## Open gates and next work

This does not establish complete C++ creation-date contract 3 parity. Generic C
still reconciles in mapping order, validates retained references during commit,
and requires distinct source/output stores. Mixed simultaneous failures may have
different diagnostic precedence. ModifyDate lifecycle, shared opt-in destination
validation, and remaining UP4 capture/sensitivity/Flash/lens families remain.
Explicit last-IPTC-carrier removal is a separate transfer contract. Pin newly
committed upstream 0.8.20 for that next bounded increment, then resume shared
destination validation and the remaining UP4 families.

No full upstream C++ suite, native Windows, embedded device/resource budget,
private corpus, installed-consumer acceptance or performance claim follows from
these WSL checks. The broad inventory residuals remain open.

## Reproduction

From the workspace wrapper, with the pinned C++ build above:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-exif-dates-0819-release -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DOMC_WARNINGS_AS_ERRORS=ON \
  -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-reverse-dates-0819-build \
  -DOMC_OPENMETA_SOURCE_DIR=/tmp/openmeta-cpp-reverse-dates-0819
cmake --build /tmp/openmeta-c-exif-dates-0819-release -j 2
ctest --test-dir /tmp/openmeta-c-exif-dates-0819-release --output-on-failure
/tmp/openmeta-c-exif-dates-0819-release/tests/omc_test_parity --all

cmake -S OpenMeta-c -B /tmp/openmeta-c-exif-dates-0819-sanitizer -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Debug -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/openmeta-c-exif-dates-0819-sanitizer -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/openmeta-c-exif-dates-0819-sanitizer --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-exif-dates-0819-disabled-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Release \
  -DOMC_WARNINGS_AS_ERRORS=ON -DOMC_BUILD_STATIC=OFF -DOMC_BUILD_SHARED=ON \
  -DOMC_USE_ZLIB=OFF -DOMC_USE_BROTLI=OFF
cmake --build /tmp/openmeta-c-exif-dates-0819-disabled-shared -j 2
ctest --test-dir /tmp/openmeta-c-exif-dates-0819-disabled-shared --output-on-failure
```

Build logs, source/reference hashes, review, counterfactual and residual
comparisons are in workspace `_handoff/c_port_exif_dates_0819/`, outside the
public C repository.
