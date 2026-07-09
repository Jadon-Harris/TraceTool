#!/usr/bin/env python3
"""Explicit host-only decoder integration sample.

This script is intentionally not registered in default CTest/Makefile tests.
Run it manually when you want an end-to-end synthetic decoder check.
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from tests.support.aarch64_decoder_sample import (  # noqa: E402
    build_minimal_aarch64_elf,
    build_sample_metadata,
    build_sample_trace,
)


def main() -> int:
    decoder = REPO / "tools" / "ete_decode" / "ete_decode.py"

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        trace = work / "sample_trace.bin"
        meta = work / "sample_trace.json"
        image = work / "sample_app.elf"
        flow = work / "sample_flow.json"
        branches = work / "sample_branches.csv"
        dot = work / "sample_flow.dot"

        trace.write_bytes(build_sample_trace())
        meta.write_text(build_sample_metadata(), encoding="utf-8")
        image.write_bytes(build_minimal_aarch64_elf())

        subprocess.check_call([
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
        ])

        flow_data = json.loads(flow.read_text(encoding="utf-8"))
        if flow_data["decoder_stage"] != "dynamic-flow-mvp":
            raise AssertionError("decoder did not run dynamic-flow MVP")
        if flow_data["packet_count"] != 10:
            raise AssertionError("synthetic trace packet count changed")
        if flow_data["branch_catalog_count"] != 4:
            raise AssertionError("ELF branch catalog count changed")
        if flow_data["recovered_branch_count"] != 3:
            raise AssertionError("dynamic recovered branch count changed")
        if flow_data["flow_recovery"]["start_pc"] != "0x400000":
            raise AssertionError("flow recovery did not start at trace address")

        csv_text = branches.read_text(encoding="utf-8")
        if "conditional_branch" not in csv_text or "call_direct" not in csv_text:
            raise AssertionError("branches.csv missing recovered branches")

        dot_text = dot.read_text(encoding="utf-8")
        if "0x400000" not in dot_text or "0x400008" not in dot_text:
            raise AssertionError("flow.dot missing expected PCs")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
