"""
 *  author: Suhas Vittal
 *  date:   12 February 2026
 *
 *  Scans a HamLib sub-directory on the NERSC portal and lists all
 *  Hamiltonians exceeding optional qubit and term count thresholds.
 *
 *  Usage:
 *      python hamlib_scan_ftp_server.py <sub-directory> [--min-qubits N] [--min-terms N]
 *
 *  Example:
 *      python hamlib_scan_ftp_server.py chemistry/electronic/standard --min-qubits 60 --min-terms 500000
 * """

import argparse
import csv
import io
import sys

import requests

############################################################
############################################################

HAMLIB_BASE_URL    = "https://portal.nersc.gov/cfs/m888/dcamps/hamlib"
DEFAULT_MIN_QUBITS = 50
DEFAULT_MIN_TERMS  = 0

############################################################
############################################################

def _csv_url_from_subdir(subdir: str) -> str:
    """
    Derives the CSV summary filename from the sub-directory path.
    HamLib names its CSV files by joining path components with underscores.
    e.g. chemistry/electronic/standard -> chemistry_electronic_standard.csv
    """
    parts = [p for p in subdir.strip("/").split("/") if p]
    filename = "_".join(parts) + ".csv"
    return f"{HAMLIB_BASE_URL}/{subdir.strip('/')}/{filename}"


def _fetch_csv(url: str) -> list[dict]:
    """
    Downloads and parses the CSV at `url`. Returns a list of row dicts.
    Raises on HTTP errors.
    """
    response = requests.get(url, timeout=30)
    response.raise_for_status()
    reader = csv.DictReader(io.StringIO(response.text))
    return list(reader)


def _filter_hamiltonians(rows: list[dict], min_qubits: int, min_terms: int) -> list[dict]:
    """
    Returns rows where 'nqubits' > min_qubits AND 'terms' > min_terms.
    """
    result = []
    for row in rows:
        try:
            if int(row["nqubits"]) > min_qubits and int(row["terms"]) > min_terms:
                result.append(row)
        except (ValueError, KeyError):
            pass
    return result


def _print_results(rows: list[dict], subdir: str, min_qubits: int, min_terms: int) -> None:
    filters = []
    if min_qubits > 0:
        filters.append(f"qubits > {min_qubits}")
    if min_terms > 0:
        filters.append(f"terms > {min_terms:,}")
    filter_str = " and ".join(filters) if filters else "no filters"

    if not rows:
        print(f"No Hamiltonians matching ({filter_str}) found in '{subdir}'.")
        return

    # Compute column widths for aligned output.
    col_file    = max(len(r["File"])    for r in rows)
    col_dataset = max(len(r["Dataset"]) for r in rows)
    col_qubits  = max(len(r["nqubits"]) for r in rows)
    col_terms   = max(len(r["terms"])   for r in rows)

    col_file    = max(col_file,    len("File"))
    col_dataset = max(col_dataset, len("Dataset"))
    col_qubits  = max(col_qubits,  len("nqubits"))
    col_terms   = max(col_terms,   len("terms"))

    header = (f"{'File':<{col_file}}  "
              f"{'Dataset':<{col_dataset}}  "
              f"{'nqubits':>{col_qubits}}  "
              f"{'terms':>{col_terms}}")
    separator = "-" * len(header)

    print(f"\nHamiltonians matching ({filter_str}) in '{subdir}':")
    print(separator)
    print(header)
    print(separator)
    for r in rows:
        print(f"{r['File']:<{col_file}}  "
              f"{r['Dataset']:<{col_dataset}}  "
              f"{int(r['nqubits']):>{col_qubits}}  "
              f"{int(r['terms']):>{col_terms},}")
    print(separator)
    print(f"Total: {len(rows)} Hamiltonian(s)")

############################################################
############################################################

def main() -> None:
    parser = argparse.ArgumentParser(
        description="List HamLib Hamiltonians in a sub-directory matching qubit/term thresholds."
    )
    parser.add_argument(
        "subdir",
        help='HamLib sub-directory (e.g. "chemistry/electronic/standard")',
    )
    parser.add_argument(
        "--min-qubits",
        type=int,
        default=DEFAULT_MIN_QUBITS,
        metavar="N",
        help=f"minimum qubit count (exclusive, default: {DEFAULT_MIN_QUBITS})",
    )
    parser.add_argument(
        "--min-terms",
        type=int,
        default=DEFAULT_MIN_TERMS,
        metavar="N",
        help=f"minimum term count (exclusive, default: {DEFAULT_MIN_TERMS})",
    )
    args = parser.parse_args()

    csv_url = _csv_url_from_subdir(args.subdir)
    print(f"Fetching CSV: {csv_url}")
    try:
        rows = _fetch_csv(csv_url)
    except requests.HTTPError as e:
        print(f"Error: could not fetch CSV ({e})")
        print(f"Check that '{args.subdir}' is a valid HamLib sub-directory.")
        sys.exit(1)
    except requests.RequestException as e:
        print(f"Network error: {e}")
        sys.exit(1)

    filtered = _filter_hamiltonians(rows, args.min_qubits, args.min_terms)
    _print_results(filtered, args.subdir, args.min_qubits, args.min_terms)

############################################################
############################################################

if __name__ == "__main__":
    main()
