# 0.6.19 CPU vertex evidence

The complete user capture from 0.6.17 supplies 1,333 draw records and 61,479
vertex occurrences. Format `0x115` accounts for 34,583 occurrences. It has a
10-byte record, so the existing full-record memo can specialize its byte count
without changing its hash, comparison, cache size, or invalidation rules.
Only the already guarded ambient `0x115` route selects this specialization.
A supplied size other than 10 falls back to the generic checked implementation.
Matrices, clipping, camera visibility, lighting and reflection calculations are
unchanged.

Replay against the unmodified `1c231b5` (0.6.18) decoder produced bit-identical
outputs for all 61,479 vertices, including colour, position, W, inverse W, UVQ and
fog. Both paths recorded 15,521 memo hits and 5,936 indexed hits; 22,102 decodes
used the existing ambient decoder. No float tolerance was used.

Host GCC results below are medians of seven alternating baseline/new runs;
each run reports the median of seven trials of ten captured CPU vertex frames.

| Compiler options | 0.6.18 | 0.6.19 | Interpretation |
| --- | ---: | ---: | --- |
| `-O3 -ffp-contract=fast` | 2.85178 ms | 2.70085 ms | 5.29% less host vertex replay time |
| `-O2` | 2.96414 ms | 2.97213 ms | Effectively unchanged (0.27% slower) |

These timings measure the captured CPU vertex stage on the host, not Android
frame time or game FPS. GPU submission, rendering, guest execution and display
pacing are outside this measurement. The capture predates the physical-camera
fix and is useful as an exact decoder workload, not as a new-device FPS test.

The self-contained `vertex_memo_0115_regression.cpp` test passed 36,864 bit-exact
decoder comparisons, with identical generic/specialized cache decisions
(18,484 hits and 18,380 misses). It covers changed records, collisions, material
and transform changes, layout changes, unknown revisions, failed decodes, and
short/null input. The same test passed ASAN and UBSAN; leak detection was disabled
for the container environment. A tested position-result cache was discarded
because it made this host workload slower.

## Reproduce without committing captured assets

From the repository root, point `capture_zip` at the supplied capture. Exported
fixtures and reference outputs remain temporary local files.

```sh
capture_zip='/path/to/LCS-render-capture (1).zip'
python3 android/tools/capture_vertex_fixture.py "$capture_zip" /tmp/lcs-vertex-input.bin
mkdir -p /tmp/lcs-0618-reference
git show 1c231b5:lcs/host/ge_renderer.cpp > /tmp/lcs-0618-reference/ge_renderer.cpp
git show 1c231b5:lcs/host/lcs_ge_vertex_memo.hpp > /tmp/lcs-0618-reference/lcs_ge_vertex_memo.hpp
g++ -std=c++20 -O3 -ffp-contract=fast -ffunction-sections -fdata-sections \
  -DLCS_BASELINE_MEMO_REPLAY \
  '-DLCS_TEST_RENDERER="/tmp/lcs-0618-reference/ge_renderer.cpp"' \
  -Iinclude -Ilcs/host android/tests/capture_vertex_replay.cpp src/guest_memory.cpp \
  -Wl,--gc-sections -pthread -o /tmp/lcs-vertex-before
g++ -std=c++20 -O3 -ffp-contract=fast -ffunction-sections -fdata-sections \
  -Iinclude -Ilcs/host android/tests/capture_vertex_replay.cpp src/guest_memory.cpp \
  -Wl,--gc-sections -pthread -o /tmp/lcs-vertex-after
/tmp/lcs-vertex-before /tmp/lcs-vertex-input.bin --write-reference /tmp/lcs-vertex-reference.bin
/tmp/lcs-vertex-after /tmp/lcs-vertex-input.bin --compare /tmp/lcs-vertex-reference.bin
```

`LCS_BASELINE_MEMO_REPLAY` retains the 0.6.17 ambient decoder while selecting the
original generic memo call. The older `LCS_BASELINE_REPLAY` option remains for
comparisons against 0.6.16, before that ambient decoder existed.

Self-contained regression:

```sh
g++ -std=c++20 -O3 -ffp-contract=fast -ffunction-sections -fdata-sections \
  -Iinclude -Ilcs/host android/tests/vertex_memo_0115_regression.cpp src/guest_memory.cpp \
  -Wl,--gc-sections -pthread -o /tmp/lcs-memo-test
/tmp/lcs-memo-test
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -Iinclude -Ilcs/host \
  android/tests/vertex_memo_0115_regression.cpp src/guest_memory.cpp \
  -Wl,--gc-sections -pthread -o /tmp/lcs-memo-sanitized
ASAN_OPTIONS=detect_leaks=0 /tmp/lcs-memo-sanitized
```

Local detailed logs were saved under ignored `android/test-results/` as
`vertex-memo-capture-0619-{O2,O3-ffp-contract-fast}.txt`,
`vertex-memo-capture-0619-summary.json`, `vertex-memo-0115-0619.txt`, and
`vertex-memo-0115-sanitized-0619.txt`. No user game assets are included here.
