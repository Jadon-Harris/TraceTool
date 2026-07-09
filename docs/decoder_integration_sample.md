# Decoder Integration Sample

This sample is a deterministic host-only decoder check. It does not use real
ETE/TRBE hardware and must not be treated as hardware validation.

Run it explicitly:

```sh
python3 tests/integration/test_decoder_flow_sample.py
```

The script creates temporary files:

- `sample_trace.bin`: synthetic ETE-like byte stream with async, trace-info,
  trace-on, atom, commit, cancel, overflow, discard, and address packets.
- `sample_trace.json`: metadata that marks the sample as synthetic and not
  hardware validated.
- `sample_app.elf`: minimal ELF64 little-endian AArch64 image with `.text`,
  `.symtab`, and `.strtab`.
- `sample_flow.json`: decoder output with packets, atom stream, branch catalog,
  recovered branches, and dynamic-flow MVP edges.
- `sample_branches.csv`: recovered dynamic branch rows.
- `sample_flow.dot`: DOT graph for the recovered MVP flow.

Expected decoder behavior:

- Parse 10 packets from the synthetic trace.
- Resolve two atoms: one committed and one canceled.
- Build four static branch catalog entries from the ELF image:
  conditional branch, direct call, return, and indirect branch.
- Recover three dynamic branches:
  conditional fallthrough, direct call, and return boundary.

The dynamic recovery remains an MVP. Return targets, indirect branch targets,
exception edges, long-running loops, and real hardware packet sequences still
need target-board validation and additional decoder work.
