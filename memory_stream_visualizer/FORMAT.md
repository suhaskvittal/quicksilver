# The `.qsmem` memory stream format

author: Claude Code
date: 8 September 2026

This is the binary trace format written by `MemoryStreamWriter`
(`src/compiler/pass/memory_scheduler/memory_stream.{h,cpp}`) when
`qs_memory_scheduler` is run with `--memory-stream <path>`, and parsed by
`qsmem/reader.py` in this directory. It records, epoch by epoch, which
program qubits the memory scheduler pass loaded into / evicted from its
active set, plus per-epoch scheduling metrics.

It is a **contract between the C++ writer and the Python reader** — if you
change one side, change the other, and bump `FORMAT_VERSION` in both
`memory_stream.h` and `qsmem/format.py` if the on-disk layout changes.

## Conventions

- All multi-byte integers are **native little-endian**. The writer asserts
  `std::endian::native == std::endian::little` at compile time rather than
  byte-swap; the reader assumes the same (all target hosts are little-endian).
- All structs are **packed** (no padding between fields).
- `0xFFFFFFFF` (`QSMEM_NONE`) is a sentinel meaning "no such qubit" wherever
  a `u32` qubit id appears.
- The file must be a **plain, uncompressed, seekable file** — not `.gz` or
  `.xz`. Seekability (jumping to any block via the index) is the entire
  point of this format; `qs_memory_scheduler` refuses `--memory-stream`
  paths containing `.gz`/`.xz`.

## Layout

```
HEADER | BLOCK* | INDEX (one entry+keyframe per block) | FOOTER
```

**The footer and index are an optimization, not a requirement.** If the
writing process crashes or is killed mid-run, they will be missing or
truncated. A reader must be able to recover in that case: every block
header carries its own `first_epoch_index`/`first_access_index`, so a
reader can scan forward from the end of the header for `"QSMB"` magics and
reconstruct the epoch/access numbering without the index. Residency in a
recovered file can still be reconstructed exactly, starting from the
header's `initial_active_set` and replaying `loaded`/`evicted` deltas
block by block — it just costs an O(file) scan instead of O(1) block
lookups via the index.

### HEADER (48 bytes, then a variable-length tail)

| Field                   | Type      | Notes                                   |
|-------------------------|-----------|------------------------------------------|
| `magic`                 | `char[8]` | `"QSMEMSTR"`                              |
| `version`               | `u16`     | currently `1`                             |
| `flags`                 | `u16`     | reserved, currently `0`                   |
| `header_size`           | `u32`     | byte offset of the first BLOCK            |
| `num_qubits`            | `u32`     | program qubit count                       |
| `active_set_capacity`   | `u32`     | active-set size (all keyframes are padded to this length) |
| `block_epoch_capacity`  | `u32`     | epochs per block (last block may be short)|
| `scheduler_id`          | `u32`     | `0 = EIF`, `1 = HINT`, others reserved     |
| `scheduler_name`        | `char[16]`| NUL-padded, not necessarily NUL-terminated if exactly 16 chars |

Immediately following the fixed 48-byte header:

| Field                     | Type       | Notes                              |
|---------------------------|------------|-------------------------------------|
| `initial_active_set_len`  | `u32`      | length of the next array            |
| `initial_active_set`      | `u32[len]` | sorted ascending, **not** padded    |

`header_size` = `48 + 4 + 4*initial_active_set_len`.

### BLOCK (repeated `block_count` times)

Block header (32 bytes):

| Field               | Type  | Notes                                          |
|---------------------|-------|--------------------------------------------------|
| `magic`             | `char[4]` | `"QSMB"`                                      |
| `epoch_count`       | `u32` | epochs in this block                              |
| `first_epoch_index` | `u64` | global index of this block's first epoch          |
| `access_count`      | `u64` | AccessRecords in this block                       |
| `first_access_index`| `u64` | global index of this block's first AccessRecord    |

Then `epoch_count` **EpochRecord**s, 40 bytes each, in epoch order:

| Field                | Type  | Meaning                                                        |
|----------------------|-------|------------------------------------------------------------------|
| `inst_done`          | `u64` | cumulative unrolled instructions retired *before* this epoch      |
| `access_begin`       | `u64` | global AccessRecord index where this epoch's accesses start       |
| `access_count`       | `u32` | number of AccessRecords for this epoch                            |
| `compute_since_prev` | `u32` | unrolled instructions retired since the previous epoch             |
| `unused_bandwidth`   | `u32` | qubits left untouched by this epoch's swaps (>0 = wasted capacity) |
| `front_layer_width`  | `u32` | DAG front-layer size at the moment this epoch triggered            |
| `dag_inst_count`     | `u32` | DAG instruction count at that moment                               |
| `reserved`           | `u32` | must be `0`                                                        |

Then `access_count` **AccessRecord**s, 8 bytes each, in the order the
scheduler emitted them:

| Field         | Type | Meaning                                                    |
|---------------|------|--------------------------------------------------------------|
| `load_qubit`  | `u32`| qubit loaded into the active set, or `QSMEM_NONE`             |
| `evict_qubit` | `u32`| qubit evicted from the active set, or `QSMEM_NONE`             |

Today the scheduler only emits paired `COUPLED_LOAD_STORE` swaps, so every
AccessRecord has both fields set; unpaired `LOAD`/`STORE` (not currently
emitted) would set only one side. Per-epoch loaded/evicted qubit *lists*
are exactly the AccessRecords in `[access_begin, access_begin+access_count)`
— they are not duplicated anywhere else in the format.

### INDEX (`block_count` entries, only valid if the FOOTER is present)

Each entry is a 24-byte **QsmemIndexEntry** immediately followed by a
`u32[active_set_capacity]` **keyframe** (sorted ascending, `QSMEM_NONE`
padded past the actual active-set size):

| Field               | Type  | Notes                                              |
|---------------------|-------|------------------------------------------------------|
| `block_file_offset` | `u64` | byte offset of this block's header                   |
| `first_epoch_index` | `u64` | duplicate of the block header field, for convenience  |
| `epoch_count`       | `u32` | duplicate of the block header field                   |
| `reserved`          | `u32` | must be `0`                                           |

The keyframe is the **active set as it existed entering this block** —
i.e. before any of the block's own epochs ran. Block 0's keyframe equals
the header's `initial_active_set`. This is what lets a reader jump to an
arbitrary epoch and know the exact active set at that point without
replaying every access from epoch 0: seek to the keyframe of the block
containing the target epoch, then replay only that block's own accesses
up to the target.

### FOOTER (40 bytes, at end of file)

| Field            | Type      | Notes                          |
|------------------|-----------|----------------------------------|
| `index_offset`   | `u64`     | byte offset of the INDEX section |
| `block_count`    | `u64`     |                                   |
| `total_epochs`   | `u64`     |                                   |
| `total_accesses` | `u64`     |                                   |
| `magic`          | `char[8]` | `"QSMEMEND"`                     |

A reader should first try: read the last 40 bytes, check the magic, then
jump straight to `index_offset`. Only if that fails (missing/short file,
bad magic) fall back to the forward-scan recovery path described above.

## Size, roughly

`~40 bytes/epoch + 8 bytes/access + ~4 bytes/(active_set_capacity) per
block for keyframes`, plus a fixed ~48-56 byte header/footer. This is
linear in trace size, unlike the old inline-HTML tool this replaces, and
lets the visualizer downsample rather than hold the whole trace in memory.
