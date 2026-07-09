#!/usr/bin/env python3

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def build_minimal_aarch64_elf() -> bytes:
    text_vaddr = 0x400000
    text_offset = 0x100
    text = struct.pack(
        "<IIII",
        0x54000040,  # b.eq 0x400008
        0x94000001,  # bl 0x400008
        0xD65F03C0,  # ret
        0xD61F0200,  # br x16
    )
    shstrtab = b"\x00.text\x00.shstrtab\x00.symtab\x00.strtab\x00"
    strtab = b"\x00_start\x00callee\x00"
    symtab_entry_size = 24
    symtab = b"".join([
        b"\x00" * symtab_entry_size,
        struct.pack("<IBBHQQ", 1, 0x12, 0, 1, text_vaddr, len(text)),
        struct.pack("<IBBHQQ", 8, 0x12, 0, 1, text_vaddr + 8, 4),
    ])

    shstr_offset = align(text_offset + len(text), 8)
    symtab_offset = align(shstr_offset + len(shstrtab), 8)
    strtab_offset = align(symtab_offset + len(symtab), 8)
    shoff = align(strtab_offset + len(strtab), 8)
    size = shoff + 5 * 64
    data = bytearray(size)

    data[text_offset:text_offset + len(text)] = text
    data[shstr_offset:shstr_offset + len(shstrtab)] = shstrtab
    data[symtab_offset:symtab_offset + len(symtab)] = symtab
    data[strtab_offset:strtab_offset + len(strtab)] = strtab

    data[0:64] = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x01\x01" + b"\x00" * 9,
        2,
        183,
        1,
        text_vaddr,
        0,
        shoff,
        0,
        64,
        0,
        0,
        64,
        5,
        2,
    )

    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 0x6, text_vaddr, text_offset, len(text), 0, 0, 4, 0),
        (7, 3, 0, 0, shstr_offset, len(shstrtab), 0, 0, 1, 0),
        (17, 2, 0, 0, symtab_offset, len(symtab), 4, 1, 8,
         symtab_entry_size),
        (25, 3, 0, 0, strtab_offset, len(strtab), 0, 0, 1, 0),
    ]
    for index, section in enumerate(sections):
        struct.pack_into("<IIQQQQIIQQ", data, shoff + index * 64, *section)

    return bytes(data)


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
