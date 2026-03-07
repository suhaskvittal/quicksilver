# author: Claude Code

import math
import os
import re
from typing import Any

##############################################
##############################################

'''
    A "stat line" is any line that could belong to the stats block at the
    bottom of an output file. The two properties that distinguish stat lines
    from progress-log lines are:
      1. They begin with an uppercase letter (possibly preceded by whitespace
         for group members).
      2. They do NOT contain an '=' character (progress lines use "key = val").
'''
_STAT_LINE_RE = re.compile(r'^\s*[A-Z]')

def _is_stat_line(line: str) -> bool:
    stripped = line.rstrip()
    if not stripped:
        return True     # blank lines are fine inside a stats block
    if '=' in stripped:
        return False
    return bool(_STAT_LINE_RE.match(stripped))

##############################################
##############################################

def _parse_value(s: str) -> Any:
    '''
    Try to interpret a raw string value as int → float → str, in that order.
    Handles 'nan' / 'inf' transparently via float().
    '''
    try:
        return int(s)
    except ValueError:
        pass
    try:
        return float(s)
    except ValueError:
        pass
    return s

##############################################
##############################################

def _find_stats_start(lines: list[str]) -> int:
    '''
    Returns the index of the first line of the stats block by scanning
    backwards from the end of the file.  This is robust to any progress
    output that might share superficial similarities with stat lines.
    '''
    i = len(lines) - 1
    # skip trailing blank lines
    while i >= 0 and not lines[i].strip():
        i -= 1
    # walk backwards through stat-looking lines
    while i >= 0 and _is_stat_line(lines[i]):
        i -= 1
    return i + 1

##############################################
##############################################

def parse_stats_file(filepath: str) -> dict:
    '''
    Parse a single .out file and return a dictionary of statistics.

    Top-level stats are stored as flat key → value pairs.

    When a line has no value of its own and is followed by indented lines,
    it is treated as a group header: its stats are collected into a nested
    dictionary keyed by the full header text (e.g. "CLIENT 0", "L1_FACTORY").

    Example output structure:
        {
            "INST_DONE": 168200877,
            "COMPUTE_INTENSITY": 14.008,
            "CLIENT 0": {
                "IPC": 0.014,
                "INSTRUCTIONS": 100000054,
                ...
            },
            "L1_FACTORY": { ... },
            ...
        }
    '''
    with open(filepath) as f:
        lines = f.readlines()

    start = _find_stats_start(lines)
    stat_lines = lines[start:]

    stats: dict = {}
    current_group: str | None = None

    i = 0
    while i < len(stat_lines):
        line = stat_lines[i].rstrip()

        if not line:
            i += 1
            continue

        is_indented = (line != line.lstrip())

        if is_indented:
            # ── group member ────────────────────────────────────────────────
            if current_group is not None:
                parts = line.split()
                if len(parts) >= 2:
                    key = parts[0]
                    value = _parse_value(' '.join(parts[1:]))
                    stats[current_group][key] = value
                elif len(parts) == 1:
                    stats[current_group][parts[0]] = None
        else:
            # ── top-level line: group header OR plain stat ───────────────────
            # Peek ahead to the next non-empty line to decide which it is.
            j = i + 1
            while j < len(stat_lines) and not stat_lines[j].strip():
                j += 1
            next_is_indented = (
                j < len(stat_lines)
                and stat_lines[j].strip()
                and stat_lines[j] != stat_lines[j].lstrip()
            )

            parts = line.split()
            if next_is_indented:
                # group header: use the full stripped line as the key
                current_group = line.strip()
                stats[current_group] = {}
            else:
                current_group = None
                if len(parts) >= 2:
                    key = parts[0]
                    value = _parse_value(' '.join(parts[1:]))
                    stats[key] = value
                elif len(parts) == 1:
                    stats[parts[0]] = None

        i += 1

    return stats

##############################################
##############################################

def aggregate_stats_by_policy(folder_path: str) -> dict:
    '''
    Read every .out file inside the given folder path and return a dictionary
    mapping workload names → statistics dictionaries.

    The workload name is derived the same way as get_workload_name() in
    common.py: strip the file extension from the basename.  For a file
    named "BQ_e_cr2_120_trotter.rpc.out" this yields "BQ_e_cr2_120_trotter.rpc".

    Example:
        data = aggregate_stats_by_policy("out/hint_isca2026/compiler_results/eif_a12")
        # data["BQ_e_cr2_120_trotter.rpc"]["INST_DONE"]  →  168200877
        # data["BQ_e_cr2_120_trotter.rpc"]["CLIENT 0"]["IPC"]  →  0.014
    '''
    result: dict = {}

    for filename in sorted(os.listdir(folder_path)):
        if not filename.endswith('.out'):
            continue
        workload_name = os.path.splitext(filename)[0]
        filepath = os.path.join(folder_path, filename)
        result[workload_name] = parse_stats_file(filepath)

    return result

##############################################
##############################################
