#!/usr/bin/env python3
"""Compare private corpus files with an explicitly supplied pinned C++ harness.

Manifest: one extension<TAB>path per line. Relative paths use the manifest
folder. Results include private input paths: keep the output outside releases.
No input files are modified or copied into the source tree.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=180.0)
    args = parser.parse_args()
    if args.jobs < 1 or args.timeout <= 0:
        parser.error("jobs and timeout must be positive")
    binary = args.binary.resolve(strict=True)
    manifest = args.manifest.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    result_path = output / "results.json"
    if result_path.exists():
        parser.error("output already has results.json; use a new artifact folder")
    inputs = []
    for line in manifest.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        extension, name = line.split("\t", 1)
        path = Path(name)
        if not path.is_absolute():
            path = manifest.parent / path
        inputs.append({"extension": extension, "path": str(path.resolve(strict=True))})
    report = {"platform": platform.platform(), "binary": str(binary),
              "binary_sha256": sha256(binary), "manifest_sha256": sha256(manifest),
              "timeout_seconds": args.timeout, "jobs": args.jobs,
              "inputs": inputs, "results": []}

    def compare(item):
        index, mode = item
        path = inputs[index]["path"]
        log = output / f"{index:03d}_{mode.removeprefix('--')}.log"
        started = time.monotonic()
        with log.open("w") as sink:
            try:
                completed = subprocess.run([str(binary), mode, path], stdout=sink,
                                           stderr=subprocess.STDOUT, timeout=args.timeout,
                                           check=False)
                code = completed.returncode
            except subprocess.TimeoutExpired:
                code = 124
        return {"index": index, "mode": mode, "exit": code, "log": log.name,
                "elapsed_seconds": round(time.monotonic() - started, 3)}

    tasks = [(i, mode) for i in range(len(inputs))
             for mode in ("--read-file", "--read-file-source")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for result in pool.map(compare, tasks):
            report["results"].append(result)
            result_path.write_text(json.dumps(report, indent=2) + "\n")
            print(result["index"], result["mode"], result["exit"], flush=True)
    passed = sum(row["exit"] == 0 for row in report["results"])
    print(f"Executions: {len(tasks)}; exact record matches: {passed}; residuals: {len(tasks) - passed}")
    return 0 if passed == len(tasks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
