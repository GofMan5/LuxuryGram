# H3 Research Note: Rust Storage & Search Options for `ayudata.db`

**Status:** Research complete (pre-ADR; no Cargo dependencies added).  
**Scope:** Evaluate `fjall`, `rocksdb`, `surrealkv`, `tantivy`, and `zstd` against LuxuryGram's C++ SQLite persistence and substring search in `./tdata/ayudata.db`, and define a synthetic benchmark plan to decide Fjall vs. RocksDB in the upcoming H3 ADR (`rust/docs/PROGRESS.md`, `rust/docs/DECISIONS.md` ADR-010).

---

## 1. Current C++ Workload & Constraints

Source of truth:
- `Telegram/SourceFiles/luxury/data/entities.h`
- `Telegram/SourceFiles/luxury/data/luxury_database.h`
- `Telegram/SourceFiles/luxury/data/luxury_database.cpp`
- `Telegram/SourceFiles/luxury/data/messages_storage.cpp`
- `Telegram/SourceFiles/luxury/ui/message_history/history_inner.cpp`

### 1.1 On-disk shape and documented bottleneck
- **Database path & mode:** `./tdata/ayudata.db` via `sqlite_orm` (`luxury_database.cpp:21-22`), WAL mode with `synchronous=NORMAL`, `kBusyTimeoutMs = 3000`, `open_forever()` (`luxury_database.cpp:187,579-584`).
- **Concurrency model:** Single ordered queue (`crl::queue DatabaseQueue`) serialized by `std::recursive_mutex DatabaseMutex`, drained synchronously at quit (`luxury_database.cpp:160-169,287-317`).
- **Documented scale (`entities.h:34-41`):** `DeletedMessage` reaches **12 GB / 31.8M rows** in real databases. Dropping 14 unused scaffolding columns (`fwdPostAuthor`, `replyFlags`, ..., `mediaPath` = `"/"` in all 31.8M rows) caused `sqlite_orm::sync_schema` to run 14 full-table `ALTER TABLE DROP COLUMN` rewrites and hang startup.
- **Tables & indexes (`luxury_database.cpp:23-156`):**
  - `DeletedMessage`: 16 active `LuxuryMessageBase` columns (`fakeId` PK, `userId`, `dialogId`, `groupedId`, `peerId`, `fromId`, `topicId`, `messageId`, `date`, `flags`, `editDate`, `views`, `postAuthor`, `entityCreateDate`, `text`, `textEntities`) + 14 legacy no-op columns.
  - `EditedMessage`: 16 `LuxuryMessageBase` columns.
  - `OnlineEvent` & `WatchEvent`: per-peer rolling histories pruned on every insert to `kMaxOnlineEventsPerPeer = 200` and `kMaxWatchEventsPerPeer = 200` (`luxury_database.cpp:893-984`).
  - `RegexFilter` & `RegexFilterGlobalExclusion`: small rule/exclusion tables.
  - Composite B-tree indexes:
    - `idx_deleted_message_userId_dialogId_topicId_messageId`
    - `idx_edited_message_userId_dialogId_messageId`
    - `idx_online_event_userId_dialogId_fakeId`
    - `idx_watch_event_userId_dialogId_fakeId`

### 1.2 Hot query & write patterns
1. **Batch/single inserts:** `addDeletedMessages` wraps inserts in a transaction (`luxury_database.cpp:751-769`); `OnlineEvent`/`WatchEvent` append and prune tail rows per `(userId, dialogId)`.
2. **Cursor-paginated history reads (`getDeletedMessages` without `searchQuery`):** Filters `(userId, dialogId)`, `(topicId == arg || topicId == 0 || anyTopic)`, optional `messageId > minId` / `< maxId`, ordered by `messageId DESC LIMIT totalLimit` (`luxury_database.cpp:771-794`, `history_inner.cpp:811-849`).
3. **Unindexed substring search (`getDeletedMessages` with `searchQuery`):** Calls custom SQLite scalar UDF `luxury_contains_ignoring_case` (`ContainsIgnoringCase`, implemented via `QString::contains(..., Qt::CaseInsensitive)`) on `DeletedMessage::text` (`luxury_database.cpp:189-200,796-810`). SQLite must scan every candidate row for `(userId, dialogId, topicId)` and evaluate UTF-16 case-insensitive substring matching per row.
4. **Workspace constraints (`rust/Cargo.toml`, `rust/AGENTS.md`):** Edition `2024`, `GPL-3.0-only`, Windows x64 (MSVC) and Linux x64 primary targets, avoid `unsafe` in app code, no silent stubs, and keep `ayudata.db` migration/coexistence intact during transition.

---

## 2. Crate Comparison (Verified Metadata)

All versions, licenses, MSRVs, features, and dependency trees below were verified directly against the crates.io API and each crate's upstream repository manifests/source.

| Crate | Verified Version | License | MSRV / Ed. | Core Architecture | Native / Build Deps | Built-in Compression | License vs `GPL-3.0-only` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `fjall` | `3.1.13` (2026-10-25) | `MIT OR Apache-2.0` | `1.90.0` / 2021 | Pure-Rust LSM (`#![deny(unsafe_code)]`, `lsm-tree ~3.1.10`) | None (pure Rust) | `lz4` default (`lz4_flex 0.13.0`); **no Zstd** | Compatible |
| `rocksdb` | `0.25.0` (2026-08-16) | `Apache-2.0` | `1.88.0` (`1.89.0` on `master`) / 2021 | C++ RocksDB FFI (`librocksdb-sys ^0.19.0`) | C++20 toolchain + CMake/cc + **Clang/libclang** (`bindgen-runtime`) | `snappy`, `lz4`, `zstd`, `zlib`, `bzip2` (block-level SST + dict) | Compatible |
| `surrealkv` | `1.4.0` (2026-11-11) | `Apache-2.0` | `1.86` / 2021 | Pure-Rust async/Tokio LSM (`artmap`, `tokio 1.36`) | None by default (pure Rust + optional `zstd` C) | `None`, `SnappyCompression`, `ZstdCompression` (`zstd ^0.13`) | Compatible |
| `tantivy` | `0.27.0` (2026-10-17) | `MIT` | `1.88` (`1.91` on `main`) / 2021 | Pure-Rust inverted FTS index (`mmap`, `bitpacking`) | None (pure Rust; optional `zstd ^0.14`) | `lz4-compression` + `columnar-zstd-compression` default; `zstd-compression` opt. | Compatible |
| `zstd` | `0.14.4` (2026-11-12) | `BSD-3-Clause` | `1.64` / 2018 | Safe wrapper over `zstd-safe ^8.3.0` / `zstd-sys 2.1.3+zstd.1.5.7` | C compiler (`cc`) for `zstd-sys` (no Clang/bindgen); optional `rust-backend` | Zstd 1.5.7 (`legacy`, `arrays`, `zdict_builder` default) | Compatible |

---

## 3. Deep Dive by Candidate

### 3.1 `fjall` `3.1.13` — Pure-Rust Embedded LSM
- **Verified internals (`fjall/Cargo.toml`, `fjall/src/keyspace/options.rs`, `lsm-tree/src/compression.rs`):**
  - Normal dependencies: `lsm-tree ~3.1.10`, `xxhash-rust 0.8.15` (`xxh3`), `dashmap 6.1.0`, `flume 0.12.0`, `byteview ~0.10.1`, `byteorder-lite 0.1.0`, `tempfile 3.20.0`, `log 0.4.27`, optional `lz4_flex 0.13.0` (enabled by default).
  - Supports multi-keyspace isolation (`Keyspace` per entity type: `deleted_messages`, `edited_messages`, `online_events`, `watch_events`, `filters`), WiscKey-style `KvSeparationOptions` (useful if `textEntities` or large messages are separated), `SingleWriterTxDatabase` / `OptimisticTxDatabase`, prefix and range `DoubleEndedIterator` (maps cleanly to `messageId DESC` keyset pagination), and compaction filters.
- **Compression catch:** `lsm-tree/src/compression.rs` defines only `CompressionType::None` and `CompressionType::Lz4`. Default `CreateOptions` uses `[None, None, Lz4]` across levels. **Fjall does not compress SSTable data blocks with Zstd.**
  - *Implication for ADR-010:* To achieve Zstd compression with Fjall, LuxuryGram must compress serialized message payloads at the application boundary with `zstd` (`0.14.4`). Because individual Telegram messages are short (often 50–300 bytes), standalone per-row Zstd yields weak ratios and high header overhead unless paired with a **trained Zstd dictionary (`zstd::dict`)**—which works well for `text` + `textEntities` + `postAuthor` structs but does not compress the key prefix (`userId` + `dialogId` + `topicId` + `messageId`) across rows the way SSTable block compression does.
- **Strengths for LuxuryGram:** Zero C++ toolchain/Clang friction on Windows & Linux CI, pure-Rust memory safety (`rust/AGENTS.md` rule 6), fast incremental builds, clean synchronous API matching a dedicated storage actor/thread.
- **Risks:** Younger storage format/ecosystem than RocksDB; per-value Zstd dictionary versioning must be managed by LuxuryGram if dictionaries are evolved.

### 3.2 `rocksdb` `0.25.0` — Battle-Tested C++ LSM Bindings
- **Verified internals (`crates.io` `0.25.0`, `rust-rocksdb/Cargo.toml`):**
  - Default features: `snappy`, `lz4`, `zstd`, `zlib`, `bzip2`, `bindgen-runtime`. Optional: `multi-threaded-cf`, `mt_static` (Windows `/MT`), `io-uring` (Linux), `jemalloc`, `lto`.
  - Depends on `libc 0.2` and `librocksdb-sys ^0.19.0` (`0.20.0` on `master`).
- **Compression advantage:** Native **block-level Zstd** across SSTable blocks (e.g., 4–16 KiB data blocks compressed together after prefix-encoding sorted keys). Even 80-byte messages share a compression context within a 16 KiB SSTable block, naturally eliminating repetition across `dialogId`/`fromId`/common words without application-level dictionary management. Supports per-level compression (`LZ4` on L0/L1, `Zstd` on L2+ bottommost levels) and optional RocksDB dictionary compression.
- **Key-model fit:** Column Families map 1:1 to LuxuryGram's tables; prefix extractors + Bloom filters accelerate `(userId, dialogId, ...)` seeks; reverse iterators support `messageId DESC`; native compaction filters or `DeleteRange` handle `OnlineEvent`/`WatchEvent` pruning.
- **Build & operational costs:**
  - `librocksdb-sys` compiles a large C++ codebase and runs `bindgen` (requiring `libclang` / LLVM installed on every developer machine and CI runner).
  - `Cargo.toml` in `rust-rocksdb` explicitly notes `librocksdb-sys` compilation overhead (`debug = false` in dev profile because debug symbols add ~20% build time and bloat test binaries).
  - Windows MSVC builds require careful `/MT` vs `/MD` alignment (`mt_static` feature) and longer cold CI times, echoing the `LNK1102`/cache-time pressure already documented in root `AGENTS.md`.
  - Exposes `unsafe` FFI internals in `librocksdb-sys` (though `rocksdb` wraps them in safe Rust types).

### 3.3 `surrealkv` `1.4.0` — Pure-Rust Async LSM (Evaluated & Deprioritized)
- **Verified internals (`surrealkv/Cargo.toml`, `src/lib.rs`, `src/compression.rs`):**
  - Pure-Rust LSM with `artmap 0.6.0`, `quick_cache 0.6.16`, `parking_lot 0.12.1`, `tokio 1.36`, `lz4_flex 0.12.1`, `snap 1.0.0`, `zstd ^0.13` (`default-features = false`).
  - Default `encryption` feature pulls `aes-gcm 0.11`, `chacha20poly1305 0.11`, `hkdf 0.13`, `sha2 0.11`, `zeroize 1.8`.
  - Supports `CompressionType::{None, SnappyCompression, ZstdCompression}` per level, optional VLog (`vlog_value_threshold = 1024`), MVCC versioning (`enable_versioning`), `wal_preallocate`, and Ribbon/Bloom filters.
- **Why it fits poorly here:**
  1. **No Column Families / Keyspaces:** One `Tree` is one flat keyspace; LuxuryGram would need manual key-namespace prefixing or multiple separate `Tree` instances (each with its own WAL, memtable, and compaction loop).
  2. **Async/Tokio coupling:** Built around async Tokio runtime (`#[deny(clippy::await_holding_lock)]`, `tokio` features) rather than a simple sync handle, adding async complexity even for synchronous/worker-queued lookups.
  3. **Dependency conflict & bloat:** Pins `zstd ^0.13` while `tantivy 0.27.0` (`zstd-compression`) uses `zstd ^0.14`, forcing two `zstd`/`zstd-sys` versions (or a native link conflict on `zstd-sys` if both link C `libzstd`!) in the same workspace; also pulls TDE crypto crates by default even when unrequested.
  4. **Lower maturity for desktop 12 GB single-node stores:** SurrealKV is primarily tuned for SurrealDB's transactional/versioned embedded server and WASM/OPFS targets (`wasm32` VFS shims).

### 3.4 `tantivy` `0.27.0` — Full-Text & Substring Search Engine
- **Verified internals (`crates.io` `0.27.0`, `tantivy/src/tokenizer/ngram_tokenizer.rs`):**
  - Pure Rust (`MIT`), MSRV `1.88` on `0.27.0`. Default features: `mmap`, `stopwords`, `lz4-compression`, `columnar-zstd-compression`, `stemmer`, `plain_codec`.
  - Schema supports `u64`/`i64` `INDEXED | FAST` fields (`userId`, `dialogId`, `topicId`, `messageId`) and `TopDocs::with_limit(total_limit).order_by_fast_field("message_id", Order::Desc)`—directly matching `ORDER BY messageId DESC LIMIT totalLimit` and returning `(dialogId, messageId)` keys for hydration from the LSM store.
- **Critical semantic mismatch vs. C++ `ContainsIgnoringCase`:**
  - C++ (`luxury_database.cpp:190-200`) executes **exact Unicode case-insensitive substring matching** (`QString::contains(query, Qt::CaseInsensitive)`), not word/stem matching. For example, searching `"gram"` matches `"LuxuryGram"`, and searching `"прив"` matches `"Привет"`.
  - Standard Tantivy word tokenization (`SimpleTokenizer` + `LowerCaser`) **misses mid-word substrings**.
  - **Mitigation strategies to benchmark & decide in H3:**
    - *Option A (`NgramTokenizer` + post-filter):* Index `text` with `LowerCaser` + `NgramTokenizer::new(3, 3, false)` (trigrams). Queries of length >= 3 decompose into a phrase/conjunction of trigrams (`"luxury"` -> `"lux"`, `"uxu"`, `"xur"`, `"ury"`) and verify exact substring match on the top/candidate docs fetched from LSM. Queries of length 1–2 fall back to LSM scan over that `(userId, dialogId)` slice (or `min_gram = 2` at higher index size).
    - *Option B (Dual-mode search):* Word-tokenized FTS (`SimpleTokenizer` + `LowerCaser`) for fast multi-word queries + explicit substring/prefix fallback (`RegexQuery` or LSM dialog scan) if exact C++ substring parity is mandatory for short/partial tokens.
- **Windows `mmap` operational note:** Tantivy defaults to `memmap2` (`mmap` feature). On Windows, memory-mapped segment files stay locked while `Searcher`/`IndexReader` holds them, which requires careful `IndexReader` reload policies and graceful handling of delayed deleted-segment cleanup. Also check `deleted` / `edited` updates: `IndexWriter::delete_term` + `add_document` handles message edits/deletes cleanly, though frequent single-row commits are expensive—batching commits (e.g., every N writes or every 500–1000 ms, plus on shutdown) is required.

### 3.5 `zstd` `0.14.4` — Compression & Dictionary Layer
- **Verified internals (`crates.io` `0.14.4`):**
  - Wraps `zstd-safe ^8.3.0` -> `zstd-sys 2.1.3+zstd.1.5.7`. Default features: `["legacy", "arrays", "zdict_builder"]`. Optional `rust-backend` (`libzstd-rs-sys`), `zstdmt`, `seekable`.
  - Unlike `rocksdb` (`bindgen-runtime`), `zstd-sys` compiles plain C via `cc` with **no Clang/libclang requirement**, or can run pure-Rust via `rust-backend` if zero-C builds are desired.
- **Role in the two architectures:**
  - With **Fjall**: Mandatory for Zstd compression. Best used with `zstd::dict::from_samples` trained on representative message payloads (or batching/chunking) since individual messages average < 256 bytes.
  - With **RocksDB**: Built into `librocksdb-sys` at the SSTable block level (default `zstd` feature), so an external `zstd` crate is not needed for message storage unless sharing a single `zstd-sys` version with `tantivy`'s `zstd-compression` feature.

---

## 4. Architectural Fit for `ayudata.db`

### 4.1 Key-encoding & schema cleanup
Moving from SQLite (`DeletedMessage` 30 columns) to LSM drops the 14 dead scaffolding columns immediately (removing the `"/"` `mediaPath` waste across 31.8M rows) while a one-time background/startup importer reads existing `./tdata/ayudata.db` rows using `rusqlite` or C++ migration.

Proposed big-endian composite keys (preserving lexicographical sort order for `DoubleEndedIterator` reverse scans):
- **Deleted messages (`cf_deleted` / `ks_deleted`):**
  - Primary key: `user_id (i64 BE) || dialog_id (i64 BE) || message_id (i64 BE) || fake_id (i64 BE)` -> compressed value (`date`, `flags`, `edit_date`, `views`, `grouped_id`, `peer_id`, `from_id`, `topic_id`, `post_author`, `entity_create_date`, `text`, `text_entities`).
  - Optional secondary index for forum topics (`topic_id != 0`): `user_id || dialog_id || topic_id || message_id || fake_id -> empty` (or filter inline when a chat has few topics; note `getDeletedMessages` includes `topicId == 0` rows in every topic query per `luxury_database.cpp:773-787`, so a topic-filtered read must merge `topicId == arg` and `topicId == 0` streams).
- **Edited messages (`cf_edited` / `ks_edited`):**
  - Key: `user_id || dialog_id || message_id || fake_id` -> compressed `LuxuryMessageBase` value.
- **Rolling watcher logs (`cf_online`, `cf_watch`):**
  - Key: `user_id || dialog_id || fake_id` -> compact binary payload. Keeps the 200-row per-peer cap trivially via reverse seek + range/key delete.
- **Regex filters (`cf_filters`):**
  - Small keyspace (`filter_id -> payload`, `dialog_id || filter_id -> ()`).

### 4.2 Decision matrix: Fjall vs. RocksDB

| Criterion | `fjall` `3.1.13` + `zstd` `0.14.4` | `rocksdb` `0.25.0` |
| --- | --- | --- |
| **Build ergonomics (Windows MSVC + Linux)** | Excellent — pure Rust (`zstd-sys` optional C only, or `rust-backend`) | Heavy — C++ compilation + Clang/libclang required for `bindgen` |
| **Compression ratio on 100–300 B messages** | Needs trained `zstd::dict` per payload (or LZ4 block + dict) to beat SQLite | Native 4–16 KiB SSTable block Zstd compresses across adjacent messages automatically |
| **Write amplification & compaction tuning** | Good (`lsm-tree` leveled compaction, `KvSeparationOptions`, `SingleWriterTxDatabase`) | Industry-standard (`OptimizeLevelStyleCompaction`, per-level Zstd/LZ4, `DeleteRange`, battle-tested at multi-GB) |
| **Safety & `rust/AGENTS.md` rule 6** | `#![deny(unsafe_code)]` in `fjall` | C++ engine behind FFI (`librocksdb-sys`) |

- **`surrealkv` `1.4.0` verdict:** **Reject for H3** — single-tree (no CFs/keyspaces), Tokio-coupled, `zstd ^0.13` dependency conflict with `tantivy`, and unnecessary MVCC/TDE overhead.

**Preliminary hypothesis to test (not pre-judged):**
- **RocksDB** likely wins on raw disk-space reduction and cold compaction stability at 12 GB+ due to native cross-row SSTable block Zstd, at the cost of Clang/C++ build complexity in CI.
- **Fjall + `zstd` (dictionary-compressed values)** likely wins on build speed, binary simplicity, and Rust safety, and may match RocksDB's disk ratio if Zstd dictionary compression handles small message payloads well.
- **Tantivy** (`NgramTokenizer` vs. standard tokenizer) solves the `ContainsIgnoringCase` O(N) scan bottleneck, provided index disk overhead and Windows `mmap`/commit latency stay within budget.

---

## 5. Proposed Synthetic Benchmark Plan (Before H3 ADR)

Run a standalone disposable benchmark harness (outside the workspace `Cargo.toml` or in a throwaway scratch branch/bench crate) on **Windows x64** (matching primary desktop target) with **1M messages** first for fast iteration, then **5M messages** to stress compaction and cache spill.

### 5.1 Synthetic Dataset Generator
Generate deterministic messages modeled after `DeletedMessage` (`entities.h:13-32`):
- **Distribution:** 1 user, 200 dialogs (10% heavy group/forum chats with `topicId != 0`, 90% DMs/small chats), Zipfian messages per dialog.
- **Payloads:** Mixed English + Cyrillic UTF-8 strings (e.g., 60% short 30–140 B, 30% medium 150–600 B, 10% long 1–4 KB; realistic Telegram entity blobs in `textEntities` 0–512 B).
- **Baseline reference:** Populate the exact 16-column + 14-dead-column SQLite WAL table (plus composite index) as the C++ control baseline.

### 5.2 Candidates Under Test
1. **SQLite WAL baseline** (`rusqlite` with `ContainsIgnoringCase` Rust UDF + `idx_deleted_message_userId_dialogId_topicId_messageId`).
2. **Fjall (`3.1.13`)**:
   - Variant A: Default `lz4` block compression, raw payload.
   - Variant B: `fjall` (`lz4` off or on) + payload `zstd` (`0.14.4`) with a 64 KiB trained dictionary (`zstd::dict::from_samples`).
3. **RocksDB (`0.25.0`)**:
   - Block-based SSTable, `LZ4` on L0–L1 + `Zstd` on L2+ (16 KiB block size, prefix Bloom filter on `(userId, dialogId)`).
4. **Tantivy (`0.27.0`)** paired with the LSM store:
   - Variant A: `NgramTokenizer(3, 3, false)` + `LowerCaser` (exact substring candidate index + LSM verify).
   - Variant B: `SimpleTokenizer` + `LowerCaser` (word index, to quantify the disk/speed cost of trigram vs. word indexing).

### 5.3 Metrics & Pass/Decision Thresholds
Measure on cold OS page cache where applicable (or separated warm vs. cold runs on NVMe SSD):

| Metric | How to Measure | Decision Signal |
| --- | --- | --- |
| **1. On-disk footprint (GB)** | Total directory size (`ayudata.*` + Tantivy index) after full ingest + manual compaction/flush, at 1M and 5M rows. | Verify ADR-010's 3–5x target vs. SQLite (16-col and 30-col). Measure Tantivy trigram vs. word index overhead separately. |
| **2. Ingest throughput (msg/s & p99 tail)** | Bulk import of 1M/5M rows (migration path) + steady-state batches of 1–50 messages (live runtime path). | Must avoid long write stalls (`p99` batch < 20 ms for live writes; 5M migration < 2 min). |
| **3. Cold paginated read latency (p50 / p99)** | Query `limit = 50` messages (`ORDER BY messageId DESC`, with and without `topicId` filter) across 1,000 random `(dialogId, maxId)` cursors after cache clear. | Target < 5 ms p99 (UI 120 Hz scroll budget is 8.3 ms/frame). |
| **4. Substring search latency (p50 / p99)** | 500 mixed ASCII + Cyrillic case-insensitive substring queries (lengths 2, 3, 6, 12 chars) inside a large 500K-msg dialog (`limit = 50`). | Compare SQLite UDF linear scan vs. Tantivy trigram + LSM hydration. Target < 20 ms p99 even on 500K-msg dialogs. |
| **5. Zstd compression ratio & CPU** | Compressed bytes / raw payload bytes; encode/decode MB/s for Fjall+`zstd::dict` vs. RocksDB block Zstd. | Determines whether Fjall+dict matches RocksDB's block compression closely enough to avoid RocksDB's Clang/C++ build cost. |
| **6. Build & binary delta** | Clean `cargo build --release` wall time and binary size delta on Windows x64 MSVC. | Quantifies real CI cost of `librocksdb-sys` + `bindgen` vs. `fjall` + `zstd`. |

### 5.4 Decision Rule for the H3 ADR
- **Choose `fjall` + `zstd` (dictionary) + `tantivy`** if: Fjall+dict achieves >= 80% of RocksDB's disk savings (still >= 2.5–3x smaller than SQLite), cold `p99` read is <= 5 ms, and write stalls under 5M-row compaction stay bounded—saving C++/Clang build complexity and keeping `unsafe` out of the storage layer.
- **Choose `rocksdb` + `tantivy`** if: Fjall+dict trails RocksDB significantly on disk footprint (> 25% larger at 5M rows) or exhibits high write/read tail latency under 5M-row compaction, justifying the `librocksdb-sys` + Clang CI dependency.
- **Keep `tantivy` trigram vs. word indexing explicit:** Adopt `NgramTokenizer(3, 3, false)` + LSM verification (with LSM scan fallback for 1–2 char queries) only if its index size stays <= 1.0–1.5x the compressed message store; otherwise switch to word-tokenized Tantivy + async scan fallback for mid-word substrings and document the behavioral delta from C++ `ContainsIgnoringCase`.
