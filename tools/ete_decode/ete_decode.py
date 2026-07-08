#!/usr/bin/env python3
"""Minimal ETE raw trace decoder skeleton.

This stage validates CLI plumbing and output contracts only. It does not parse
ETE packets or recover AArch64 flow yet.
"""

import argparse
import csv
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Decode ETE/TRBE raw trace")
    parser.add_argument("--trace", required=True, help="raw ETE trace bytes")
    parser.add_argument("--meta", required=True, help="capture metadata JSON")
    parser.add_argument("--image", required=True, help="program ELF or image")
    parser.add_argument("--out-flow", required=True, help="flow JSON output")
    parser.add_argument("--out-branches", required=True, help="branches CSV output")
    parser.add_argument("--out-dot", required=True, help="Graphviz DOT output")
    return parser.parse_args()


def load_inputs(args: argparse.Namespace) -> tuple[bytes, dict]:
    trace_path = Path(args.trace)
    meta_path = Path(args.meta)
    image_path = Path(args.image)

    if not trace_path.exists():
        raise FileNotFoundError(f"trace file not found: {trace_path}")
    if not meta_path.exists():
        raise FileNotFoundError(f"metadata file not found: {meta_path}")
    if not image_path.exists():
        raise FileNotFoundError(f"image file not found: {image_path}")

    with trace_path.open("rb") as trace_file:
        trace = trace_file.read()
    with meta_path.open("r", encoding="utf-8") as meta_file:
        metadata = json.load(meta_file)

    return trace, metadata


def write_flow(path: Path, trace: bytes, metadata: dict, image: str) -> None:
    flow = {
        "format": "ete-flow-v1",
        "decoder_stage": "skeleton",
        "trace_bytes": len(trace),
        "metadata_format": metadata.get("format", "unknown"),
        "image": image,
        "basic_blocks": [],
        "edges": [],
        "events": [
            {
                "type": "gap",
                "reason": "decoder skeleton has not parsed ETE packets yet",
                "offset": 0,
            }
        ],
        "warnings": [
            "ETE packet parsing and AArch64 flow recovery are not implemented yet"
        ],
    }
    path.write_text(json.dumps(flow, indent=2) + "\n", encoding="utf-8")


def write_branches(path: Path) -> None:
    with path.open("w", newline="", encoding="utf-8") as branches_file:
        writer = csv.writer(branches_file)
        writer.writerow(
            [
                "seq",
                "cpu",
                "context",
                "cycle",
                "timestamp",
                "src_pc",
                "dst_pc",
                "symbol_src",
                "symbol_dst",
                "type",
                "taken",
                "flags",
            ]
        )


def write_dot(path: Path) -> None:
    path.write_text(
        "digraph ete_flow {\n"
        "  label=\"ETE decoder skeleton\";\n"
        "  labelloc=\"t\";\n"
        "}\n",
        encoding="utf-8",
    )


def main() -> int:
    args = parse_args()
    trace, metadata = load_inputs(args)
    write_flow(Path(args.out_flow), trace, metadata, args.image)
    write_branches(Path(args.out_branches))
    write_dot(Path(args.out_dot))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
