# author: Claude Code
"""
Buckets an arbitrary epoch range of a `.qsmem` trace down to a bounded
number of display columns, so the UI can pan/zoom over traces with far
more epochs than fit on screen without ever holding the whole trace in
the browser (this is the scaling problem the old inline-HTML tool had).

Per-qubit residency for a window is returned as a small
(#display_qubits x #columns) state grid rather than a full per-cell
heatmap over every qubit, since the payload should be bounded by what is
actually drawn, not by the trace size.
"""

import numpy as np

STATE_ABSENT = 0
STATE_RESIDENT = 1
STATE_LOADED = 2
STATE_EVICTED = 3


def ever_resident_qubits(ms, aggregates):
    """All qubits that were ever in the active set at some point in the trace."""
    ever = set(int(q) for q in ms.initial_active_set)
    ever.update(int(q) for q in np.nonzero(aggregates["load_counts"])[0])
    return sorted(ever)


def select_display_qubits(ms, aggregates, top):
    """
    The qubit ids to show on the heatmap's Y axis: the `top` qubits by
    total access count (load+evict), then re-sorted by qubit id for
    display -- matching the old tool's convention.
    """
    ever = ever_resident_qubits(ms, aggregates)
    if top is None or top >= len(ever):
        return ever
    access = aggregates["access_counts"]
    by_hotness = sorted(ever, key=lambda q: -int(access[q]))
    return sorted(by_hotness[:top])


def downsample_window(ms, start, count, columns, display_qubits):
    """
    Returns a dict of per-column arrays (metrics + a qubit residency state
    grid) covering epochs `[start, start+count)`, clamped to the trace,
    bucketed into at most `columns` columns.
    """
    total = ms.total_epochs
    start = max(0, min(start, total))
    end = max(start, min(start + count, total))
    n = end - start

    if n == 0:
        return {
            "start": start, "end": end, "bucket_size": 0, "num_buckets": 0,
            "epoch_first": [], "inst_done": [], "compute_since_prev": [],
            "mem_accesses": [], "unused_bw": [], "front_width": [], "dag_count": [],
            "qubits": display_qubits, "state": [[] for _ in display_qubits],
        }

    columns = max(1, min(columns, n))
    bucket_size = -(-n // columns)          # ceil(n / columns)
    num_buckets = -(-n // bucket_size)      # == columns, unless n < columns

    q_index = {q: i for i, q in enumerate(display_qubits)}
    nq = len(display_qubits)

    epoch_first = [-1] * num_buckets
    inst_done = [0] * num_buckets
    compute = [0] * num_buckets
    mem_accesses = [0] * num_buckets
    unused_bw = [0] * num_buckets
    front_width = [0] * num_buckets
    dag_count = [0] * num_buckets
    state = np.zeros((nq, num_buckets), dtype=np.uint8)

    for global_epoch, active in ms.active_sets_in_range(start, end):
        col = (global_epoch - start) // bucket_size
        if epoch_first[col] < 0:
            epoch_first[col] = global_epoch

        rec = ms.epoch_record(global_epoch)
        inst_done[col] = int(rec["inst_done"])  # cumulative: last epoch in bucket wins
        compute[col] += int(rec["compute_since_prev"])
        mem_accesses[col] += int(rec["access_count"])
        unused_bw[col] = max(unused_bw[col], int(rec["unused_bandwidth"]))
        front_width[col] = max(front_width[col], int(rec["front_layer_width"]))
        dag_count[col] = max(dag_count[col], int(rec["dag_inst_count"]))

        loaded, evicted = ms.epoch_loaded_evicted(global_epoch)
        for q in loaded:
            idx = q_index.get(q)
            if idx is not None:
                state[idx, col] = STATE_LOADED
        for q in evicted:
            idx = q_index.get(q)
            if idx is not None and state[idx, col] != STATE_LOADED:
                state[idx, col] = STATE_EVICTED
        for q in active:
            idx = q_index.get(q)
            if idx is not None and state[idx, col] == STATE_ABSENT:
                state[idx, col] = STATE_RESIDENT

    return {
        "start": start, "end": end, "bucket_size": bucket_size, "num_buckets": num_buckets,
        "epoch_first": epoch_first,
        "inst_done": inst_done,
        "compute_since_prev": compute,
        "mem_accesses": mem_accesses,
        "unused_bw": unused_bw,
        "front_width": front_width,
        "dag_count": dag_count,
        "qubits": display_qubits,
        "state": state.tolist(),
    }
