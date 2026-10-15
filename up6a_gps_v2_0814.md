# Native selected edits and GPS v2 against C++ 0.8.14

Accepted: 2026-10-15. C package: `0.12.0`, local revision `0.12.0.012`.

## Reference and scope

The immutable reference is OpenMeta C++ `0.8.14`,
`fc2052b9eac0c7819c09a067e0c698fe38a2a155`. The C starting revision is
`630d6a249ca75cfa24ec2afaf5ff4910b3f6c1fb` (`0.11.0.011`).
The paused diff was reviewed and rebuilt before acceptance. This batch covers
the UP6a direct native-edit foundation and all five UP3 GPS v2 APIs. It does
not complete every UP6 consumer or establish stage-wide parity.

The reference was built from `/tmp/openmeta-cpp-up6a-0814` into
`/tmp/openmeta-cpp-up6a-0814-build` with Clang 20, libc++, Release dependencies
at `/mnt/e/UBc/Release`, RapidFuzz off and Adobe DNG SDK off. C++ reference
tests were disabled in this build; this batch runs C and paired C/C++ tests.
The prior C++ full-suite receipt remains historical evidence under the same
pin. No private corpus, native Windows or embedded-device qualification was
performed for this batch.

## Implemented behavior

- Owned native edit intent contains sorted bounded removal lists for seven
  IFD0, 85 ExifIFD and 31 GPS value tags. Only Dirty|Deleted entries select
  removal; live source owners suppress tombstones. GPSVersionID and ColorSpace
  are not deletion selectors. Whole-directory clear is explicit and conflicts
  with same-directory merge, removal or live payload.
- Prepare and compile copy intent inline. The new native `include_exif` option
  defaults to one and is independent of EXIF-to-XMP projection. Host ColorSpace
  requests Exif merge; source ColorSpace is filtered. Exif merge alone remains
  valid for fresh metadata output.
- Selected TIFF edits support classic TIFF and BigTIFF in LE and BE. Supplied
  live IFD0/Exif values apply during merge; omitted destination fields survive.
  GPS merges select supported Dirty values and require one supplied BYTE/count-4
  GPSVersionID matching any target version. Removal-only edits retain it until
  the last non-version value disappears. Unchanged child IFD offsets, opaque
  MakerNote bytes and image ranges remain intact.
- Existing/template DNG preserves clean source DNGVersion as context rather
  than replacing the target. Eligible Dirty replacements remain possible in
  CompatibleFile mode. Fresh scaffold version injection is separate.
- Transfer execution publishes both output arenas only after success, including
  when input bytes alias the previous edited output. Generic TIFF/DNG
  payload/package emission rejects removals, GPS merge and explicit clears
  before output callbacks. Executed-output packages serialize, restore,
  materialize and replay the already-applied result.
- `OMC_EDIT_OP_MARK_DIRTY` preserves representation and provenance. All five
  GPS translators promote accepted clean native values, preserve active owners
  under PreserveExisting, retain typed absent-key deletion intent, reuse dirty
  intent, and account for promotions/additions/duplicate removal before commit.
  The public GPS lifecycle contract constant is 2. Existing mapping defaults
  and the shared option initializer are retained.
- Primary coordinate equivalence now decodes rational arrays using their
  declared byte order. A paired big-endian case retains unreduced components
  and provenance across promotion and repeated calls. The new direct test
  fails against the pre-correction library, confirming that it detects the
  previous host-byte-order assumption.
- Explicit type-129 UTF-8 Text and a retained terminal NUL serialize without
  adding a second terminator. Invalid UTF-8 and invalid type-129 value shapes
  are rejected even when detached validation is disabled. The baseline schema
  accepts type 129 for its known UTF-8 text fields and keeps date fields ASCII.

The transfer prepare/bundle/executor layouts changed. Rebuild consumers and
initialize options through their public initializers. No C++ runtime is added
to the C library. The core still builds as C90 with extensions disabled.

## Acceptance evidence

All three C configurations use Clang 20 and warnings as errors for the library.
Assertions remain active in the tests.

| Configuration / inventory | Result |
| --- | --- |
| Release static, zlib/Brotli enabled, C++ parity enabled | 57/65 targets pass: all 48 direct targets plus nine differential targets; eight historical inventory targets still fail |
| Debug ASan/UBSan, leak detection, zlib/Brotli enabled | 48/48 direct targets pass |
| Release shared, zlib/Brotli disabled | 48/48 direct targets pass |
| Broad CTest diagnostics | 135 mismatch/assertion reports; exact diagnostic multiset unchanged from the prior accepted baseline |
| Separate `omc_test_parity --all` inventory | 176 mismatch reports; exact diagnostic multiset unchanged |

The new native-edit parity target contains **24 cases**: four TIFF layouts
times selected removal, active GPS merge with clean context, version mismatch,
host ColorSpace merge, clean supplied values during merge, and last-value GPS
cleanup. It compares normalized native fields with C++, checks negative
statuses/rollback, and verifies retained opaque/media bytes.

The GPS carrier matrix rereads **all 32 standard GPS IDs in 13 cases**: JPEG,
four TIFF layouts, four existing-DNG layouts and four template-DNG layouts.
Translation parity covers the five APIs' exact-clean promotion, PreserveExisting,
repeated calls, absent deletion, dirty-intent reuse, clean-tombstone promotion,
representation/provenance retention, limits and rollback. Direct tests also
cover clear/live/removal conflicts, duplicate GPS versions, type-129 failures,
malformed directory counts, input/output aliasing and independent output controls.
Executed-output packages are checked after serialization and destruction of the
original package storage, then through deserialization, materialization and
callback replay.

The unchanged failing inventory targets are `omc_test_parity`,
`omc_test_remaining_source_parity`, `omc_test_read_inventory`,
`omc_test_callback_inventory`, `omc_test_bmff_read_parity`,
`omc_test_bmff_callback_parity`, `omc_test_box_source_parity` and
`omc_test_tiff_source_parity`. Expected values were not changed to accept these
differences. See the earlier [baseline receipt](up0_up1_0814.md).

## Boundaries and next work

Native collection is limited to 200,000 source entries. The TIFF overlay
rejects directory counts above 65,535 and caps newly appended data at 64 MiB
before append/allocation. It still materializes the existing file; this is not
an embedded-memory or streaming qualification. Empty edits preserve C's
successful-copy policy; there is no `require_updates` option.

Full replacement/page/SubIFD semantics, prepared snapshots, remaining direct
stream/package consumers, complete thirteen-field text semantics, shared
destination-profile validation and non-GPS lifecycle families remain open.
Canonical type-129 support is a wire-format gate, not complete EXIF text/profile
validation. Source filtering is not destination redaction. Synthetic opaque
payload retention does not establish arbitrary MakerNote relocation safety.

Next authoring work is UP4 dates/basic capture/sensitivity/Flash/lens lifecycle,
with shared destination validation and one transfer acceptance path per family.
RD9/RD10 reader expansion can proceed independently. Later C++ commits belong
to a new delta queue, not this receipt's acceptance denominator.

## Reproduction and local artifacts

From the workspace wrapper, with the matching reference worktree/build above:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-up6a-20261015-release -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DOMC_WARNINGS_AS_ERRORS=ON \
  -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-up6a-0814-build \
  -DOMC_OPENMETA_SOURCE_DIR=/tmp/openmeta-cpp-up6a-0814
cmake --build /tmp/openmeta-c-up6a-20261015-release -j 8
ctest --test-dir /tmp/openmeta-c-up6a-20261015-release --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-up6a-20261015-sanitizer -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Debug -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/openmeta-c-up6a-20261015-sanitizer -j 8
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/openmeta-c-up6a-20261015-sanitizer --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-up6a-20261015-disabled-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Release \
  -DOMC_WARNINGS_AS_ERRORS=ON -DOMC_BUILD_STATIC=OFF -DOMC_BUILD_SHARED=ON \
  -DOMC_USE_ZLIB=OFF -DOMC_USE_BROTLI=OFF
cmake --build /tmp/openmeta-c-up6a-20261015-disabled-shared -j 8
ctest --test-dir /tmp/openmeta-c-up6a-20261015-disabled-shared --output-on-failure
```

The full Release schedule intentionally reports the eight residual targets.
For the new focused gate, use
`-R '^omc_test_(native_edit|native_edit_parity|gps_translation|gps_groups|gps_parity|edit|exif_carriers|exif_tiff_serialize|store_validate)$'`.

Full logs stay outside the public repository in workspace
`_handoff/c_port_up6a_gps_v2/`. Final evidence is `final_validation.json`,
`build_release_final.log`, `ctest_release_final.log`, `build_sanitizer.log`,
`ctest_sanitizer.log`, `build_disabled_shared.log`, `ctest_disabled_shared.log`,
`parity_all.log` and `residual_comparison.json`. Worker checkpoints and the
independent transfer review are retained there for audit.
