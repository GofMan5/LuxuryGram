# fast-anything trial on LuxuryGram (2026-11-23, branch `luxurygram-rust`)

## What was installed

- `cargo install fast-anything --locked` → **failed**: the crate is not in this
  machine's crates.io index (`could not find fast-anything in registry crates-io`).
- `npm i -g @el4cteo/fast-anything` → **fast-anything 0.8.0** (prebuilt win32-x64
  binary). `fast-anything doctor`: cargo 1.99, gcc(MinGW) 13.2, Python 3.14,
  Node 26, bun 1.3, Go 1.25, dotnet 10, claude/codex present → ready.

## Real target and behavior-preserving seam

Target: `Telegram/SourceFiles/_other/zxcvbn_translit.cpp`, the standalone C++
dictionary transliterator (also the black-box oracle for the existing Rust
`zxcvbn-translit` crate).

fast-anything initially reported every hot function unportable: `MapWord` took
`std::string_view` + `std::string&`, `DecodeUtf8` returned a struct. We changed
**only the contract**, not the algorithm:

- `DecodeUtf8(std::string_view, size_t)` kept as a private helper;
- `MapWord(const std::string&) -> std::string` (empty = reject, matching the old
  `false` + empty out-parameter);
- new pure `TransformDictionary(const std::string&) -> std::string` containing
  the whole line loop, so the native↔Rust boundary is crossed once per run;
- `main` now reads the file, calls the pure function, and writes the result.

Byte-for-byte equivalence with the pre-existing reference binary
(`rust/verification/zxcvbn-translit/reference/zxcvbn_translit.exe`):

| workload | SHA-256 of output | identical |
| --- | --- | --- |
| `Telegram/ThirdParty/zxcvbn/words-eng_wiki.txt` (2.46 MB) | `7C864FCE6637315D98D91888DE7145DE91A55FC29A6F5A103E63496D502F941F` | yes |
| `.ai/cyrillic-dictionary.txt` (100k Cyrillic words, 2.26 MB) | `0996E498713D3335FA75A66878E40F7193ECD9A2A636977ECCC5827C26566E64` | yes |

## Profiling and verified Rust ports

Exact scan command (app arguments after the second `--`):

```
fast-anything scan -- g++ -std=c++20 -O2 -ITelegram/SourceFiles \
  Telegram/SourceFiles/_other/zxcvbn_translit.cpp \
  Telegram/SourceFiles/ui/passcode_strength_translit.cpp \
  -- <dictionary> <output>
```

| workload | top function | share | calls |
| --- | --- | --- | --- |
| English wiki (2.46 MB) | `TransformDictionary` | 95.3% | 1 |
| Cyrillic (2.26 MB) | `TransformDictionary` | 98.6% | 1 |

Two ports were written by hand from the pinned sources (no `todo!()` left):

1. `MapWord` — **786/786 calls matched** (500 recorded + 86 edge + 200 derived).
   Per call: 77.9 ns C++ vs 1534.8 ns Rust → 20× slower; tool refused to enable.
2. `TransformDictionary` — **35/35 calls matched** (1 recorded + 8 edge + 26 derived)
   on the Cyrillic workload; 36/36 on the earlier English workload. Per call
   (Cyrillic): 29.7 ms C++ vs 25.5 ms Rust → 1.2× faster; tool allowed enable.

## Why the port was switched back off

`fast-anything enable <id>` + `fast-anything bench --runs 5` on the Cyrillic
workload:

```
C++   250 ms
Rust  299 ms
0.8x faster end to end (median of 5, +/-2 ms)
```

The generated C++ bridge (`// >>> fast-anything: ... TransformDictionary bridge >>>`)
serializes the **entire** 2.26 MB input into one JSON string, calls the Rust
cdylib, then parses the ~1.3 MB JSON result back into a custom `V` tree. The
micro-benchmark times only the function pair; the whole-app bench includes that
bridge. For this linear byte-processing function the bridge costs more than the
transform saves. `fast-anything disable <id>` restored the original code; the
final C++ source contains zero `fast-anything` markers and both output hashes
above were re-checked after disabling.

## Binary C ABI: the verified port becomes a real win

The fast-anything port was correct but bridge-bound. Keeping the same verified
logic, we replaced its JSON bridge with a binary C ABI in the existing
`zxcvbn-translit` crate (`ffi` feature) and added an opt-in call site in the
C++ CLI:

- `luxury_zxcvbn_transform(input, len, out_len) -> bytes` and
  `luxury_zxcvbn_free(bytes, len)` pass raw byte buffers, no JSON, one crossing
  per dictionary.
- `transform_optimized` reuses a single scratch buffer for mapped words. The
  original `map_word` allocated a `String` per word and was 2× slower than C++
  on short ASCII words; the optimized version has no per-word heap allocation.
- The C++ path is unchanged by default. Set `LUXURY_ZXCVBN_RUST=1` and
  `LUXURY_ZXCVBN_RUST_DLL=<path to rust/target/release/zxcvbn_translit.dll>`
  after `cargo build -p zxcvbn-translit --features ffi --release`. Missing or
  incompatible library falls back silently to `TransformDictionary`.

In-process benchmark (20 interleaved iterations, same binary and inputs,
`.ai/bench-ffi.cpp`; process startup and file I/O excluded):

| workload | C++ native | Rust binary FFI | speedup |
| --- | --- | --- | --- |
| `words-eng_wiki.txt` | 8.038 ms | 4.521 ms | **1.78×** |
| `.ai/cyrillic-dictionary.txt` | 31.250 ms | 12.403 ms | **2.52×** |

Correctness after the FFI path: both dictionary outputs are byte-identical to
the pre-existing reference binary (SHA-256 values above), and all 7 recorded
oracle cases match exit code and output. `cargo fmt --all`,
`cargo clippy --workspace --all-targets -- -D warnings`,
`cargo clippy -p zxcvbn-translit --features ffi --all-targets -- -D warnings`
and `cargo test --workspace` pass.

## Decision rule for the next candidate

fast-anything's C/C++ path pays a fixed per-call and per-byte JSON cost. It only
wins where one call does much more CPU work than its serialized arguments:

- good: O(n²) / heavy per-element compute, small arguments (the tool's own C++
  fuzzy-dedupe example: 3.02 s → 1.09 s);
- bad: streaming transforms, copies, simple decoders — any function whose cost is
  proportional to the JSON bytes it must cross.

Next candidates from `FORK_DELTA.md` must be checked against this rule before a
port is attempted: the Qt-free WebSocket framing core
(`mtproto/details/mtproto_websocket_logic.*`, black-box harness exists but ADR-004
confirmation is required) and fork-owned plugin/TL helpers. Do not enable a port
whose `fast-anything bench` result is not faster than the original.

## Triage: Qt-free WebSocket framing (2026-11-23)

Standalone harness `.ai/ws-bench.cpp` (200k encode + decode cycles, 700 ms)
profiled with both `.ai/ws-bench.cpp` and
`Telegram/SourceFiles/mtproto/details/mtproto_websocket_logic.cpp` as sources:

| function | share | blocker |
| --- | --- | --- |
| `WsEncodeFrame` | 48.9% | takes a `WsOpcode` enum |
| `WsFrameDecoder::parseFrame` | 17.7% | method defined outside its class |
| four decoder helpers | 1.3–6.5% | same |

Nothing is portable without changing public signatures and class structure, and
the work per call is linear masking/copying (~1.8 µs), close to fast-anything's
JSON bridge cost. No port attempted: expected payoff is negative. This is a
measured triage result, not a claim that WebSocket cannot be optimized later.

## Artifacts

- `.fast/` — generated profiler state, recordings and port crates (gitignored;
  can be deleted and rebuilt). The two `src/port.rs` implementations are kept
  there for reuse if the target is revisited.
- `.ai/` — local workload and intermediate outputs (gitignored).
- No runtime product behavior changed: the only source edits are the verified
  behavior-preserving refactoring of `zxcvbn_translit.cpp` plus an opt-in Rust
  FFI call site (disabled unless both environment variables are set).
