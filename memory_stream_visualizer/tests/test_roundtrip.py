# author: Claude Code
"""
Round-trip tests for the `.qsmem` reader against a synthetic file built
directly from the FORMAT.md spec (independent of the C++ writer, so these
can run without building the C++ side). This validates the reader/
downsample logic; the C++ writer itself is cross-checked against a real
`qs_memory_scheduler` run separately (see the repo's verification notes).
"""

import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from qsmem import downsample
from qsmem.reader import MemoryStream

HEADER_FMT = "<8sHHIIIII16s"
BLOCK_HEADER_FMT = "<4sIQQQ"
EPOCH_FMT = "<QQIIIIII"
ACCESS_FMT = "<II"
INDEX_ENTRY_FMT = "<QQII"
FOOTER_FMT = "<QQQQ8s"

NONE = 0xFFFFFFFF


class SyntheticTrace:
    """
    A tiny, fully deterministic memory-scheduler trace used to test the
    reader. `epochs` is a list of dicts:
        {"loads": [...], "evicts": [...], "compute": int,
         "unused_bw": int, "front_width": int, "dag_count": int}
    where `loads[i]`/`evicts[i]` pair up (equal length) as the swaps for
    that epoch, matching how `transform_active_set` emits them.
    """
    def __init__(self, num_qubits, capacity, initial_active_set, epochs,
                 scheduler_id=1, scheduler_name="HINT", block_epoch_capacity=4096):
        self.num_qubits = num_qubits
        self.capacity = capacity
        self.initial_active_set = sorted(initial_active_set)
        self.epochs = epochs
        self.scheduler_id = scheduler_id
        self.scheduler_name = scheduler_name
        self.block_epoch_capacity = block_epoch_capacity

    def expected_active_set_after(self, epoch_idx):
        active = set(self.initial_active_set)
        for e in self.epochs[:epoch_idx + 1]:
            for ev in e["evicts"]:
                active.discard(ev)
            for ld in e["loads"]:
                active.add(ld)
        return sorted(active)


def write_synthetic_qsmem(path, trace: SyntheticTrace, include_footer=True):
    with open(path, "wb") as f:
        name_bytes = trace.scheduler_name.encode("ascii")[:16].ljust(16, b"\x00")
        initial = trace.initial_active_set
        header_size = struct.calcsize(HEADER_FMT) + 4 + 4 * len(initial)

        f.write(struct.pack(HEADER_FMT, b"QSMEMSTR", 1, 0, header_size,
                             trace.num_qubits, trace.capacity, trace.block_epoch_capacity,
                             trace.scheduler_id, name_bytes))
        f.write(struct.pack("<I", len(initial)))
        for q in initial:
            f.write(struct.pack("<I", q))

        index_entries = []  # (file_offset, first_epoch_index, epoch_count, keyframe)
        block_start_active = list(initial)
        inst_done = 0
        access_global_idx = 0
        epoch_idx = 0

        pending_epochs = []
        pending_accesses = []
        block_first_epoch = 0
        block_first_access = 0

        def flush_block():
            nonlocal pending_epochs, pending_accesses, block_first_epoch, block_first_access, block_start_active
            if not pending_epochs:
                return
            offset = f.tell()
            f.write(struct.pack(BLOCK_HEADER_FMT, b"QSMB", len(pending_epochs),
                                 block_first_epoch, len(pending_accesses), block_first_access))
            for rec in pending_epochs:
                f.write(struct.pack(EPOCH_FMT, *rec))
            for load, evict in pending_accesses:
                f.write(struct.pack(ACCESS_FMT, load, evict))

            kf = sorted(block_start_active) + [NONE] * (trace.capacity - len(block_start_active))
            index_entries.append((offset, block_first_epoch, len(pending_epochs), kf))

            block_first_epoch += len(pending_epochs)
            block_first_access += len(pending_accesses)
            pending_epochs = []
            pending_accesses = []

        active = set(initial)
        for e in trace.epochs:
            begin = access_global_idx
            for ld, ev in zip(e["loads"], e["evicts"]):
                pending_accesses.append((ld if ld is not None else NONE,
                                          ev if ev is not None else NONE))
                access_global_idx += 1
            count = access_global_idx - begin

            pending_epochs.append((inst_done, begin, count, e["compute"],
                                    e["unused_bw"], e["front_width"], e["dag_count"], 0))
            inst_done += e["compute"]

            for ev in e["evicts"]:
                active.discard(ev)
            for ld in e["loads"]:
                active.add(ld)

            epoch_idx += 1
            if len(pending_epochs) >= trace.block_epoch_capacity:
                flush_block()
                block_start_active = sorted(active)

        flush_block()

        index_offset = f.tell()
        if include_footer:
            for offset, first_epoch, epoch_count, kf in index_entries:
                f.write(struct.pack(INDEX_ENTRY_FMT, offset, first_epoch, epoch_count, 0))
                for q in kf:
                    f.write(struct.pack("<I", q))
            f.write(struct.pack(FOOTER_FMT, index_offset, len(index_entries),
                                 len(trace.epochs), access_global_idx, b"QSMEMEND"))


def make_small_trace(n_epochs=20, capacity=4, block_epoch_capacity=None):
    initial = [0, 1, 2, 3]
    epochs = []
    next_q = 4
    for i in range(n_epochs):
        # Sliding window of size `capacity`: epoch i evicts the qubit that
        # entered the active set exactly `capacity` epochs ago (or, for the
        # first `capacity` epochs, one of the original `initial` qubits),
        # so `evict_q` is always currently resident.
        evict_q = i
        epochs.append({
            "loads": [next_q], "evicts": [evict_q],
            "compute": 10 + i, "unused_bw": 0, "front_width": 1 + (i % 3), "dag_count": 100 + i,
        })
        next_q += 1
    kwargs = {}
    if block_epoch_capacity is not None:
        kwargs["block_epoch_capacity"] = block_epoch_capacity
    return SyntheticTrace(num_qubits=next_q, capacity=capacity, initial_active_set=initial,
                           epochs=epochs, **kwargs)


class RoundtripTests(unittest.TestCase):
    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self.tmp_path = self._tmpdir.name

    def tearDown(self):
        self._tmpdir.cleanup()

    def path(self, name):
        return os.path.join(self.tmp_path, name)

    def test_roundtrip_basic(self):
        trace = make_small_trace(n_epochs=20, capacity=4)
        path = self.path("basic.qsmem")
        write_synthetic_qsmem(path, trace)

        with MemoryStream(path) as ms:
            self.assertEqual(ms.format_version, 1)
            self.assertEqual(ms.num_qubits, trace.num_qubits)
            self.assertEqual(ms.active_set_capacity, trace.capacity)
            self.assertEqual(ms.scheduler_name, "HINT")
            self.assertEqual(ms.total_epochs, len(trace.epochs))
            self.assertEqual(ms.total_accesses, len(trace.epochs))  # one access per epoch here
            self.assertFalse(ms.truncated)
            self.assertEqual(sorted(int(q) for q in ms.initial_active_set), trace.initial_active_set)

            for i in range(len(trace.epochs)):
                rec = ms.epoch_record(i)
                self.assertEqual(int(rec["compute_since_prev"]), trace.epochs[i]["compute"])
                loaded, evicted = ms.epoch_loaded_evicted(i)
                self.assertEqual(loaded, trace.epochs[i]["loads"])
                self.assertEqual(evicted, trace.epochs[i]["evicts"])
                self.assertEqual(ms.active_set_after_epoch(i), trace.expected_active_set_after(i))

    def test_roundtrip_multi_block(self):
        # force multiple blocks with a tiny per-block capacity
        trace = make_small_trace(n_epochs=25, capacity=4, block_epoch_capacity=7)
        path = self.path("multiblock.qsmem")
        write_synthetic_qsmem(path, trace)

        with MemoryStream(path) as ms:
            self.assertEqual(ms.block_count(), 4)  # ceil(25/7)
            self.assertEqual(ms.total_epochs, 25)
            for i in (0, 6, 7, 13, 14, 24):
                self.assertEqual(ms.active_set_after_epoch(i), trace.expected_active_set_after(i))

            # active_sets_in_range should agree with active_set_after_epoch epoch-by-epoch
            got = dict(ms.active_sets_in_range(5, 20))
            for i in range(5, 20):
                self.assertEqual(sorted(got[i]), trace.expected_active_set_after(i))

    def test_truncated_file_recovers(self):
        trace = make_small_trace(n_epochs=25, capacity=4, block_epoch_capacity=7)
        path = self.path("truncated.qsmem")
        write_synthetic_qsmem(path, trace, include_footer=False)

        with MemoryStream(path) as ms:
            self.assertTrue(ms.truncated)
            self.assertEqual(ms.total_epochs, 25)
            self.assertEqual(ms.block_count(), 4)
            for i in (0, 6, 13, 24):
                self.assertEqual(ms.active_set_after_epoch(i), trace.expected_active_set_after(i))

    def test_zero_epoch_file(self):
        trace = SyntheticTrace(num_qubits=8, capacity=4, initial_active_set=[0, 1, 2, 3], epochs=[])
        path = self.path("empty.qsmem")
        write_synthetic_qsmem(path, trace)

        with MemoryStream(path) as ms:
            self.assertEqual(ms.total_epochs, 0)
            self.assertEqual(ms.total_accesses, 0)
            self.assertEqual(ms.block_count(), 0)

    def test_global_aggregates_and_downsample(self):
        trace = make_small_trace(n_epochs=30, capacity=4, block_epoch_capacity=10)
        path = self.path("agg.qsmem")
        write_synthetic_qsmem(path, trace)

        with MemoryStream(path) as ms:
            agg = ms.global_aggregates()
            # every epoch here does exactly one load and one evict
            self.assertEqual(agg["load_counts"].sum(), len(trace.epochs))
            self.assertEqual(agg["evict_counts"].sum(), len(trace.epochs))
            self.assertGreater(len(agg["span_lengths"]), 0)

            display_qubits = downsample.select_display_qubits(ms, agg, top=8)
            win = downsample.downsample_window(ms, 0, ms.total_epochs, columns=10,
                                                display_qubits=display_qubits)
            self.assertLessEqual(win["num_buckets"], 10)
            self.assertEqual(sum(win["mem_accesses"]), ms.total_accesses)
            self.assertEqual(len(win["state"]), len(display_qubits))

    def test_epoch_for_inst_done_and_cumulative_stats(self):
        trace = make_small_trace(n_epochs=25, capacity=4, block_epoch_capacity=7)
        path = self.path("goto.qsmem")
        write_synthetic_qsmem(path, trace)

        # prefix sums of `compute` mirror what the writer records as each
        # epoch's `inst_done` (cumulative BEFORE that epoch)
        computes = [e["compute"] for e in trace.epochs]
        inst_done_before = [0] * len(computes)
        for i in range(1, len(computes)):
            inst_done_before[i] = inst_done_before[i - 1] + computes[i - 1]

        with MemoryStream(path) as ms:
            # exact epoch boundaries and interior points should resolve to that epoch
            for i in (0, 6, 7, 13, 24):
                lo = inst_done_before[i]
                hi = lo + computes[i] - 1
                self.assertEqual(ms.epoch_for_inst_done(lo), i)
                self.assertEqual(ms.epoch_for_inst_done(hi), i)

            # before the trace starts / past the end clamp to the first/last epoch
            self.assertEqual(ms.epoch_for_inst_done(-100), 0)
            self.assertEqual(ms.epoch_for_inst_done(10_000_000), len(trace.epochs) - 1)

            # cumulative stats: one access per epoch here, so cumulative
            # accesses through epoch i is simply i+1
            for i in (0, 6, 13, 24):
                cum_inst_done, cum_accesses, intensity = ms.epoch_cumulative_stats(i)
                self.assertEqual(cum_inst_done, inst_done_before[i] + computes[i])
                self.assertEqual(cum_accesses, i + 1)
                self.assertAlmostEqual(intensity, cum_inst_done / cum_accesses)


if __name__ == "__main__":
    unittest.main()
