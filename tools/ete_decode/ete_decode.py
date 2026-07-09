#!/usr/bin/env python3
"""最小 ETE raw trace decoder。

packet parser 刻意保持小范围实现，但遵循 Arm OpenCSD 使用的 ETMv4/ETE packet
header 分类。它会产出 packet 记录，供后续 speculation 和 instruction-flow
恢复阶段使用。
"""

import argparse
import csv
import json
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


ASYNC_PACKET = b"\x00" * 11 + b"\x80"


@dataclass
class EtePacket:
    """一个已解码 ETE packet，保留原始 byte offset 方便审计和调试。"""

    offset: int
    raw: bytes
    kind: str
    fields: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        item = {
            "offset": self.offset,
            "length": len(self.raw),
            "kind": self.kind,
            "raw": self.raw.hex(),
        }
        if self.fields:
            item["fields"] = self.fields
        return item


@dataclass
class AtomResolution:
    """经过 MVP speculation 处理后的一个 E/N atom。"""

    seq: int
    packet_offset: int
    atom_index: int
    atom: str
    state: str
    resolved_by: Optional[int] = None
    flags: list[str] = field(default_factory=list)

    def to_json(self) -> dict:
        item = {
            "seq": self.seq,
            "packet_offset": self.packet_offset,
            "atom_index": self.atom_index,
            "atom": self.atom,
            "state": self.state,
        }
        if self.resolved_by is not None:
            item["resolved_by"] = self.resolved_by
        if self.flags:
            item["flags"] = self.flags
        return item


@dataclass
class BranchInstruction:
    """JSON、CSV、DOT 共用的一条静态或已恢复 AArch64 branch 记录。"""

    seq: int
    pc: int
    dst: Optional[int]
    kind: str
    taken: str
    symbol_src: str = ""
    symbol_dst: str = ""
    flags: list[str] = field(default_factory=list)

    def to_json(self) -> dict:
        item = {
            "seq": self.seq,
            "pc": f"0x{self.pc:x}",
            "kind": self.kind,
            "taken": self.taken,
            "symbol_src": self.symbol_src,
            "symbol_dst": self.symbol_dst,
            "flags": self.flags,
        }
        item["dst"] = f"0x{self.dst:x}" if self.dst is not None else None
        return item


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


def read_cont_u32(trace: bytes, start: int, limit: int = 5) -> tuple[int, int]:
    """按 little-endian 7-bit stream 读取 ETM/ETE continuation bytes。"""

    value = 0
    pos = start
    for idx in range(limit):
        if pos >= len(trace):
            raise ValueError("incomplete continuation field")
        byte = trace[pos]
        value |= (byte & 0x7F) << (idx * 7)
        pos += 1
        if (byte & 0x80) == 0:
            return value, pos
    raise ValueError("unterminated continuation field")


def read_cont_u64(trace: bytes, start: int) -> tuple[int, int, int]:
    """读取 timestamp 风格 continuation 字段，并返回有效 bit 宽度。"""

    value = 0
    pos = start
    for idx in range(9):
        if pos >= len(trace):
            raise ValueError("incomplete timestamp field")
        byte = trace[pos]
        mask = 0xFF if idx == 8 else 0x7F
        value |= (byte & mask) << (idx * 7)
        pos += 1
        if idx == 8 or (byte & 0x80) == 0:
            bits = 64 if idx == 8 else (idx + 1) * 7
            return value, bits, pos
    raise ValueError("unterminated timestamp field")


def parse_addr_payload(header: int, payload: bytes, isa: int, bits: int) -> int:
    """还原 address payload，同时处理 A64/A32 低位编码差异。"""

    if bits == 32:
        if isa == 0:
            return ((payload[0] & 0x7F) << 2 |
                    (payload[1] & 0x7F) << 9 |
                    payload[2] << 16 |
                    payload[3] << 24)
        return ((payload[0] & 0x7F) << 1 |
                payload[1] << 8 |
                payload[2] << 16 |
                payload[3] << 24)

    if isa == 0:
        return ((payload[0] & 0x7F) << 2 |
                (payload[1] & 0x7F) << 9 |
                payload[2] << 16 |
                payload[3] << 24 |
                payload[4] << 32 |
                payload[5] << 40 |
                payload[6] << 48 |
                payload[7] << 56)
    return ((payload[0] & 0x7F) << 1 |
            payload[1] << 8 |
            payload[2] << 16 |
            payload[3] << 24 |
            payload[4] << 32 |
            payload[5] << 40 |
            payload[6] << 48 |
            payload[7] << 56)


def parse_short_addr(trace: bytes, start: int, isa: int) -> tuple[int, int, int]:
    """解码 address 和 Q packet 形式使用的 short address payload。"""

    first = trace[start]
    shift = 2 if isa == 0 else 1
    value = (first & 0x7F) << shift
    bits = 7 + shift
    pos = start + 1
    if first & 0x80:
        if pos >= len(trace):
            raise ValueError("incomplete short address")
        value |= trace[pos] << (7 + shift)
        bits += 8
        pos += 1
    return value, bits, pos


def atom_pattern(header: int) -> tuple[str, int, int]:
    """把紧凑 atom packet header 映射为 E/N 字符串。

    E 表示条件分支 executed/taken，N 表示 not executed/not taken。映射遵循
    ETMv4/ETE atom packet family，但当前 decoder 仍只把结果作为后续 flow
    recovery 的 MVP 输入。
    """

    f4_patterns = [0xE, 0x0, 0xA, 0x5]

    if 0xF6 <= header <= 0xF7:
        pattern = header & 0x1
        count = 1
    elif 0xD8 <= header <= 0xDB:
        pattern = header & 0x3
        count = 2
    elif 0xF8 <= header <= 0xFF:
        pattern = header & 0x7
        count = 3
    elif 0xDC <= header <= 0xDF:
        pattern = f4_patterns[header & 0x3]
        count = 4
    elif 0xD5 <= header <= 0xD7 or header == 0xF5:
        pattern_index = ((header & 0x20) >> 3) | (header & 0x3)
        patterns = {5: 0x1E, 1: 0x00, 2: 0x0A, 3: 0x15}
        pattern = patterns.get(pattern_index, 0)
        count = 5
    else:
        e_count = (header & 0x1F) + 3
        pattern = (1 << e_count) - 1
        if (header & 0x20) == 0:
            pattern |= 1 << e_count
        count = e_count + 1

    atoms = "".join("E" if (pattern >> idx) & 1 else "N"
                    for idx in range(count))
    return atoms, pattern, count


def parse_trace_info(trace: bytes, offset: int) -> tuple[EtePacket, int]:
    """解析 trace-info，至少保留已出现的 optional section。"""

    pos = offset + 1
    controls = []
    while True:
        if pos >= len(trace):
            raise ValueError("incomplete trace-info controls")
        byte = trace[pos]
        controls.append(byte)
        pos += 1
        if (byte & 0x80) == 0:
            break

    present = controls[0] & 0x1F
    names = [
        ("info", 0x01),
        ("key", 0x02),
        ("speculation", 0x04),
        ("cycle_threshold", 0x08),
        ("commit_window", 0x10),
    ]
    sections = {}
    for name, bit in names:
        if present & bit:
            value, pos = read_cont_u32(trace, pos)
            sections[name] = value

    return EtePacket(offset, trace[offset:pos], "trace_info",
                     {"control": controls, "sections": sections}), pos


def parse_packet(trace: bytes, offset: int) -> tuple[EtePacket, int]:
    """解码一个 packet，并返回下一个 byte offset。

    unknown、reserved、truncated 情况会变成显式 packet，而不是直接抛异常；
    这样下游 JSON 能显示 byte stream 从哪里开始无法理解。
    """

    header = trace[offset]

    if header == 0x00:
        # Header 0x00 要么是长 async 序列，要么是两字节 extension。
        if trace.startswith(ASYNC_PACKET, offset):
            end = offset + len(ASYNC_PACKET)
            return EtePacket(offset, trace[offset:end], "async"), end
        if offset + 1 >= len(trace):
            return EtePacket(offset, trace[offset:], "incomplete",
                             {"reason": "extension header at end"}), len(trace)
        ext = trace[offset + 1]
        if ext == 0x03:
            return EtePacket(offset, trace[offset:offset + 2], "discard"), offset + 2
        if ext == 0x05:
            return EtePacket(offset, trace[offset:offset + 2], "overflow"), offset + 2
        return EtePacket(offset, trace[offset:offset + 2], "bad_sequence",
                         {"extension": ext}), offset + 2

    if header == 0x01:
        packet, end = parse_trace_info(trace, offset)
        return packet, end

    if header in (0x02, 0x03):
        ts, bits, pos = read_cont_u64(trace, offset + 1)
        fields = {"timestamp": ts, "timestamp_bits": bits}
        if header & 1:
            cycle_count, pos = read_cont_u32(trace, pos, 3)
            fields["cycle_count"] = cycle_count
        return EtePacket(offset, trace[offset:pos], "timestamp", fields), pos

    no_payload = {
        0x04: "trace_on",
        0x0A: "transaction_start",
        0x0B: "transaction_commit",
        0x70: "ignore",
        0x88: "timestamp_marker",
    }
    if header in no_payload:
        return EtePacket(offset, trace[offset:offset + 1], no_payload[header]), offset + 1

    if header == 0x06:
        if offset + 1 >= len(trace):
            return EtePacket(offset, trace[offset:], "incomplete",
                             {"reason": "exception payload missing"}), len(trace)
        second = trace[offset + 1]
        size = 3 if (second & 0x80) else 2
        end = min(offset + size, len(trace))
        fields = {"exception_type": (second >> 1) & 0x1F}
        if end - offset < size:
            fields["reason"] = "exception payload truncated"
            return EtePacket(offset, trace[offset:end], "incomplete", fields), end
        if second & 0x80:
            fields["exception_type"] |= (trace[offset + 2] & 0x1F) << 5
        return EtePacket(offset, trace[offset:end], "exception", fields), end

    if 0x71 <= header <= 0x7F:
        return EtePacket(offset, trace[offset:offset + 1], "event",
                         {"event": header & 0x0F}), offset + 1

    if header in (0x80, 0x81):
        if (header & 1) == 0:
            return EtePacket(offset, trace[offset:offset + 1], "context",
                             {"updated": False}), offset + 1
        if offset + 1 >= len(trace):
            return EtePacket(offset, trace[offset:], "incomplete",
                             {"reason": "context info missing"}), len(trace)
        info = trace[offset + 1]
        fields = {
            "updated": True,
            "el": info & 0x03,
            "secure": (info >> 3) & 1,
            "ns": (info >> 4) & 1,
            "sf": (info >> 5) & 1,
            "vmid_present": bool(info & 0x40),
            "context_id_present": bool(info & 0x80),
        }
        return EtePacket(offset, trace[offset:offset + 2], "context", fields), offset + 2

    long_addr_headers = {
        0x82: ("address_context", 32, 0),
        0x83: ("address_context", 32, 1),
        0x85: ("address_context", 64, 0),
        0x86: ("address_context", 64, 1),
        0x9A: ("address", 32, 0),
        0x9B: ("address", 32, 1),
        0x9D: ("address", 64, 0),
        0x9E: ("address", 64, 1),
        0xB6: ("source_address", 32, 0),
        0xB7: ("source_address", 32, 1),
        0xB8: ("source_address", 64, 0),
        0xB9: ("source_address", 64, 1),
    }
    if header in long_addr_headers:
        kind, bits, isa = long_addr_headers[header]
        addr_len = bits // 8
        end = min(offset + 1 + addr_len, len(trace))
        if end - offset < 1 + addr_len:
            return EtePacket(offset, trace[offset:end], "incomplete",
                             {"reason": f"{kind} payload truncated"}), end
        value = parse_addr_payload(header, trace[offset + 1:end], isa, bits)
        fields = {"address": f"0x{value:x}", "isa": isa, "bits": bits}
        if kind == "address_context":
            if end >= len(trace):
                fields["context_truncated"] = True
            else:
                info = trace[end]
                fields.update({
                    "context_info": info,
                    "el": info & 0x03,
                    "secure": (info >> 3) & 1,
                    "ns": (info >> 4) & 1,
                    "sf": (info >> 5) & 1,
                    "vmid_present": bool(info & 0x40),
                    "context_id_present": bool(info & 0x80),
                })
                end += 1
        return EtePacket(offset, trace[offset:end], kind, fields), end

    if header in (0x95, 0x96, 0xB4, 0xB5):
        kind = "source_address" if header >= 0xB4 else "address"
        isa = 1 if header in (0x96, 0xB5) else 0
        value, bits, end = parse_short_addr(trace, offset + 1, isa)
        return EtePacket(offset, trace[offset:end], kind,
                         {"address": f"0x{value:x}", "isa": isa,
                          "bits": bits, "short": True}), end

    if 0x90 <= header <= 0x92 or 0xB0 <= header <= 0xB2:
        kind = "source_address_match" if header >= 0xB0 else "address_match"
        return EtePacket(offset, trace[offset:offset + 1], kind,
                         {"exact_match": header & 0x03}), offset + 1

    if header == 0x2D or header in (0x2E, 0x2F):
        value, end = read_cont_u32(trace, offset + 1)
        kind = {0x2D: "commit", 0x2E: "cancel", 0x2F: "cancel_mispredict"}[header]
        field = "commit_elements" if header == 0x2D else "cancel_elements"
        return EtePacket(offset, trace[offset:end], kind, {field: value}), end

    if 0x30 <= header <= 0x33:
        atoms = {1: "E", 2: "EE", 3: "N"}.get(header & 0x03, "")
        return EtePacket(offset, trace[offset:offset + 1], "mispredict",
                         {"atoms": atoms}), offset + 1

    if 0x34 <= header <= 0x37:
        atoms = {1: "E", 2: "EE", 3: "N"}.get(header & 0x03, "")
        return EtePacket(offset, trace[offset:offset + 1], "cancel",
                         {"cancel_elements": 1, "atoms": atoms}), offset + 1

    if 0x38 <= header <= 0x3F:
        atoms = "E" if header & 1 else ""
        return EtePacket(offset, trace[offset:offset + 1], "cancel",
                         {"cancel_elements": ((header >> 1) & 0x03) + 2,
                          "atoms": atoms}), offset + 1

    if 0xA0 <= header <= 0xAF:
        q_type = header & 0x0F
        if q_type in (0x3, 0x4, 0x7, 0x8, 0x9, 0xD, 0xE):
            return EtePacket(offset, trace[offset:offset + 1], "reserved",
                             {"header": header}), offset + 1
        if q_type == 0xF:
            return EtePacket(offset, trace[offset:offset + 1], "q",
                             {"count_present": False, "q_type": q_type}), offset + 1
        pos = offset + 1
        fields = {"count_present": True, "q_type": q_type}
        if q_type in (0x5, 0x6):
            isa = 0 if q_type == 0x5 else 1
            value, bits, pos = parse_short_addr(trace, pos, isa)
            fields.update({"address": f"0x{value:x}", "isa": isa, "bits": bits})
        elif q_type in (0xA, 0xB):
            isa = 0 if q_type == 0xA else 1
            end_addr = pos + 4
            if end_addr > len(trace):
                return EtePacket(offset, trace[offset:], "incomplete",
                                 {"reason": "q long address truncated"}), len(trace)
            value = parse_addr_payload(header, trace[pos:end_addr], isa, 32)
            fields.update({"address": f"0x{value:x}", "isa": isa, "bits": 32})
            pos = end_addr
        elif q_type in (0x0, 0x1, 0x2):
            fields["exact_match"] = q_type & 0x03
        count, pos = read_cont_u32(trace, pos)
        fields["count"] = count
        return EtePacket(offset, trace[offset:pos], "q", fields), pos

    if (0xC0 <= header <= 0xD7 or 0xD8 <= header <= 0xDF or
            0xE0 <= header <= 0xF5 or 0xF6 <= header <= 0xFF):
        atoms, pattern, count = atom_pattern(header)
        return EtePacket(offset, trace[offset:offset + 1], "atom",
                         {"atoms": atoms, "pattern": pattern,
                          "count": count}), offset + 1

    return EtePacket(offset, trace[offset:offset + 1], "reserved",
                     {"header": header}), offset + 1


def parse_trace_packets(trace: bytes) -> list[EtePacket]:
    """尽力把完整 raw trace byte stream 切成 packet。"""

    packets = []
    offset = 0
    while offset < len(trace):
        try:
            packet, next_offset = parse_packet(trace, offset)
        except (IndexError, ValueError) as exc:
            packet = EtePacket(offset, trace[offset:offset + 1], "incomplete",
                               {"reason": str(exc)})
            next_offset = offset + 1
        packets.append(packet)
        if next_offset <= offset:
            next_offset = offset + 1
        offset = next_offset
    return packets


def packet_events(packets: list[EtePacket]) -> list[dict]:
    """把 packet 级别的数据丢失或格式错误提升为 flow event。"""

    events = []
    for packet in packets:
        if packet.kind in {"overflow", "discard"}:
            events.append({"type": packet.kind, "offset": packet.offset})
        elif packet.kind in {"bad_sequence", "reserved", "incomplete"}:
            events.append({
                "type": "gap",
                "reason": packet.kind,
                "offset": packet.offset,
            })
    if not packets:
        events.append({
            "type": "gap",
            "reason": "empty trace byte stream",
            "offset": 0,
        })
    return events


def resolve_speculation(packets: list[EtePacket]) -> tuple[list[AtomResolution], dict]:
    """用 MVP 的小型 commit/cancel 模型解析 atom packet。

    如果没有 resolution packet，所有 atom 都视为 committed，因为很多简单 trace
    不启用 speculation reporting。一旦出现 commit/cancel packet，atom 会保持
    pending，直到被显式 resolved。
    """

    atoms: list[AtomResolution] = []
    pending: list[int] = []
    events: list[dict] = []
    saw_resolution_packet = False

    def append_atoms(packet: EtePacket, state: str,
                     flags: Optional[list[str]] = None) -> None:
        """把一个 packet 中的每个 E/N 字符追加为可独立跟踪的 atom。"""

        atom_text = packet.fields.get("atoms", "")
        for atom_index, atom in enumerate(atom_text):
            entry = AtomResolution(
                seq=len(atoms),
                packet_offset=packet.offset,
                atom_index=atom_index,
                atom=atom,
                state=state,
                flags=list(flags or []),
            )
            atoms.append(entry)
            if state == "pending":
                pending.append(entry.seq)

    for packet in packets:
        if packet.kind == "atom":
            append_atoms(packet, "pending")
            continue

        if packet.kind == "mispredict":
            append_atoms(packet, "mispredict", ["mispredict"])
            events.append({
                "type": "mispredict",
                "offset": packet.offset,
                "atoms": packet.fields.get("atoms", ""),
            })
            continue

        if packet.kind == "commit":
            saw_resolution_packet = True
            count = int(packet.fields.get("commit_elements", 0))
            committed = 0
            # commit 按程序顺序 retire 最老的 pending atom。
            while count > 0 and pending:
                atom_seq = pending.pop(0)
                atoms[atom_seq].state = "committed"
                atoms[atom_seq].resolved_by = packet.offset
                committed += 1
                count -= 1
            if count > 0:
                events.append({
                    "type": "speculation_gap",
                    "reason": "commit exceeds pending atoms",
                    "offset": packet.offset,
                    "missing_atoms": count,
                })
            events.append({
                "type": "commit",
                "offset": packet.offset,
                "atoms": committed,
            })
            continue

        if packet.kind in {"cancel", "cancel_mispredict"}:
            saw_resolution_packet = True
            count = int(packet.fields.get("cancel_elements", 0))
            canceled = 0
            flags = ["mispredict"] if packet.kind == "cancel_mispredict" else []
            # cancel 优先移除最新的 speculative atom。
            while count > 0 and pending:
                atom_seq = pending.pop()
                atoms[atom_seq].state = "canceled"
                atoms[atom_seq].resolved_by = packet.offset
                atoms[atom_seq].flags.extend(flags)
                canceled += 1
                count -= 1
            if count > 0:
                events.append({
                    "type": "speculation_gap",
                    "reason": "cancel exceeds pending atoms",
                    "offset": packet.offset,
                    "missing_atoms": count,
                })
            events.append({
                "type": "cancel",
                "offset": packet.offset,
                "atoms": canceled,
                "mispredict": packet.kind == "cancel_mispredict",
            })

    if saw_resolution_packet:
        for atom_seq in pending:
            events.append({
                "type": "speculation_pending",
                "offset": atoms[atom_seq].packet_offset,
                "seq": atom_seq,
            })
    else:
        for atom_seq in pending:
            atoms[atom_seq].state = "committed"
        pending.clear()

    summary = {
        "mode": "resolution-packets" if saw_resolution_packet else "no-resolution-packets",
        "total_atoms": len(atoms),
        "committed_atoms": sum(1 for atom in atoms if atom.state == "committed"),
        "canceled_atoms": sum(1 for atom in atoms if atom.state == "canceled"),
        "pending_atoms": sum(1 for atom in atoms if atom.state == "pending"),
        "mispredict_atoms": sum(1 for atom in atoms if atom.state == "mispredict"),
        "events": events,
    }
    return atoms, summary


def sign_extend(value: int, bits: int) -> int:
    """对指定 bit 宽度的指令立即数做符号扩展。"""

    sign_bit = 1 << (bits - 1)
    if value & sign_bit:
        return value - (1 << bits)
    return value


def read_c_string(blob: bytes, offset: int) -> str:
    """从 ELF string table 读取 NUL 结尾字符串。"""

    if offset < 0 or offset >= len(blob):
        return ""
    end = blob.find(b"\x00", offset)
    if end < 0:
        end = len(blob)
    return blob[offset:end].decode("utf-8", errors="replace")


def decode_aarch64_branch(insn: int, pc: int) -> Optional[dict]:
    """识别 flow MVP 需要的 AArch64 branch 指令。

    mask 刻意只覆盖常见 control-flow opcode。其他指令返回 None，并在 branch
    记录之间视为线性代码。
    """

    if (insn & 0x7C000000) == 0x14000000:
        imm = sign_extend(insn & 0x03FFFFFF, 26) << 2
        is_link = bool(insn & 0x80000000)
        return {
            "kind": "call_direct" if is_link else "branch_direct",
            "dst": pc + imm,
            "taken": "always",
            "flags": ["link"] if is_link else [],
        }

    if (insn & 0xFF000010) == 0x54000000:
        imm = sign_extend((insn >> 5) & 0x7FFFF, 19) << 2
        cond = insn & 0xF
        return {
            "kind": "conditional_branch",
            "dst": pc + imm,
            "taken": "unknown",
            "flags": [f"cond={cond}"],
        }

    if (insn & 0x7E000000) == 0x34000000:
        imm = sign_extend((insn >> 5) & 0x7FFFF, 19) << 2
        op = "cbnz" if (insn & (1 << 24)) else "cbz"
        width = 64 if (insn & (1 << 31)) else 32
        return {
            "kind": "conditional_branch",
            "dst": pc + imm,
            "taken": "unknown",
            "flags": [op, f"rt={insn & 0x1f}", f"width={width}"],
        }

    if (insn & 0x7E000000) == 0x36000000:
        imm = sign_extend((insn >> 5) & 0x3FFF, 14) << 2
        bit = ((insn >> 19) & 0x1F) | (((insn >> 31) & 1) << 5)
        op = "tbnz" if (insn & (1 << 24)) else "tbz"
        return {
            "kind": "conditional_branch",
            "dst": pc + imm,
            "taken": "unknown",
            "flags": [op, f"rt={insn & 0x1f}", f"bit={bit}"],
        }

    if (insn & 0xFFFFFC1F) == 0xD65F0000:
        return {
            "kind": "return",
            "dst": None,
            "taken": "always",
            "flags": [f"rn={(insn >> 5) & 0x1f}"],
        }

    if (insn & 0xFFFFFC1F) == 0xD61F0000:
        return {
            "kind": "branch_indirect",
            "dst": None,
            "taken": "always",
            "flags": [f"rn={(insn >> 5) & 0x1f}"],
        }

    if (insn & 0xFFFFFC1F) == 0xD63F0000:
        return {
            "kind": "call_indirect",
            "dst": None,
            "taken": "always",
            "flags": [f"rn={(insn >> 5) & 0x1f}", "link"],
        }

    if insn == 0xD69F03E0:
        return {
            "kind": "exception_return",
            "dst": None,
            "taken": "always",
            "flags": [],
        }

    return None


def find_symbol(symbols: list[dict], addr: Optional[int]) -> str:
    """返回包含某地址的最接近 symbol 名。"""

    if addr is None:
        return ""
    best = ""
    best_value = -1
    for symbol in symbols:
        value = symbol["value"]
        size = symbol["size"]
        if value <= addr and (size == 0 or addr < value + size):
            if value > best_value:
                best = symbol["name"]
                best_value = value
    return best


def parse_hex_int(value: object) -> Optional[int]:
    """接受 JSON 风格 hex string 或整数，并归一化为 int。"""

    if value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def parse_elf_symbols(data: bytes, sections: list[dict]) -> list[dict]:
    """从 ELF symtab/dynsym section 提取名称和地址范围。"""

    symbols = []
    for section in sections:
        if section["type"] not in (2, 11) or section["entsize"] == 0:
            continue
        if section["link"] >= len(sections):
            continue
        strtab = sections[section["link"]]
        names = data[strtab["offset"]:strtab["offset"] + strtab["size"]]
        start = section["offset"]
        end = start + section["size"]
        for offset in range(start, end, section["entsize"]):
            if offset + 24 > len(data):
                break
            st_name, st_info, _st_other, st_shndx, st_value, st_size = (
                struct.unpack_from("<IBBHQQ", data, offset)
            )
            name = read_c_string(names, st_name)
            sym_type = st_info & 0x0F
            if name and sym_type in (0, 2):
                symbols.append({
                    "name": name,
                    "value": st_value,
                    "size": st_size,
                    "section_index": st_shndx,
                })
    symbols.sort(key=lambda item: item["value"])
    return symbols


def parse_elf_branch_catalog(image_path: str) -> tuple[dict, list[BranchInstruction]]:
    """从 ELF64 little-endian AArch64 image 构建静态 branch catalog。

    解析失败写入 info["warnings"]，而不是直接抛异常；这样 malformed 或非 ELF
    输入仍能产出 packet/speculation 结果。
    """

    path = Path(image_path)
    info = {
        "path": image_path,
        "format": "unknown",
        "machine": None,
        "entry": None,
        "text_vaddr": None,
        "text_size": 0,
        "symbols": [],
        "warnings": [],
    }
    branches: list[BranchInstruction] = []

    try:
        data = path.read_bytes()
    except OSError as exc:
        info["warnings"].append(f"image read failed: {exc}")
        return info, branches

    if len(data) < 64 or not data.startswith(b"\x7fELF"):
        info["warnings"].append("image is not an ELF file")
        return info, branches
    if data[4] != 2 or data[5] != 1:
        info["warnings"].append("only ELF64 little-endian images are supported")
        return info, branches

    try:
        header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    except struct.error as exc:
        info["warnings"].append(f"ELF header parse failed: {exc}")
        return info, branches

    machine = header[2]
    entry = header[4]
    shoff = header[6]
    shentsize = header[11]
    shnum = header[12]
    shstrndx = header[13]
    info.update({
        "format": "elf64-le",
        "machine": machine,
        "entry": f"0x{entry:x}",
    })
    if machine != 183:
        info["warnings"].append(f"ELF machine {machine} is not AArch64")
        return info, branches
    if shoff == 0 or shentsize < 64 or shnum == 0:
        info["warnings"].append("ELF section table is missing")
        return info, branches

    # 需要 section name 来定位 .text 和链接到的 string table。
    sections = []
    for index in range(shnum):
        offset = shoff + index * shentsize
        if offset + 64 > len(data):
            info["warnings"].append("ELF section table is truncated")
            return info, branches
        fields = struct.unpack_from("<IIQQQQIIQQ", data, offset)
        sections.append({
            "index": index,
            "name_offset": fields[0],
            "type": fields[1],
            "flags": fields[2],
            "addr": fields[3],
            "offset": fields[4],
            "size": fields[5],
            "link": fields[6],
            "info": fields[7],
            "addralign": fields[8],
            "entsize": fields[9],
            "name": "",
        })

    if shstrndx >= len(sections):
        info["warnings"].append("ELF section-name table index is invalid")
        return info, branches
    shstr = sections[shstrndx]
    names = data[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for section in sections:
        section["name"] = read_c_string(names, section["name_offset"])

    text = next((section for section in sections if section["name"] == ".text"),
                None)
    if text is None:
        info["warnings"].append("ELF .text section is missing")
        return info, branches

    symbols = parse_elf_symbols(data, sections)
    info["text_vaddr"] = f"0x{text['addr']:x}"
    info["text_size"] = text["size"]
    info["symbols"] = [
        {"name": sym["name"], "value": f"0x{sym['value']:x}",
         "size": sym["size"]}
        for sym in symbols
    ]

    # AArch64 指令定长 4 字节，因此静态扫描按 4 字节对齐推进。
    text_data = data[text["offset"]:text["offset"] + text["size"]]
    if len(text_data) < text["size"]:
        info["warnings"].append("ELF .text section is truncated")
    for pos in range(0, len(text_data) - (len(text_data) % 4), 4):
        insn = struct.unpack_from("<I", text_data, pos)[0]
        pc = text["addr"] + pos
        decoded = decode_aarch64_branch(insn, pc)
        if decoded is None:
            continue
        branches.append(BranchInstruction(
            seq=len(branches),
            pc=pc,
            dst=decoded["dst"],
            kind=decoded["kind"],
            taken=decoded["taken"],
            symbol_src=find_symbol(symbols, pc),
            symbol_dst=find_symbol(symbols, decoded["dst"]),
            flags=decoded["flags"],
        ))

    return info, branches


def first_trace_address(packets: list[EtePacket]) -> Optional[int]:
    """如果 trace 里有 address packet，就用第一个 address 作为动态 flow 起点。"""

    for packet in packets:
        if packet.kind in {"address", "address_context"}:
            return parse_hex_int(packet.fields.get("address"))
    return None


def recover_aarch64_flow(packets: list[EtePacket],
                         atoms: list[AtomResolution],
                         image_info: dict,
                         branch_catalog: list[BranchInstruction]
                         ) -> tuple[list[BranchInstruction], list[dict], dict]:
    """把 trace atom 和静态 branch catalog 合成为路径 MVP。

    这里刻意保守：跟随 direct branch/call；每个 conditional branch 消费一个
    resolved atom；一旦下一步需要调用栈或 indirect target 重建就停止。
    """

    events = []
    edges = []
    recovered: list[BranchInstruction] = []
    catalog = sorted(branch_catalog, key=lambda branch: branch.pc)
    start_pc = first_trace_address(packets)
    if start_pc is None:
        start_pc = parse_hex_int(image_info.get("entry"))
    if start_pc is None:
        events.append({"type": "flow_gap", "reason": "missing start PC"})
        return recovered, edges, {
            "stage": "dynamic-mvp",
            "start_pc": None,
            "branches_recovered": 0,
            "events": events,
        }
    if not catalog:
        events.append({"type": "flow_gap", "reason": "empty branch catalog"})
        return recovered, edges, {
            "stage": "dynamic-mvp",
            "start_pc": f"0x{start_pc:x}",
            "branches_recovered": 0,
            "events": events,
        }

    symbols = [
        {
            "name": symbol["name"],
            "value": parse_hex_int(symbol["value"]) or 0,
            "size": symbol["size"],
        }
        for symbol in image_info.get("symbols", [])
    ]
    atom_index = 0
    cursor = start_pc
    visits: dict[int, int] = {}
    step_limit = max(16, len(catalog) * 4 + len(atoms) + 4)

    def next_branch_at_or_after(pc: int) -> Optional[BranchInstruction]:
        """从 pc 线性执行后，寻找 catalog 中下一个 branch。"""

        for branch in catalog:
            if branch.pc >= pc:
                return branch
        return None

    def next_resolved_atom() -> Optional[AtomResolution]:
        """跳过 canceled atom，因为它们没有从 speculation 中存活。"""

        nonlocal atom_index
        while atom_index < len(atoms):
            atom = atoms[atom_index]
            atom_index += 1
            if atom.state in {"committed", "mispredict", "pending"}:
                return atom
        return None

    for _step in range(step_limit):
        branch = next_branch_at_or_after(cursor)
        if branch is None:
            events.append({
                "type": "flow_gap",
                "reason": "no branch at or after cursor",
                "cursor": f"0x{cursor:x}",
            })
            break
        if branch.pc > cursor:
            events.append({
                "type": "linear_scan",
                "from": f"0x{cursor:x}",
                "to": f"0x{branch.pc:x}",
            })

        visits[branch.pc] = visits.get(branch.pc, 0) + 1
        if visits[branch.pc] > 4:
            events.append({
                "type": "flow_gap",
                "reason": "branch visit limit reached",
                "pc": f"0x{branch.pc:x}",
            })
            break

        dst = branch.dst
        taken = branch.taken
        flags = list(branch.flags) + ["dynamic-mvp"]
        if branch.kind == "conditional_branch":
            # 在当前 MVP 中，E 选择编码的 branch target，N 选择 fallthrough。
            atom = next_resolved_atom()
            if atom is None:
                taken = "unknown"
                dst = branch.pc + 4
                flags.append("atom=missing")
                events.append({
                    "type": "flow_gap",
                    "reason": "conditional branch without atom",
                    "pc": f"0x{branch.pc:x}",
                })
            else:
                taken = "yes" if atom.atom == "E" else "no"
                dst = branch.dst if atom.atom == "E" else branch.pc + 4
                flags.extend([
                    f"atom_seq={atom.seq}",
                    f"atom={atom.atom}",
                    f"atom_state={atom.state}",
                ])
        elif branch.kind in {"branch_direct", "call_direct"}:
            taken = "yes"
        elif branch.kind in {"return", "branch_indirect", "call_indirect",
                             "exception_return"}:
            # 这些需要调用栈或 data-flow 恢复；当前只记录边界。
            taken = "yes"
            flags.append("target_unresolved")

        recovered_branch = BranchInstruction(
            seq=len(recovered),
            pc=branch.pc,
            dst=dst,
            kind=branch.kind,
            taken=taken,
            symbol_src=branch.symbol_src,
            symbol_dst=find_symbol(symbols, dst),
            flags=flags,
        )
        recovered.append(recovered_branch)
        edges.append({
            "seq": recovered_branch.seq,
            "src_pc": f"0x{recovered_branch.pc:x}",
            "dst_pc": f"0x{dst:x}" if dst is not None else None,
            "type": recovered_branch.kind,
            "taken": recovered_branch.taken,
            "flags": recovered_branch.flags,
        })

        if branch.kind in {"return", "branch_indirect", "call_indirect",
                           "exception_return"}:
            events.append({
                "type": "flow_stop",
                "reason": f"{branch.kind} target is unresolved in MVP",
                "pc": f"0x{branch.pc:x}",
            })
            break
        if dst is None:
            break
        cursor = dst
    else:
        events.append({"type": "flow_gap", "reason": "step limit reached"})

    summary = {
        "stage": "dynamic-mvp",
        "start_pc": f"0x{start_pc:x}",
        "branches_recovered": len(recovered),
        "atom_cursor": atom_index,
        "events": events,
    }
    return recovered, edges, summary


def decode_trace(trace: bytes, image: str) -> dict:
    """运行 decoder pipeline，并保留输出所需的中间产物。"""

    packets = parse_trace_packets(trace)
    atom_stream, speculation = resolve_speculation(packets)
    image_info, branch_catalog = parse_elf_branch_catalog(image)
    recovered_branches, edges, flow_recovery = recover_aarch64_flow(
        packets, atom_stream, image_info, branch_catalog
    )
    return {
        "packets": packets,
        "atom_stream": atom_stream,
        "speculation": speculation,
        "image_info": image_info,
        "branch_catalog": branch_catalog,
        "recovered_branches": recovered_branches,
        "edges": edges,
        "flow_recovery": flow_recovery,
    }


def write_flow(path: Path, trace: bytes, metadata: dict, image: str,
               decoded: dict) -> None:
    """写出用于 review 和后续工具消费的完整 JSON 输出。"""

    packets = decoded["packets"]
    atom_stream = decoded["atom_stream"]
    branch_catalog = decoded["branch_catalog"]
    warnings = [
        "dynamic AArch64 flow recovery is an MVP and stops at unresolved "
        "return/indirect targets"
    ]
    warnings.extend(decoded["image_info"].get("warnings", []))
    flow = {
        "format": "ete-flow-v1",
        "decoder_stage": "dynamic-flow-mvp",
        "trace_bytes": len(trace),
        "metadata_format": metadata.get("format", "unknown"),
        "image": image,
        "image_info": decoded["image_info"],
        "packet_count": len(packets),
        "packets": [packet.to_json() for packet in packets],
        "atom_count": len(atom_stream),
        "atom_stream": [atom.to_json() for atom in atom_stream],
        "speculation": decoded["speculation"],
        "branch_catalog_count": len(branch_catalog),
        "branch_catalog": [branch.to_json() for branch in branch_catalog],
        "recovered_branch_count": len(decoded["recovered_branches"]),
        "recovered_branches": [
            branch.to_json() for branch in decoded["recovered_branches"]
        ],
        "basic_blocks": [],
        "edges": decoded["edges"],
        "flow_recovery": decoded["flow_recovery"],
        "events": packet_events(packets),
        "warnings": warnings,
    }
    path.write_text(json.dumps(flow, indent=2) + "\n", encoding="utf-8")


def write_branches(path: Path, branches: list[BranchInstruction],
                   metadata: dict, source: str) -> None:
    """写出紧凑 branch 表；source 标记 dynamic 输出或 static fallback。"""

    cpu = metadata.get("cpu", "")
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
        for branch in branches:
            writer.writerow([
                branch.seq,
                cpu,
                "",
                "",
                "",
                f"0x{branch.pc:x}",
                f"0x{branch.dst:x}" if branch.dst is not None else "",
                branch.symbol_src,
                branch.symbol_dst,
                branch.kind,
                branch.taken,
                "|".join(branch.flags + [source]),
            ])


def write_dot(path: Path, branches: list[BranchInstruction]) -> None:
    """写出最小 DOT 图，便于快速可视化检查。"""

    lines = [
        "digraph ete_flow {",
        "  label=\"ETE dynamic flow MVP\";",
        "  labelloc=\"t\";",
    ]
    for branch in branches:
        src = f"0x{branch.pc:x}"
        if branch.dst is None:
            lines.append(f"  \"{src}\" [label=\"{src}\\n{branch.kind}\"];")
            continue
        dst = f"0x{branch.dst:x}"
        lines.append(f"  \"{src}\" -> \"{dst}\" [label=\"{branch.kind}\"];")
    lines.append("}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    trace, metadata = load_inputs(args)
    decoded = decode_trace(trace, args.image)
    write_flow(Path(args.out_flow), trace, metadata, args.image, decoded)
    output_branches = decoded["recovered_branches"] or decoded["branch_catalog"]
    branch_source = "dynamic" if decoded["recovered_branches"] else "static"
    write_branches(Path(args.out_branches), output_branches, metadata,
                   branch_source)
    write_dot(Path(args.out_dot), output_branches)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
