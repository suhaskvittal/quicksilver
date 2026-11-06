# tests/

Minimal, dependency-free CTest validation tests. Each `.cpp` file is a
standalone executable: it includes `test_util.h` for the `CHECK*`/`RUN`
macros, defines test functions, runs them from `main`, and returns
`qstest::finish()` (non-zero exit = failure, reported to CTest).

## test_util.h

Not a test itself — the shared assertion harness used by every file below.

- `CHECK(cond)` — boolean assertion.
- `CHECK_EQ(a, b)` — equality assertion, prints both values on failure.
- `CHECK_NEAR(a, b, eps)` — absolute-tolerance float comparison.
- `CHECK_REL(a, b, rel)` — relative-tolerance float comparison, for values
  spanning many orders of magnitude (e.g. error rates).
- `RUN(fn)` — logs and invokes a test function.
- `qstest::finish()` — prints a pass/fail summary and returns the process
  exit code.

## test_memory_level.cpp

Tests `MemoryLevel::striped_mapping` (`sim/memory_level.h`), exercised
through the concrete `QLDPCMemory` subclass. `striped_mapping` round-robins a
range of qubits across a level's code blocks (qubit at position `i` goes to
block `i % num_blocks`).

- `test_block_count_and_capacity` — the constructor computes
  `num_blocks = ceil(qubit_count / k)` and rounds `total_capacity` up to
  `num_blocks * k`, for both an exact multiple and a non-multiple qubit count.
- `test_striped_mapping_round_robin` — 10 qubits over 4 blocks land exactly
  at block `i % 4` (and nowhere else), with balanced block sizes `{3,3,2,2}`.
- `test_striped_mapping_fewer_than_blocks` — 3 qubits over 4 blocks fill
  blocks 0-2 and leave block 3 empty.
- `test_striped_mapping_exact_multiple` — 8 qubits over 4 blocks gives
  exactly 2 qubits per block.
- `test_striped_mapping_empty` — mapping an empty range inserts nothing;
  all blocks stay empty.

## test_epr_memory.cpp

Checks EPR-backed memory access for both baseline ideal distribution and the
existing remote distillation producer:

- Ideal production starts empty and emits one perfect pair every `4d` memory
  cycles. The saturation test uses an eight-pair buffer; the default is 1000.
- Loads and stores each consume one pair; coupled load/store consumes two.
  Failed accesses preserve credits and memory contents.
- A single extractor serializes accesses to the same block; independent blocks
  can access memory concurrently without a memory bus.
- Distilled EPR extractors contend across program blocks.
- Compute-side bus reservations still block memory accesses without consuming
  EPR credits.
- Production follows the memory clock and distillation retains its existing
  restart timing.

## test_operable.cpp

Tests the clock/frequency machinery in `sim/operable.h` / `sim/operable.cpp`.

- `test_compute_freq_khz` — `compute_freq_khz(period_ns) == 1e6 / period_ns`
  for several periods.
- `test_convert_cycles_between_frequencies` — cycle count scales by
  `f2/f1`; equal frequencies are the identity; slowing down (`f2 < f1`)
  rounds an integral result up (`ceil`); speeding up (`f2 > f1`) scales up;
  the floating-point instantiation applies no rounding.
- `test_convert_cycles_time_roundtrip` — `convert_cycles_to_time_ns` and
  `convert_time_ns_to_cycles` are exact inverses at 1 MHz, and a round trip
  cycles→ns→cycles reproduces the original count for several cycle counts
  (no over-provisioning on an exact number of cycles).
- `test_coordinate_clock_scale_cadence` — `coordinate_clock_scale` is
  observed indirectly through `tick()` cadence: at a 2:1 frequency ratio the
  slower operable advances one cycle every other tick, at 3:1 every third
  tick, and a lone operable (its own fastest) advances one cycle per tick.
- `test_fast_forward` — `fast_forward_all_operables_to_time_ns` sets each
  operable's current cycle to `convert_time_ns_to_cycles(target_ns, its freq)`.

## test_resource_estimation.cpp

Tests the pure `constexpr` physical-qubit-count formulas in
`sim/configuration/resource_estimation.{h,tpp}`.

- `test_surface_code_physical_qubit_count` — `surface_code_physical_qubit_count(dx, dz) == 2*(dx+1)*(dz+1)`,
  checked at compile time via `static_assert` and at runtime for several
  square (`d`) and asymmetric (`dx, dz`) cases, including symmetry under
  swapping `dx`/`dz` and that the single-arg overload matches `(d, d)`.
- `test_bivariate_bicycle_physical_qubit_count` — for the `[[n, k, d]]`
  bivariate-bicycle family (`d=6,12,18` → `n=72,144,288`), the qubit count
  is `2n + 50 << (d/6 - 1)`, checked at compile time and for all three sizes.
- `test_distillation_footprint_delta_vs_litinski` — cross-checks the
  codebase's `magic_state_distillation_physical_qubit_count` against a
  hand-derived reference formula from Litinski's 15-to-1 distillation paper
  (Quantum 3, 2019). The two models are structurally different (the paper
  formula is linear in `dx,dz,dm`; the codebase formula depends on
  input/output counts and surface-patch size and ignores `dm`), so the test
  only pins two anchor values (the paper's own `(7,3,3)` = 810 result, and
  the codebase's estimate for the same inputs) and prints the delta across a
  range of `(dx, dz, dm)` for visibility rather than asserting equality.

## test_routing.cpp

Tests the `Resource` interval-locking logic in `sim/routing.{h,cpp}`, which
tracks locked time intervals as half-open ranges `[a, b)` (so `[0,5)` and
`[5,10)` do not conflict).

- `test_is_lockable_half_open` — `is_lockable` allows adjacent ranges and
  rejects any range that shares a cycle with an existing lock (identical,
  partial overlap, fully contained, or fully containing).
- `test_lock_disjoint_out_of_order` — locking disjoint ranges in any order
  keeps them all independently queryable, and the gaps between them stay
  lockable; a range spanning two locks is rejected.
- `test_next_ready_cycle_basic` — `next_ready_cycle` returns the requested
  start when nothing is in the way, jumps past a lock that would overlap the
  requested duration, and returns the requested start unchanged when it's
  already past the lock.
- `test_next_ready_cycle_returns_lockable` — whatever cycle
  `next_ready_cycle` returns is actually lockable for the requested duration,
  checked across a table of `(current, duration)` cases against a resource
  with two existing locks.
- `test_next_ready_cycle_earliest_adjacent` — `next_ready_cycle` returns the
  earliest lockable cycle, including the half-open adjacency boundary
  (`current + duration <= lock_start` is the correct condition, not a strict
  `<`).
- `test_range_limit_eviction` — a `Resource` tracks at most `RANGE_LIMIT`
  (8) ranges; locking a 9th evicts the earliest, which then reads as
  lockable again.

## test_stall_monitor.cpp

Tests `StallMonitor<N,T>` (`sim/stall_monitor.{h,tpp}`), which records
active stall types over half-open cycle ranges `[start, end)` as a bitmask
(overlapping ranges of different types OR their flags together), and on
commit counts per-cycle stats: a cycle with exactly one active type is an
"isolated" stall for that type; a cycle with `>= 1` active type counts once
toward `cycles_with_stalls()`.

- `test_single_range_isolated` — a single range of one type is isolated for
  every cycle in it.
- `test_inclusive_flag` — the `inclusive` flag treats `[start, end]` as
  `[start, end+1)`.
- `test_empty_range_noop` — a range with `start >= end` is a no-op.
- `test_disjoint_ranges` — disjoint ranges of the same type accumulate
  isolated cycles; disjoint ranges of different types are each isolated
  independently and both count toward the total.
- `test_overlap_partial` — two ranges of different types that partially
  overlap produce isolated tails on each side and a non-isolated combined
  middle; every cycle in the union counts once toward the total.
- `test_overlap_subset` — a range fully contained inside another (different
  type) splits the outer range into two isolated tails around a combined
  middle; the inner type is never isolated.
- `test_full_overlap_all_types` — three types fully overlapping the same
  cycles produce zero isolated stalls for any type, but the cycles still
  count once toward the total.
- `test_stats_require_commit` — stats read as zero for buffered
  (uncommitted) ranges and only reflect reality after `commit_contents()`.
- `test_eviction_preserves_counts` — once more than `max_ranges` ranges are
  buffered, the earliest are committed (evicted) on the fly; eviction must
  not lose or double-count cycles across the eventual `commit_contents()`.
