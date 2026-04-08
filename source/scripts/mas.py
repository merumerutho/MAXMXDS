import os
import sys
from pathlib import Path
import argparse
import subprocess

MMUTIL_CMD = r"C:/devkitpro/tools/bin/mmutil.exe"
EXTENSIONS = {'.xm', '.mod', '.s3m', '.it'}
TIMEOUT_S = 30

def convert_to_mas(args):
    path, input_root, output_dir = args
    rel = Path(path).relative_to(input_root)
    out_subdir = Path(output_dir) / rel.parent
    os.makedirs(out_subdir, exist_ok=True)

    stem = Path(path).stem
    out = str(out_subdir / (stem + '.mas'))

    try:
        result = subprocess.run(
            [MMUTIL_CMD, '-d', '-y', '-m', str(path), '-o' + out],
            timeout=TIMEOUT_S,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            return f"FAIL  {rel} (exit code {result.returncode})"
        return None  # success
    except subprocess.TimeoutExpired:
        return f"TIMEOUT  {rel} (killed after {TIMEOUT_S}s)"
    except Exception as e:
        return f"ERROR  {rel} ({e})"


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", "-i", required=True, help="Input folder containing module data")
    ap.add_argument("--output", "-o", required=True, help="Output folder for .mas files")
    ap.add_argument("--jobs", "-j", type=int, default=0, help="Parallel jobs (0 = auto)")

    args = ap.parse_args()

    input_root = Path(args.input)
    os.makedirs(args.output, exist_ok=True)

    # Collect all tracker files
    files = [
        (e, input_root, args.output)
        for e in input_root.rglob("*")
        if e.suffix.lower() in EXTENSIONS
    ]

    if not files:
        print("No tracker files found in", input_root)
        sys.exit(0)

    # Try parallel, fall back to sequential
    n_jobs = args.jobs if args.jobs > 0 else None

    errors = []
    try:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(max_workers=n_jobs) as pool:
            results = list(pool.map(convert_to_mas, files))
        errors = [r for r in results if r is not None]
    except (ImportError, RuntimeError):
        for f in files:
            r = convert_to_mas(f)
            if r is not None:
                errors.append(r)

    ok = len(files) - len(errors)
    print(f"Done: {ok}/{len(files)} converted")

    if errors:
        print(f"\n{len(errors)} problem(s):")
        for e in errors:
            print(f"  {e}")
        sys.exit(1)
