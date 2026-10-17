# Explicit IPTC carrier removal against C++ 0.8.20

C package `0.17.0`, local revision `0.17.0.017`. Validated 2026-10-17.

## Reference and scope

The immutable C++ reference is
`0f492554fd57ee65381e6092debd88efc88c1285` (0.8.20), built with Clang 20
and libc++ from `/tmp/openmeta-cpp-iptc-removal-0820` into
`/tmp/openmeta-cpp-iptc-removal-0820-build`. The C baseline is
`7039d8f2a60b42ef6b0db0e81db433aff08f9489`, archived and freshly rebuilt in
`/tmp/openmeta-c-iptc-removal-0820-baseline`. Reference and library hashes are
recorded in the private receipt. The live C++ checkout is not modified.

This increment covers explicit native IPTC carrier removal in the existing C
prepare/compile/execute path. It does not add owning C++ prepared snapshots,
source-snapshot wire formats, codec adapters or C2PA signing support.

## Behavior and ownership

Native `include_iptc` must be zero or one, defaults to one and is independent
of IPTC-to-XMP projection. Prepare derives `remove_existing_iptc` only for JPEG/TIFF/DNG,
with native output enabled, no active native IPTC datasets and a Dirty|Deleted
native dataset. Version, charset, unknown and damaged active datasets all
suppress removal. Empty/omitted data, clean tombstones and XMP-only tombstones
do not remove a carrier. Stale opaque Photoshop IPTC cannot restore a deleted
native group.

The policy is copied inline through bundle and executor. Their layouts and the
prepare-options layout change; rebuild callers with matching headers/binaries
and initialize options. C borrows the source store and checks the removal
lifecycle again at execution. The compiled policy must still match that source;
changing the native source state requires preparation again. This differs from
the C++ bundle's independently owned payload and manually authored edit intent.

JPEG removes all managed `0x0404` resources from recognized leading Photoshop
APP13 streams. Pure unrelated segments remain intact; mixed segments retain
unrelated resources in their original order and placement, including duplicate
IDs, names and padding. Recognized malformed resource streams fail. The scan
tail and other JPEG segments remain byte-for-byte unchanged by removal.

TIFF/BigTIFF/DNG removes IFD0 tag 33723 after EXIF overlay, so a copied EXIF tag
cannot reintroduce it. Rebuilding IFD0 keeps unrelated entries, child/page links
and original offsets. The old directory and IPTC bytes can remain unreachable
in the file; this operation does not erase historic bytes. A target without
33723 succeeds without a deletion-specific directory rebuild.
Disabling native IPTC output does not filter a source tag 33723 supplied through
the independent EXIF path; the direct fixture verifies both disabled-copy and
enabled-removal outcomes.

Both output arenas are staged and published together on success. Removal needs
an existing destination; a fresh DNG scaffold with removal intent fails as
unsupported. Payload-only and fresh package builders also reject this intent
before resetting caller storage. Executed-output packages persist the edited
bytes using existing package wire versions.

## Validation

The fresh baseline passes 73/81 Release targets, with the same eight historical
failures as the previous pin. Inventories match the previous receipt: 135 CTest
diagnostics, 176 full-inventory reports and 351 detailed records.

| Final check | Result |
| --- | --- |
| Clang 20 Release | 75/83; eight historical inventory failures remain |
| ASan/UBSan with leak detection, direct C | 59/59 |
| Dependencies disabled, shared library, direct C | 59/59 |
| C/C++ derivation combinations | 54 |
| Paired carrier comparisons | 24: eight JPEG, sixteen TIFF/DNG |
| Standalone old/new counterfactual | Old retains the carrier (exit 1); new removes it (exit 0) |

The eight retained failures are `omc_test_parity`,
`omc_test_remaining_source_parity`, `omc_test_read_inventory`,
`omc_test_callback_inventory`, `omc_test_bmff_read_parity`,
`omc_test_bmff_callback_parity`, `omc_test_box_source_parity`, and
`omc_test_tiff_source_parity`. Diagnostic multisets remain identical to the
fresh baseline: 135 CTest diagnostics, 176 full-inventory reports and 351
detailed records. Complete `--all` output matches after normalizing source
paths and generated temporary timestamps. Final C builds have no warnings.

Direct coverage includes all four TIFF layouts/byte orders, already-absent
and repeated removal, empty/duplicate-only IFDs, DNG existing/template/fresh
behavior, copied-EXIF precedence, padded JPEG resources, aliasing, stale source,
invalid policy flags, payload-storage preservation and malformed input with
both output arenas populated. Updated older reverse-date and JPEG fixtures
now require explicit last-carrier removal while retaining clean-tombstone
omission checks. TIFF arithmetic guards are source-reviewed; truncated table
cases are exercised without using fictitious huge backing buffers.

The paired fixture checks 54 derivation combinations across nine source states,
three formats and enabled/disabled output. An unreadable active dataset blocks
removal in both libraries; C++ reports Unsupported at preparation when it leaves
no output, while C defers serialization. This existing status-stage difference
is explicit in the fixture.

Twenty-four carrier comparisons cover eight JPEG variants and sixteen TIFF/DNG
variants, including classic/BigTIFF, both byte orders and already-absent tags.
JPEG compares exact expected bytes after excluding C's generated XMP packet;
TIFF compares all fixture native fields except C's generated tag 700. These are
selected removal comparisons, not whole-transfer XMP parity. JPEG repeats also
exercise input bytes aliasing the old output arena. Package serialization and
replay are checked after freeing builder storage and serialized bytes.

The standalone counterfactual uses only the preexisting public API and matching
old headers/library. It reports a retained last IPTC carrier against C 0.16.0;
the same source must pass against this implementation. No incompatible-layout
link is used as counterfactual evidence.

## Limits and next work

No full upstream C++ test suite, native Windows, embedded device/resource,
private corpus, installed-consumer or performance acceptance follows from these
WSL checks. Source snapshots, independent signing failures and the eight broad
reader/decode residuals remain separate gates.

Next: shared opt-in destination-profile validation, then the remaining UP4
basic capture, sensitivity, Flash and lens families. Recheck upstream before
pinning each batch.

## Reproduction

The reference build uses static OpenMeta, `OPENMETA_USE_LIBCXX=ON`, and
`CMAKE_PREFIX_PATH=/mnt/e/UBc/Release`, with tests/tools/Python, DNG SDK,
RapidFuzz and C2PA verification disabled.

From the workspace wrapper:

```sh
cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-removal-0820-release -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Release \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DOMC_WARNINGS_AS_ERRORS=ON \
  -DOMC_BUILD_PARITY_TESTS=ON \
  -DOMC_OPENMETA_DIR=/tmp/openmeta-cpp-iptc-removal-0820-build \
  -DOMC_OPENMETA_SOURCE_DIR=/tmp/openmeta-cpp-iptc-removal-0820
cmake --build /tmp/openmeta-c-iptc-removal-0820-release -j 2
ctest --test-dir /tmp/openmeta-c-iptc-removal-0820-release --output-on-failure
/tmp/openmeta-c-iptc-removal-0820-release/tests/omc_test_parity --all

cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-removal-0820-sanitizer -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/e/UBc/Debug -DOMC_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/openmeta-c-iptc-removal-0820-sanitizer -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/openmeta-c-iptc-removal-0820-sanitizer --output-on-failure

cmake -S OpenMeta-c -B /tmp/openmeta-c-iptc-removal-0820-disabled-shared -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Release \
  -DOMC_WARNINGS_AS_ERRORS=ON -DOMC_BUILD_STATIC=OFF -DOMC_BUILD_SHARED=ON \
  -DOMC_USE_ZLIB=OFF -DOMC_USE_BROTLI=OFF
cmake --build /tmp/openmeta-c-iptc-removal-0820-disabled-shared -j 2
ctest --test-dir /tmp/openmeta-c-iptc-removal-0820-disabled-shared --output-on-failure
```

Private logs, reference/source hashes, reviews and residual comparisons are in
workspace `_handoff/c_port_iptc_removal_0820/`, outside the public C repository.
