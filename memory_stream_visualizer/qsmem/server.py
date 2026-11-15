# author: Claude Code
"""
A small local-only HTTP server exposing a `.qsmem` memory stream to the
static browser UI in `qsmem/static/`. No third-party web framework is
used (none is installed in the repo venv) -- just `http.server` plus a
tiny JSON API.
"""

import json
import math
import mimetypes
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

from . import downsample
from .reader import MemoryStream

STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")

DEFAULT_MAX_COLUMNS = 1500
DEFAULT_TOP_QUBITS = 64


class QsmemRequestHandler(BaseHTTPRequestHandler):
    # set on the class by `serve()` before the server starts handling requests
    memory_stream: MemoryStream = None

    def log_message(self, fmt_str, *args):
        pass  # keep stdout quiet; errors still raise/print via BaseHTTPRequestHandler

    def _send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_error_json(self, status, message):
        self._send_json({"error": message}, status=status)

    def _send_static(self, rel_path):
        if rel_path in ("", "/"):
            rel_path = "index.html"
        elif rel_path.startswith("/static/"):
            rel_path = rel_path[len("/static/"):]
        rel_path = rel_path.lstrip("/")
        full_path = os.path.normpath(os.path.join(STATIC_DIR, rel_path))
        if not full_path.startswith(STATIC_DIR):
            self._send_error_json(403, "forbidden")
            return
        if not os.path.isfile(full_path):
            self._send_error_json(404, "not found")
            return

        ctype = mimetypes.guess_type(full_path)[0] or "application/octet-stream"
        with open(full_path, "rb") as f:
            body = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        try:
            if path == "/api/meta":
                self._handle_meta()
            elif path == "/api/window":
                self._handle_window(query)
            elif path == "/api/find_epoch":
                self._handle_find_epoch(query)
            elif path.startswith("/api/epoch/"):
                self._handle_epoch(path[len("/api/epoch/"):])
            elif path.startswith("/api/"):
                self._send_error_json(404, f"unknown endpoint {path}")
            else:
                self._send_static(path)
        except Exception as e:  # noqa: BLE001 -- surface any error to the client as JSON
            self._send_error_json(500, str(e))

    def _handle_meta(self):
        ms = self.memory_stream
        agg = ms.global_aggregates()
        self._send_json({
            "scheduler": ms.scheduler_name,
            "scheduler_id": ms.scheduler_id,
            "num_qubits": ms.num_qubits,
            "active_set_capacity": ms.active_set_capacity,
            "total_epochs": ms.total_epochs,
            "total_accesses": ms.total_accesses,
            "truncated": ms.truncated,
            "initial_active_set": [int(q) for q in ms.initial_active_set],
            "ever_resident_qubits": downsample.ever_resident_qubits(ms, agg),
            "access_counts": agg["access_counts"].tolist(),
            "unused_bandwidth_hist": agg["unused_bandwidth_hist"].tolist(),
            "span_lengths": agg["span_lengths"].tolist(),
            "heat_mean": agg["heat_mean"],
            "heat_std": agg["heat_std"],
        })

    def _handle_window(self, query):
        ms = self.memory_stream
        agg = ms.global_aggregates()

        def qint(name, default):
            return int(query[name][0]) if name in query else default

        start = qint("start", 0)
        count = qint("count", ms.total_epochs)
        columns = min(qint("columns", DEFAULT_MAX_COLUMNS), DEFAULT_MAX_COLUMNS)
        top = qint("top", DEFAULT_TOP_QUBITS)

        display_qubits = downsample.select_display_qubits(ms, agg, top if top > 0 else None)
        result = downsample.downsample_window(ms, start, count, columns, display_qubits)
        self._send_json(result)

    def _handle_find_epoch(self, query):
        ms = self.memory_stream
        if "inst_done" not in query:
            self._send_error_json(400, "missing required query param 'inst_done'")
            return
        try:
            target = int(query["inst_done"][0])
        except ValueError:
            self._send_error_json(400, f"invalid inst_done value {query['inst_done'][0]!r}")
            return

        epoch = ms.epoch_for_inst_done(target)
        if epoch is None:
            self._send_error_json(404, "trace has no epochs")
            return
        rec = ms.epoch_record(epoch)
        self._send_json({
            "epoch": epoch,
            "requested_inst_done": target,
            "epoch_inst_done": int(rec["inst_done"]),
        })

    def _handle_epoch(self, epoch_str):
        ms = self.memory_stream
        try:
            epoch = int(epoch_str)
        except ValueError:
            self._send_error_json(400, f"invalid epoch id {epoch_str!r}")
            return
        if epoch < 0 or epoch >= ms.total_epochs:
            self._send_error_json(404, f"epoch {epoch} out of range [0, {ms.total_epochs})")
            return

        rec = ms.epoch_record(epoch)
        loaded, evicted = ms.epoch_loaded_evicted(epoch)
        active = ms.active_set_after_epoch(epoch)
        cum_inst_done, cum_accesses, cum_intensity = ms.epoch_cumulative_stats(epoch)
        if not math.isfinite(cum_intensity):
            cum_intensity = None  # not valid strict JSON; JS side treats null as "n/a"
        self._send_json({
            "epoch": epoch,
            "total_epochs": ms.total_epochs,
            "inst_done": int(rec["inst_done"]),
            "compute_since_prev": int(rec["compute_since_prev"]),
            "memory_accesses": int(rec["access_count"]),
            "unused_bandwidth": int(rec["unused_bandwidth"]),
            "front_layer_width": int(rec["front_layer_width"]),
            "dag_inst_count": int(rec["dag_inst_count"]),
            "loaded": loaded,
            "evicted": evicted,
            "active_set": active,
            "cumulative_inst_done": cum_inst_done,
            "cumulative_memory_accesses": cum_accesses,
            "cumulative_compute_intensity": cum_intensity,
        })


def serve(path, host="127.0.0.1", port=8765):
    ms = MemoryStream(path)
    QsmemRequestHandler.memory_stream = ms
    httpd = ThreadingHTTPServer((host, port), QsmemRequestHandler)
    print(f"qsmem visualizer: serving {path!r} at http://{host}:{httpd.server_port}/  (Ctrl-C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
        ms.close()
