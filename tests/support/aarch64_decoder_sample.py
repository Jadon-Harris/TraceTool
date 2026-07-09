#!/usr/bin/env python3
"""decoder 测试使用的 synthetic AArch64 ELF 和 ETE byte stream。

这个样本刻意很小，而且只用于 host。它不代表硬件 ETE capture，只是 decoder
管线的确定性 fixture。
"""

import struct


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def build_sample_trace() -> bytes:
    return (
        b"\x00" * 11
        + b"\x80"  # async
        + b"\x01\x00"  # 不带 optional section 的 trace-info
        + b"\x04"  # trace-on
        + b"\xf6"  # atom N
        + b"\x2d\x01"  # commit 一个 atom
        + b"\xf7"  # atom E
        + b"\x35"  # cancel 一个 atom
        + b"\x00\x05"  # overflow
        + b"\x00\x03"  # discard
        + b"\x9d\x00\x00\x40\x00\x00\x00\x00\x00"  # address 0x400000
    )


def build_sample_metadata() -> str:
    return (
        "{\n"
        "  \"format\": \"ete-trbe-raw-v1\",\n"
        "  \"cpu\": 0,\n"
        "  \"environment\": {\n"
        "    \"capture_mode\": \"synthetic\",\n"
        "    \"hardware_validated\": false\n"
        "  },\n"
        "  \"warnings\": [\"synthetic host-only decoder sample\"]\n"
        "}\n"
    )


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
