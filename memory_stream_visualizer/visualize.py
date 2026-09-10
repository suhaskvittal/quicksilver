#!/usr/bin/env python3
# author: Claude Code
"""
Visualizes a `.qsmem` memory-stream trace produced by

    qs_memory_scheduler ... --memory-stream out.qsmem

By default, starts a local web server and opens a browser tab with an
interactive residency heatmap / metrics / epoch-detail view (see
`qsmem/server.py`, `qsmem/static/`). With `--dump`, instead prints a text
summary of the file to stdout -- useful for sanity-checking the format
without a browser (e.g. over SSH, or in a test).

See FORMAT.md in this directory for the on-disk layout.
"""

import argparse
import os
import sys
import webbrowser
from threading import Timer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from qsmem.reader import MemoryStream
from qsmem.server import serve


def dump_summary(path):
    with MemoryStream(path) as ms:
        agg = ms.global_aggregates()
        print(f"file                 : {path}")
        print(f"format version       : {ms.format_version}")
        print(f"scheduler            : {ms.scheduler_name} (id={ms.scheduler_id})")
        print(f"num_qubits           : {ms.num_qubits}")
        print(f"active_set_capacity  : {ms.active_set_capacity}")
        print(f"block_epoch_capacity : {ms.block_epoch_capacity}")
        print(f"block_count          : {ms.block_count()}")
        print(f"total_epochs         : {ms.total_epochs}")
        print(f"total_accesses       : {ms.total_accesses}")
        print(f"truncated (recovered): {ms.truncated}")
        print(f"initial_active_set   : {list(int(q) for q in ms.initial_active_set)}")
        if ms.total_epochs > 0:
            unrolled_inst_done = int(ms.epoch_record(ms.total_epochs - 1)["inst_done"])
            print(f"unrolled_inst_done (as of last epoch): {unrolled_inst_done}")
            compute_intensity = unrolled_inst_done / ms.total_accesses if ms.total_accesses else float("inf")
            print(f"compute_intensity    : {compute_intensity:.4f}")
        print(f"heat_mean / heat_std : {agg['heat_mean']:.4f} / {agg['heat_std']:.4f}")
        print(f"span_lengths (n)     : {len(agg['span_lengths'])}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace_file", help="path to a .qsmem file")
    parser.add_argument("--dump", action="store_true",
                        help="print a text summary instead of starting the server")
    parser.add_argument("--host", default="127.0.0.1", help="server bind address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="server port (default: 8765)")
    parser.add_argument("--no-browser", action="store_true", help="don't auto-open a browser tab")
    args = parser.parse_args()

    if args.dump:
        dump_summary(args.trace_file)
        return

    if not args.no_browser:
        Timer(0.5, lambda: webbrowser.open(f"http://{args.host}:{args.port}/")).start()
    serve(args.trace_file, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
