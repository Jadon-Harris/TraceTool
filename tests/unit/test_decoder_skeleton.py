#!/usr/bin/env python3

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
        trace = work / "trace.bin"
        meta = work / "trace.json"
        image = work / "app.elf"
        flow = work / "flow.json"
        branches = work / "branches.csv"
        dot = work / "flow.dot"

        trace.write_bytes(build_sample_trace())
        meta.write_text(build_sample_metadata(), encoding="utf-8")
        image.write_bytes(build_minimal_aarch64_elf())

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
        if flow_data["decoder_stage"] != "dynamic-flow-mvp":
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
        branch_kinds = [
            branch["kind"] for branch in flow_data["branch_catalog"]
        ]
        expected_branches = [
            "conditional_branch",
            "call_direct",
            "return",
            "branch_indirect",
        ]
        if branch_kinds != expected_branches:
            raise AssertionError(f"unexpected branch catalog: {branch_kinds}")
        if flow_data["branch_catalog"][1]["dst"] != "0x400008":
            raise AssertionError("BL target not decoded")
        if flow_data["branch_catalog"][1]["symbol_dst"] != "callee":
            raise AssertionError("branch target symbol not resolved")
        recovered = flow_data["recovered_branches"]
        if [branch["kind"] for branch in recovered] != [
            "conditional_branch",
            "call_direct",
            "return",
        ]:
            raise AssertionError(f"unexpected recovered flow: {recovered}")
        if recovered[0]["taken"] != "no" or recovered[0]["dst"] != "0x400004":
            raise AssertionError("conditional atom did not drive fallthrough")
        if flow_data["flow_recovery"]["branches_recovered"] != 3:
            raise AssertionError("unexpected recovered branch count")
        event_types = [event["type"] for event in flow_data["events"]]
        if event_types != ["overflow", "discard"]:
            raise AssertionError(f"unexpected events: {event_types}")
        branch_csv = branches.read_text(encoding="utf-8")
        if not branch_csv.startswith("seq,cpu,"):
            raise AssertionError("branches header missing")
        if "0x400004,0x400008,_start,callee,call_direct" not in branch_csv:
            raise AssertionError("branches csv missing static call")
        if "digraph ete_flow" not in dot.read_text(encoding="utf-8"):
            raise AssertionError("dot output missing graph")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
