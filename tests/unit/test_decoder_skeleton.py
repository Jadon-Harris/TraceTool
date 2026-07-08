#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    decoder = repo / "tools" / "ete_decode" / "ete_decode.py"

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        trace = work / "trace.bin"
        meta = work / "trace.json"
        image = work / "app.elf"
        flow = work / "flow.json"
        branches = work / "branches.csv"
        dot = work / "flow.dot"

        trace.write_bytes(b"")
        meta.write_text('{"format": "ete-trbe-raw-v1"}\n', encoding="utf-8")
        image.write_bytes(b"\x7fELF")

        subprocess.check_call(
            [
                sys.executable,
                str(decoder),
                "--trace",
                str(trace),
                "--meta",
                str(meta),
                "--image",
                str(image),
                "--out-flow",
                str(flow),
                "--out-branches",
                str(branches),
                "--out-dot",
                str(dot),
            ]
        )

        flow_data = json.loads(flow.read_text(encoding="utf-8"))
        if flow_data["decoder_stage"] != "skeleton":
            raise AssertionError("unexpected decoder stage")
        if not branches.read_text(encoding="utf-8").startswith("seq,cpu,"):
            raise AssertionError("branches header missing")
        if "digraph ete_flow" not in dot.read_text(encoding="utf-8"):
            raise AssertionError("dot output missing graph")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
