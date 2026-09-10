# author: Claude Code
"""
Reader for the `.qsmem` memory-stream format (see ../FORMAT.md).

Opening a file is O(index): we mmap it, parse the header, and either read
the footer + index (fast path) or recover block boundaries by scanning
forward for block magics (if the footer is missing/truncated, e.g. because
the writer process crashed). Per-block epoch/access arrays are exposed as
zero-copy numpy structured views and decoded lazily -- nothing beyond the
header/index is read until a caller asks for a specific block or range.

Whole-file aggregates (per-qubit access counts, residency-span lengths,
unused-bandwidth histogram, heat mean/stddev) necessarily touch every
record once; `global_aggregates()` does this in one vectorized pass and
should be called once and cached by the caller (the server does this).
"""

import bisect
import mmap
import os

import numpy as np

from . import format as fmt


class QsmemBlockMeta:
    __slots__ = ("file_offset", "first_epoch_index", "epoch_count",
                 "first_access_index", "access_count", "keyframe")

    def __init__(self, file_offset, first_epoch_index, epoch_count,
                 first_access_index, access_count, keyframe):
        self.file_offset = file_offset
        self.first_epoch_index = first_epoch_index
        self.epoch_count = epoch_count
        self.first_access_index = first_access_index
        self.access_count = access_count
        self.keyframe = keyframe  # np.uint32 array, unpadded (QSMEM_NONE stripped), sorted; may be None if unknown


class MemoryStream:
    def __init__(self, path):
        self.path = path
        self._file = open(path, "rb")
        size = os.fstat(self._file.fileno()).st_size
        if size == 0:
            raise fmt.QsmemFormatError(f"{path}: empty file")
        self._mmap = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        self._buf = memoryview(self._mmap)

        self._parse_header()
        self._blocks = []          # list[QsmemBlockMeta]
        self._block_first_epoch = []  # parallel list, for bisect
        self.truncated = False
        self._parse_index_or_recover()

        self._epoch_cache = {}     # block_idx -> structured ndarray (EPOCH_RECORD_DTYPE)
        self._access_cache = {}    # block_idx -> structured ndarray (ACCESS_RECORD_DTYPE)
        self._aggregates = None    # lazily computed, see global_aggregates()

    def close(self):
        # Drop our own cached views first. A caller may still be holding a
        # numpy scalar/array obtained from one of our accessors (e.g. the
        # last `rec = ms.epoch_record(i)` of a loop) -- that keeps an
        # export on the mmap's buffer alive, which would make
        # `mmap.close()` raise BufferError. We still attempt it (the
        # common case is nothing else is holding a reference by the time
        # the caller is done), but tolerate the error rather than crash:
        # the mmap and file will be reclaimed once every view is dropped.
        self._epoch_cache.clear()
        self._access_cache.clear()
        try:
            self._buf.release()
        except BufferError:
            pass
        try:
            self._mmap.close()
        except BufferError:
            pass
        self._file.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -----------------------------------------------------------------
    # Header
    # -----------------------------------------------------------------

    def _parse_header(self):
        buf = self._buf
        if len(buf) < fmt.HEADER_DTYPE.itemsize + 4:
            raise fmt.QsmemFormatError(f"{self.path}: file too short to contain a header")

        header = np.frombuffer(buf, dtype=fmt.HEADER_DTYPE, count=1, offset=0)[0]
        if bytes(header["magic"]) != fmt.HEADER_MAGIC:
            raise fmt.QsmemFormatError(f"{self.path}: bad header magic {bytes(header['magic'])!r}")
        if int(header["version"]) != fmt.FORMAT_VERSION:
            raise fmt.QsmemFormatError(
                f"{self.path}: unsupported format version {int(header['version'])} "
                f"(reader supports {fmt.FORMAT_VERSION})")

        self.format_version = int(header["version"])
        self.num_qubits = int(header["num_qubits"])
        self.active_set_capacity = int(header["active_set_capacity"])
        self.block_epoch_capacity = int(header["block_epoch_capacity"])
        self.scheduler_id = int(header["scheduler_id"])
        self.scheduler_name = bytes(header["scheduler_name"]).rstrip(b"\x00").decode("ascii", "replace")

        off = fmt.HEADER_DTYPE.itemsize
        n = int(np.frombuffer(buf, dtype="<u4", count=1, offset=off)[0])
        off += 4
        self.initial_active_set = np.frombuffer(buf, dtype="<u4", count=n, offset=off).copy()
        off += 4 * n

        header_size = int(header["header_size"])
        if header_size != off:
            raise fmt.QsmemFormatError(
                f"{self.path}: header_size field ({header_size}) does not match "
                f"computed header end ({off})")
        self._header_size = header_size

    # -----------------------------------------------------------------
    # Index (fast path) / forward-scan recovery (crash path)
    # -----------------------------------------------------------------

    def _parse_index_or_recover(self):
        size = len(self._buf)
        footer = None
        if size >= self._header_size + fmt.FOOTER_DTYPE.itemsize:
            cand = np.frombuffer(self._buf, dtype=fmt.FOOTER_DTYPE, count=1,
                                  offset=size - fmt.FOOTER_DTYPE.itemsize)[0]
            if bytes(cand["magic"]) == fmt.FOOTER_MAGIC:
                footer = cand

        if footer is not None:
            self._parse_index(footer)
        else:
            self.truncated = True
            self._recover_by_scanning()

        self.total_epochs = sum(b.epoch_count for b in self._blocks)
        self.total_accesses = sum(b.access_count for b in self._blocks)
        self._block_first_epoch = [b.first_epoch_index for b in self._blocks]

    def _parse_index(self, footer):
        block_count = int(footer["block_count"])
        index_offset = int(footer["index_offset"])
        entry_stride = fmt.INDEX_ENTRY_DTYPE.itemsize + 4 * self.active_set_capacity

        off = index_offset
        for _ in range(block_count):
            entry = np.frombuffer(self._buf, dtype=fmt.INDEX_ENTRY_DTYPE, count=1, offset=off)[0]
            kf_off = off + fmt.INDEX_ENTRY_DTYPE.itemsize
            keyframe_padded = np.frombuffer(self._buf, dtype="<u4",
                                             count=self.active_set_capacity, offset=kf_off)
            keyframe = np.sort(keyframe_padded[keyframe_padded != fmt.QSMEM_NONE])

            # recover access_count for this block from its own block header (cheap, fixed offset)
            bh = np.frombuffer(self._buf, dtype=fmt.BLOCK_HEADER_DTYPE, count=1,
                                offset=int(entry["block_file_offset"]))[0]
            if bytes(bh["magic"]) != fmt.BLOCK_MAGIC:
                raise fmt.QsmemFormatError(
                    f"{self.path}: index points at offset {int(entry['block_file_offset'])} "
                    f"which is not a block header")

            self._blocks.append(QsmemBlockMeta(
                file_offset=int(entry["block_file_offset"]),
                first_epoch_index=int(entry["first_epoch_index"]),
                epoch_count=int(entry["epoch_count"]),
                first_access_index=int(bh["first_access_index"]),
                access_count=int(bh["access_count"]),
                keyframe=keyframe,
            ))
            off += entry_stride

    def _recover_by_scanning(self):
        """
        No valid footer: walk the file from the end of the header, reading
        block headers back-to-back until we run out of complete blocks.
        Keyframes are not stored per-block in this path (only the index
        carries them), so they are reconstructed lazily by replaying from
        `initial_active_set` -- see `block_keyframe()`.
        """
        size = len(self._buf)
        off = self._header_size
        while off + fmt.BLOCK_HEADER_DTYPE.itemsize <= size:
            bh = np.frombuffer(self._buf, dtype=fmt.BLOCK_HEADER_DTYPE, count=1, offset=off)[0]
            if bytes(bh["magic"]) != fmt.BLOCK_MAGIC:
                break  # trailing index/footer bytes, or corruption -- stop here

            epoch_count = int(bh["epoch_count"])
            access_count = int(bh["access_count"])
            block_size = (fmt.BLOCK_HEADER_DTYPE.itemsize
                          + epoch_count * fmt.EPOCH_RECORD_DTYPE.itemsize
                          + access_count * fmt.ACCESS_RECORD_DTYPE.itemsize)
            if off + block_size > size:
                break  # partial trailing block from a mid-write crash -- discard it

            self._blocks.append(QsmemBlockMeta(
                file_offset=off,
                first_epoch_index=int(bh["first_epoch_index"]),
                epoch_count=epoch_count,
                first_access_index=int(bh["first_access_index"]),
                access_count=access_count,
                keyframe=None,  # filled in lazily, see block_keyframe()
            ))
            off += block_size

    # -----------------------------------------------------------------
    # Per-block record access (zero-copy, cached)
    # -----------------------------------------------------------------

    def block_count(self):
        return len(self._blocks)

    def block_meta(self, block_idx):
        return self._blocks[block_idx]

    def block_epochs(self, block_idx):
        if block_idx not in self._epoch_cache:
            b = self._blocks[block_idx]
            off = b.file_offset + fmt.BLOCK_HEADER_DTYPE.itemsize
            self._epoch_cache[block_idx] = np.frombuffer(
                self._buf, dtype=fmt.EPOCH_RECORD_DTYPE, count=b.epoch_count, offset=off)
        return self._epoch_cache[block_idx]

    def block_accesses(self, block_idx):
        if block_idx not in self._access_cache:
            b = self._blocks[block_idx]
            off = (b.file_offset + fmt.BLOCK_HEADER_DTYPE.itemsize
                   + b.epoch_count * fmt.EPOCH_RECORD_DTYPE.itemsize)
            self._access_cache[block_idx] = np.frombuffer(
                self._buf, dtype=fmt.ACCESS_RECORD_DTYPE, count=b.access_count, offset=off)
        return self._access_cache[block_idx]

    def block_keyframe(self, block_idx):
        """Sorted, unpadded active-set qubit ids entering `block_idx`."""
        b = self._blocks[block_idx]
        if b.keyframe is not None:
            return b.keyframe
        # Recovery path: replay every prior block's accesses from
        # `initial_active_set`. Cheap relative to the file size (each block
        # is visited once, in order, and cached).
        active = set(int(q) for q in self.initial_active_set)
        for i in range(block_idx):
            self._apply_block_accesses(active, self.block_accesses(i))
        b.keyframe = np.array(sorted(active), dtype=np.uint32)
        return b.keyframe

    @staticmethod
    def _apply_block_accesses(active_set, accesses):
        for rec in accesses:
            if rec["evict_qubit"] != fmt.QSMEM_NONE:
                active_set.discard(int(rec["evict_qubit"]))
            if rec["load_qubit"] != fmt.QSMEM_NONE:
                active_set.add(int(rec["load_qubit"]))

    # -----------------------------------------------------------------
    # Epoch-indexed queries
    # -----------------------------------------------------------------

    def block_for_epoch(self, epoch_idx):
        if epoch_idx < 0 or epoch_idx >= self.total_epochs:
            raise IndexError(f"epoch {epoch_idx} out of range [0, {self.total_epochs})")
        i = bisect.bisect_right(self._block_first_epoch, epoch_idx) - 1
        return i

    def epoch_for_inst_done(self, target_inst_done):
        """
        Returns the epoch index whose scheduling epoch was in effect at
        instruction count `target_inst_done` -- i.e. the last epoch whose
        `inst_done` (cumulative instructions retired BEFORE that epoch) is
        `<= target_inst_done`. Clamped to `[0, total_epochs - 1]`; returns
        `None` if the trace has no epochs at all.

        Relies on `inst_done` being non-decreasing and contiguous across
        epochs (epoch i+1's `inst_done` == epoch i's `inst_done +
        compute_since_prev`), which the writer guarantees.
        """
        if self.total_epochs == 0:
            return None
        target = max(0, int(target_inst_done))

        def block_first_inst_done(bi):
            return int(self.block_epochs(bi)[0]["inst_done"])

        lo, hi, bi = 0, len(self._blocks) - 1, 0
        while lo <= hi:
            mid = (lo + hi) // 2
            if block_first_inst_done(mid) <= target:
                bi = mid
                lo = mid + 1
            else:
                hi = mid - 1

        inst_done_arr = self.block_epochs(bi)["inst_done"]
        local = int(np.searchsorted(inst_done_arr, target, side="right")) - 1
        local = max(0, local)
        return min(self._blocks[bi].first_epoch_index + local, self.total_epochs - 1)

    def epoch_record(self, epoch_idx):
        bi = self.block_for_epoch(epoch_idx)
        b = self._blocks[bi]
        local = epoch_idx - b.first_epoch_index
        return self.block_epochs(bi)[local]

    def epoch_cumulative_stats(self, epoch_idx):
        """
        Returns (cumulative_inst_done, cumulative_memory_accesses,
        cumulative_compute_intensity) as of the END of `epoch_idx` --
        i.e. including this epoch's own compute and accesses. O(1): both
        `inst_done` and `access_begin` are already cumulative-before-this-
        epoch counters, so no scan is needed.
        """
        rec = self.epoch_record(epoch_idx)
        cum_inst_done = int(rec["inst_done"]) + int(rec["compute_since_prev"])
        cum_accesses = int(rec["access_begin"]) + int(rec["access_count"])
        intensity = cum_inst_done / cum_accesses if cum_accesses > 0 else float("inf")
        return cum_inst_done, cum_accesses, intensity

    def epoch_loaded_evicted(self, epoch_idx):
        """Returns (loaded: list[int], evicted: list[int]) for one epoch."""
        bi = self.block_for_epoch(epoch_idx)
        b = self._blocks[bi]
        local = epoch_idx - b.first_epoch_index
        rec = self.block_epochs(bi)[local]
        begin = int(rec["access_begin"]) - b.first_access_index
        count = int(rec["access_count"])
        accs = self.block_accesses(bi)[begin:begin + count]
        loaded = [int(q) for q in accs["load_qubit"] if q != fmt.QSMEM_NONE]
        evicted = [int(q) for q in accs["evict_qubit"] if q != fmt.QSMEM_NONE]
        return loaded, evicted

    def active_set_after_epoch(self, epoch_idx):
        """Full active set (as a sorted list) immediately after `epoch_idx`."""
        bi = self.block_for_epoch(epoch_idx)
        b = self._blocks[bi]
        active = set(int(q) for q in self.block_keyframe(bi))
        epochs = self.block_epochs(bi)
        accesses = self.block_accesses(bi)
        local_end = epoch_idx - b.first_epoch_index
        for i in range(local_end + 1):
            rec = epochs[i]
            begin = int(rec["access_begin"]) - b.first_access_index
            count = int(rec["access_count"])
            self._apply_block_accesses(active, accesses[begin:begin + count])
        return sorted(active)

    def active_sets_in_range(self, start_epoch, end_epoch):
        """
        Yields the post-epoch active set (as a python set) for every epoch
        in `[start_epoch, end_epoch)`, replaying block-by-block. Used by
        `downsample.py` to build residency for an arbitrary window without
        touching blocks outside it.
        """
        if start_epoch >= end_epoch:
            return
        bi = self.block_for_epoch(start_epoch)
        active = set(int(q) for q in self.block_keyframe(bi))
        epoch = self._blocks[bi].first_epoch_index
        # fast-forward within the first block to `start_epoch`
        while epoch < start_epoch:
            b = self._blocks[bi]
            epochs = self.block_epochs(bi)
            accesses = self.block_accesses(bi)
            local = epoch - b.first_epoch_index
            rec = epochs[local]
            begin = int(rec["access_begin"]) - b.first_access_index
            count = int(rec["access_count"])
            self._apply_block_accesses(active, accesses[begin:begin + count])
            epoch += 1

        while epoch < end_epoch:
            bi = self.block_for_epoch(epoch)
            b = self._blocks[bi]
            epochs = self.block_epochs(bi)
            accesses = self.block_accesses(bi)
            local = epoch - b.first_epoch_index
            rec = epochs[local]
            begin = int(rec["access_begin"]) - b.first_access_index
            count = int(rec["access_count"])
            self._apply_block_accesses(active, accesses[begin:begin + count])
            yield epoch, frozenset(active)
            epoch += 1

    # -----------------------------------------------------------------
    # Whole-file aggregates (O(file), vectorized, meant to be cached by caller)
    # -----------------------------------------------------------------

    def global_aggregates(self):
        if self._aggregates is not None:
            return self._aggregates

        load_counts = np.zeros(self.num_qubits, dtype=np.int64)
        evict_counts = np.zeros(self.num_qubits, dtype=np.int64)
        unused_bw_hist = np.zeros(self.active_set_capacity + 1, dtype=np.int64)

        compute_all = []
        mem_all = []

        # exact residency-span replay across block boundaries
        entered_at = {int(q): 0 for q in self.initial_active_set}
        span_lengths = []

        epoch_cursor = 0
        for bi in range(len(self._blocks)):
            epochs = self.block_epochs(bi)
            accesses = self.block_accesses(bi)

            loads = accesses["load_qubit"]
            evicts = accesses["evict_qubit"]
            valid_loads = loads[loads != fmt.QSMEM_NONE]
            valid_evicts = evicts[evicts != fmt.QSMEM_NONE]
            if valid_loads.size:
                load_counts += np.bincount(valid_loads, minlength=self.num_qubits)[:self.num_qubits]
            if valid_evicts.size:
                evict_counts += np.bincount(valid_evicts, minlength=self.num_qubits)[:self.num_qubits]

            ubw = np.clip(epochs["unused_bandwidth"], 0, self.active_set_capacity)
            unused_bw_hist += np.bincount(ubw, minlength=self.active_set_capacity + 1)[:self.active_set_capacity + 1]

            compute_all.append(epochs["compute_since_prev"].astype(np.float64))
            mem_all.append(epochs["access_count"].astype(np.float64))

            # span replay: walk per-epoch within this block (accesses per
            # epoch are typically few; this is the same cost as building
            # the heatmap and only happens once per aggregates() call)
            first_access_idx = self._blocks[bi].first_access_index
            for rec in epochs:
                begin = int(rec["access_begin"]) - first_access_idx
                count = int(rec["access_count"])
                for rec2 in accesses[begin:begin + count]:
                    ev = int(rec2["evict_qubit"])
                    ld = int(rec2["load_qubit"])
                    if ev != fmt.QSMEM_NONE and ev in entered_at:
                        span_lengths.append(epoch_cursor - entered_at.pop(ev) + 1)
                    if ld != fmt.QSMEM_NONE:
                        entered_at[ld] = epoch_cursor
                epoch_cursor += 1

        for q, start in entered_at.items():
            span_lengths.append(self.total_epochs - start)

        compute = np.concatenate(compute_all) if compute_all else np.zeros(0)
        mem = np.concatenate(mem_all) if mem_all else np.zeros(0)
        heat = mem / np.maximum(1.0, compute)

        self._aggregates = {
            "load_counts": load_counts,
            "evict_counts": evict_counts,
            "access_counts": load_counts + evict_counts,
            "unused_bandwidth_hist": unused_bw_hist,
            "span_lengths": np.array(span_lengths, dtype=np.int64),
            "heat_mean": float(heat.mean()) if heat.size else 0.0,
            "heat_std": float(heat.std()) if heat.size else 0.0,
        }
        return self._aggregates
