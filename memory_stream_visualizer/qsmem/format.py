# author: Claude Code
"""
Binary layout constants and numpy structured dtypes for the `.qsmem`
memory-stream format. This module mirrors `FORMAT.md` in the parent
directory and the C++ definitions in
`src/compiler/pass/memory_scheduler/memory_stream.h` -- if you change one,
change all three.
"""

import numpy as np

QSMEM_NONE = np.uint32(0xFFFFFFFF)

HEADER_MAGIC = b"QSMEMSTR"
BLOCK_MAGIC = b"QSMB"
FOOTER_MAGIC = b"QSMEMEND"

FORMAT_VERSION = 1

SCHEDULER_NAMES = {0: "EIF", 1: "HINT"}

# ---------------------------------------------------------------------------
# All dtypes are explicitly little-endian ('<') and packed (no numpy padding
# is introduced as long as field sizes divide evenly, which they do here).
# ---------------------------------------------------------------------------

HEADER_DTYPE = np.dtype([
    ("magic", "S8"),
    ("version", "<u2"),
    ("flags", "<u2"),
    ("header_size", "<u4"),
    ("num_qubits", "<u4"),
    ("active_set_capacity", "<u4"),
    ("block_epoch_capacity", "<u4"),
    ("scheduler_id", "<u4"),
    ("scheduler_name", "S16"),
])

BLOCK_HEADER_DTYPE = np.dtype([
    ("magic", "S4"),
    ("epoch_count", "<u4"),
    ("first_epoch_index", "<u8"),
    ("access_count", "<u8"),
    ("first_access_index", "<u8"),
])

EPOCH_RECORD_DTYPE = np.dtype([
    ("inst_done", "<u8"),
    ("access_begin", "<u8"),
    ("access_count", "<u4"),
    ("compute_since_prev", "<u4"),
    ("unused_bandwidth", "<u4"),
    ("front_layer_width", "<u4"),
    ("dag_inst_count", "<u4"),
    ("reserved", "<u4"),
])

ACCESS_RECORD_DTYPE = np.dtype([
    ("load_qubit", "<u4"),
    ("evict_qubit", "<u4"),
])

INDEX_ENTRY_DTYPE = np.dtype([
    ("block_file_offset", "<u8"),
    ("first_epoch_index", "<u8"),
    ("epoch_count", "<u4"),
    ("reserved", "<u4"),
])

FOOTER_DTYPE = np.dtype([
    ("index_offset", "<u8"),
    ("block_count", "<u8"),
    ("total_epochs", "<u8"),
    ("total_accesses", "<u8"),
    ("magic", "S8"),
])

assert HEADER_DTYPE.itemsize == 48
assert BLOCK_HEADER_DTYPE.itemsize == 32
assert EPOCH_RECORD_DTYPE.itemsize == 40
assert ACCESS_RECORD_DTYPE.itemsize == 8
assert INDEX_ENTRY_DTYPE.itemsize == 24
assert FOOTER_DTYPE.itemsize == 40


class QsmemFormatError(Exception):
    """Raised when a `.qsmem` file fails to validate (bad magic/version/size)."""
