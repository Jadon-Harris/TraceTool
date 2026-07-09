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

        trace.write_bytes(
            b"\x00" * 11
            + b"\x80"
            + b"\x01\x00"
            + b"\x04"
            + b"\xf6"
            + b"\x2d\x01"
            + b"\xf7"
            + b"\x35"
            + b"\x00\x05"
            + b"\x00\x03"
            + b"\x9d\x00\x00\x40\x00\x00\x00\x00\x00"
        )
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
        if flow_data["decoder_stage"] != "speculation-mvp":
            raise AssertionError("unexpected decoder stage")
        kinds = [packet["kind"] for packet in flow_data["packets"]]
        expected = [
            "async",
            "trace_info",
            "trace_on",
            "atom",
            "commit",
            "atom",
            "cancel",
            "overflow",
            "discard",
            "address",
        ]
        if kinds != expected:
            raise AssertionError(f"unexpected packet kinds: {kinds}")
        if flow_data["packets"][3]["fields"]["atoms"] != "N":
            raise AssertionError("atom f1 pattern not decoded")
        if flow_data["packets"][5]["fields"]["atoms"] != "E":
            raise AssertionError("atom f1 taken pattern not decoded")
        if flow_data["packets"][9]["fields"]["address"] != "0x400000":
            raise AssertionError("long address not decoded")
        atom_states = [atom["state"] for atom in flow_data["atom_stream"]]
        if atom_states != ["committed", "canceled"]:
            raise AssertionError(f"unexpected atom states: {atom_states}")
        if flow_data["speculation"]["committed_atoms"] != 1:
            raise AssertionError("commit packet did not resolve one atom")
        if flow_data["speculation"]["canceled_atoms"] != 1:
            raise AssertionError("cancel packet did not resolve one atom")
        event_types = [event["type"] for event in flow_data["events"]]
        if event_types != ["overflow", "discard"]:
            raise AssertionError(f"unexpected events: {event_types}")
        if not branches.read_text(encoding="utf-8").startswith("seq,cpu,"):
            raise AssertionError("branches header missing")
        if "digraph ete_flow" not in dot.read_text(encoding="utf-8"):
            raise AssertionError("dot output missing graph")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
