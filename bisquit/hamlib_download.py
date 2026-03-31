"""
 *  author: Suhas Vittal
 *  date:   31 March 2026
 *
 *  Downloads a zipped Hamiltonian file from the HamLib NERSC portal.
 *
 *  Usage:
 *      python hamlib_download.py <file-path> [--output FILE]
 *
 *  Example:
 *      python hamlib_download.py chemistry/electronic/standard/h2_sto-3g.hdf5.zip
 * """

import argparse
import os
import sys

import requests

############################################################
############################################################

HAMLIB_BASE_URL = "https://portal.nersc.gov/cfs/m888/dcamps/hamlib"

############################################################
############################################################

def _file_url(filepath: str) -> str:
    return f"{HAMLIB_BASE_URL}/{filepath.strip('/')}"


def _download_file(url: str, dest: str) -> None:
    """
    Streams the file at `url` to `dest`. Raises on HTTP errors.
    """
    response = requests.get(url, stream=True, timeout=60)
    response.raise_for_status()
    with open(dest, "wb") as f:
        for chunk in response.iter_content(chunk_size=65536):
            f.write(chunk)

############################################################
############################################################

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Download a zipped Hamiltonian file from the HamLib NERSC portal."
    )
    parser.add_argument(
        "filepath",
        help='HamLib file path (e.g. "chemistry/electronic/standard/h2_sto-3g.hdf5.zip")',
    )
    parser.add_argument(
        "--output",
        metavar="FILE",
        default=None,
        help="destination file path (default: basename of the remote path)",
    )
    args = parser.parse_args()

    url  = _file_url(args.filepath)
    hamlib_dir = os.path.join(os.path.dirname(__file__), "hamlib")
    os.makedirs(hamlib_dir, exist_ok=True)
    dest = args.output if args.output else os.path.join(hamlib_dir, os.path.basename(args.filepath))

    print(f"Downloading: {url}")
    print(f"         -> {dest}")
    try:
        _download_file(url, dest)
    except requests.HTTPError as e:
        print(f"Error: could not download file ({e})")
        print(f"Check that '{args.filepath}' is a valid HamLib file path.")
        sys.exit(1)
    except requests.RequestException as e:
        print(f"Network error: {e}")
        sys.exit(1)

    size_kb = os.path.getsize(dest) / 1024
    print(f"Done. Saved {size_kb:.1f} KB to '{dest}'.")

############################################################
############################################################

if __name__ == "__main__":
    main()
