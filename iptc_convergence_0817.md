# IPTC convergence against C++ 0.8.17

C package: `0.13.0`, local revision `0.13.0.013`. The four scoped steps are
implemented and accepted under the WSL checks below.

## Reference and scope

This batch uses immutable C++ `0.8.17`,
`d281e041d940510d97c3e8539e2840a7fca6c679`, starting from C
`cb9d91fbe6f791c5e0576b49e1d685b73f24f2bb`. It covers the committed 0.8.15
canonical IPTC-IIM serializer, 0.8.16 JPEG Photoshop resource preservation and
0.8.17 twenty-group native IPTC to materialized XMP translator. Upstream's
date/time work was uncommitted at batch start and landed during this batch as
0.8.18, `7712c21b3dfb66ed6a2592263e087920a3b70b44`. It remains excluded from this
fixed reference. Older 0.8.14 acceptance receipts keep their original references.

The detached reference is `/tmp/openmeta-cpp-iptc-0817`, with Release build
`/tmp/openmeta-cpp-iptc-0817-build`. Clang 20 uses libc++ and dependencies at
`/mnt/e/UBc/Release`; RapidFuzz, Adobe DNG SDK, Python and tools are disabled.
C++ tests are disabled in that build. This batch runs C and paired C/C++ tests,
not a fresh full upstream test suite. No native Windows, embedded-device or
private corpus qualification is claimed.

## Behavior

- `omc_serialize_iptc_iim()` measures without allocation and writes a bounded
  canonical prefix after complete validation. It preserves raw Bytes/Text/
  Array payloads, duplicate and empty datasets, charset-first ordering and
  short/extended length encoding. Other failures preserve output. Overlap with
  source store storage is rejected. The C store has no finalized-state bit.
- JPEG/TIFF/DNG transfer shares the canonical engine. Unsupported or damaged
  native entries are skipped and counted; resource limits remain fatal. A clean
  opaque 0x0404 source carrier has precedence, while dirty native data suppresses
  that fallback. Public transfer, payload and package build result layouts gain
  `iptc_entries_skipped`; consumers must rebuild. Package wire versions stay
  unchanged, and the build counter is not serialized.
- JPEG IPTC replacement preserves each unrelated APP13 segment and each mixed
  carrier's residual at its source position, including names, duplicate IDs and
  padding. It removes all old IPTC records without merging carriers. The new
  carrier replaces the first affected segment, or precedes SOS/EOI when none
  exists. Post-SOS data is copied without inspection. Public execute stages both
  output arenas, supports aliased old output, and preserves output on failure.
- Native IPTC to XMP uses the same twenty mapping identities as reverse IPTC
  translation, with bounded per-block ASCII/UTF-8 validation, All/DirtyOnly,
  Fail/Preserve/Replace, dirty deletion and transactional publication. Repeated
  values sort by block/order/entry ID without deduplication. LangAlt owns only
  x-default. New XMP is Dirty UTF-8 with source provenance; updates retain
  destination provenance. Existing reverse policies and defaults are unchanged.

See [authoring.md](authoring.md) for API limits and failure semantics.

## Baseline and acceptance

Fresh original-C builds against the new reference passed all 48 direct targets
and 57/65 total targets. The eight broad inventory failures retained the exact
135 diagnostic multiset from the earlier accepted C++ 0.8.14 baseline.
The final Clang 20 C90 build uses warnings as errors for the C library and new
direct tests. Release assertions remain enabled in all new tests.

| Check | Result |
| --- | --- |
| Focused new IPTC/JPEG and existing reverse/transfer targets | 11/11 pass |
| Full Release schedule against the pinned C++ library | 64/72 pass; the same eight historical failures |
| ASan/UBSan, leak detection, all direct targets | 52/52 pass |
| Shared library, zlib and Brotli disabled, all direct targets | 52/52 pass |
| Broad CTest diagnostic multiset | 135, identical to the fresh baseline |
| Full `omc_test_parity --all` diagnostic multiset | 176, identical to the fresh baseline |

The paired new targets compare canonical IPTC bytes, four JPEG carrier cases,
all twenty forward mappings, a replacement, an unsupported repeated destination
shape and combined source/destination budget rejection. The direct matrix adds
wire boundaries, prefix/rollback/overlap, skip counts, per-block charset and
field validation, path bounds, repeated ordering/duplicates, conflicts,
tombstones, provenance, aliasing, malformed JPEG rollback and executed-package
replay. The existing reverse IPTC fixture remains green.

The failing Release targets remain `omc_test_parity`,
`omc_test_remaining_source_parity`, `omc_test_read_inventory`,
`omc_test_callback_inventory`, `omc_test_bmff_read_parity`,
`omc_test_bmff_callback_parity`, `omc_test_box_source_parity` and
`omc_test_tiff_source_parity`. This batch does not close their inventory gaps.

The new JPEG carrier test fails against the original C library at the byte
preservation assertion. The corrected implementation passes it. This establishes
that the fixture detects the former carrier merging, rather than merely testing
an already-supported path.

## Reproduction

From the workspace wrapper:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-0817-release -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DOMC_WARNINGS_AS_ERRORS=ON \
  -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-iptc-0817-build \
  -DOMC_OPENMETA_SOURCE_DIR=/tmp/openmeta-cpp-iptc-0817
cmake --build /tmp/openmeta-c-iptc-0817-release -j 2
ctest --test-dir /tmp/openmeta-c-iptc-0817-release --output-on-failure
/tmp/openmeta-c-iptc-0817-release/tests/omc_test_parity --all

cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-0817-sanitizer -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Debug -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/openmeta-c-iptc-0817-sanitizer -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/openmeta-c-iptc-0817-sanitizer --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-0817-disabled-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Release \
  -DOMC_WARNINGS_AS_ERRORS=ON -DOMC_BUILD_STATIC=OFF -DOMC_BUILD_SHARED=ON \
  -DOMC_USE_ZLIB=OFF -DOMC_USE_BROTLI=OFF
cmake --build /tmp/openmeta-c-iptc-0817-disabled-shared -j 2
ctest --test-dir /tmp/openmeta-c-iptc-0817-disabled-shared --output-on-failure
```

The complete Release schedule includes the existing eight failing inventory
targets. The focused batch uses `-R '^omc_test_(iptc_serialize(_parity)?|iptc_transfer|jpeg_iptc(_parity)?|iptc_to_xmp(_parity)?|iptc_translation|iptc_parity|transfer|transfer_persist)$'`.
Full local logs and review records are outside the public repository in
`_handoff/c_port_iptc_0817/` under the workspace wrapper.

## Remaining work

Port the newly committed 0.8.18 native IPTC date/time forward API, then resume
UP4 destination-profile validation and dates/basic capture/sensitivity/Flash/lens
lifecycle. Reader/decode expansion RD9/RD10 and the eight broad
inventory differences remain separate. Full RDF, prepared IPTC patching, C++
style JPEG in-place planning, full signed C2PA and stage-wide coverage estimates
are outside this acceptance. Preparation allocates and copies store/file data;
these tests do not establish an embedded memory budget or allocation-free path.
