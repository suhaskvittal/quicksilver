# Memory Stream Visualizer

author: Claude Code

Visualizes the epoch-by-epoch behavior of `qs_memory_scheduler`: which
qubits its active-set scheduler (EIF or HINT) loaded/evicted each
scheduling epoch, plus per-epoch metrics (compute since last swap, unused
bandwidth, DAG front-layer width).

This replaces an earlier tool that embedded an entire trace inline in one
HTML page and did not scale past small runs. This version instead has the
C++ pass write a compact seekable binary trace (`.qsmem`, see `FORMAT.md`),
and this directory's Python tool parses it and serves an interactive
browser UI that only ever fetches the currently visible (downsampled)
window — so pan/zoom cost is bounded by what's on screen, not by trace
size.

## 1. Produce a trace

```
qs_memory_scheduler input.trace output.trace \
    -c 32 -s 1 \
    --memory-stream out.qsmem
```

`-s 1` selects HINT (`-s 0` = EIF). `--memory-stream` must be a plain,
uncompressed path — not `.gz`/`.xz` — since the format is seekable by
byte offset.

## 2. Visualize it

```
./visualize.py out.qsmem
```

Starts a local server (default `http://127.0.0.1:8765/`) and opens a
browser tab. Panels:

- **Active-Set Residency Timeline** — a heatmap of which qubits were
  resident/loaded/evicted per epoch, with shaded "hot regions" where the
  scheduler is thrashing (memory accesses per unit compute more than `k`
  standard deviations above the trace's mean — `k` is the slider).
  Scroll/zoom with the buttons, the scrollbar, or the mouse wheel; click a
  column for full detail on that epoch.
- **Metrics per Epoch** — compute-since-last-swap, compute intensity,
  memory accesses, unused bandwidth, and front-layer width as line charts
  over the same epoch range, with the same hot-region shading.
- **Epoch Detail** — exact loaded/evicted/active-set qubit lists for
  whichever epoch was last clicked.
- **Aggregate Distributions** — qubit access-count ranking, residency-span
  length histogram, and unused-bandwidth histogram over the *whole* trace
  (computed once, not per window).

Useful flags: `--port`, `--host`, `--no-browser` (just start the server).

## 3. Sanity-check a file without a browser

```
./visualize.py --dump out.qsmem
```

Prints header fields, totals, and aggregate stats as text. Useful over
SSH, or for a quick check that a run produced the epoch/access counts you
expect (cross-check against the `SCHEDULING_EPOCHS`/`MEMORY_ACCESSES`
lines `qs_memory_scheduler` already prints to stdout).

## Layout

- `FORMAT.md` — the binary format spec; the contract between the C++
  writer (`src/compiler/pass/memory_scheduler/memory_stream.{h,cpp}`) and
  the Python reader here.
- `qsmem/format.py` — format constants + numpy structured dtypes.
- `qsmem/reader.py` — `MemoryStream`: mmap-backed, lazy per-block decode,
  whole-file aggregates, and a fallback path that recovers a truncated
  file (e.g. the writer process crashed mid-run) by scanning for block
  magics instead of trusting the (possibly-missing) footer/index.
- `qsmem/downsample.py` — buckets an epoch range down to a bounded number
  of display columns and a bounded qubit subset (the hottest `top`
  qubits), for the pan/zoom API.
- `qsmem/server.py` — stdlib `http.server`-based JSON API + static file
  serving (no third-party web framework is used or required).
- `qsmem/static/` — the browser UI (plain HTML/CSS/canvas, no JS
  libraries).
- `tests/test_roundtrip.py` — builds synthetic `.qsmem` files directly
  from the spec (independent of the C++ writer) and exercises the reader,
  including the truncated-file recovery path; run with
  `../venv/bin/python tests/test_roundtrip.py`.

## No new dependencies

Everything here runs on what's already in the repo's `venv/`: `numpy` for
the reader, and the Python standard library (`http.server`, `mmap`,
`struct`, `unittest`) for everything else.
