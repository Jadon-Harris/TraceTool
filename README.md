# TraceTool

TraceTool is an ETE + TRBE trace toolkit for Armv9-A/AArch64 program-flow
trace. The repository is structured so Codex and Mac hosts can build and test
host/mock logic by default, while real ETE/TRBE register access is compiled only
for explicit target builds.

## Environment Split

- Mac/Codex host: generates code, builds host/mock tools, runs unit and mock
  tests, and develops the offline decoder.
- Office build machine: clones the repository and cross-compiles target
  binaries with an internal AArch64 toolchain.
- Closed target board: runs `ete_trace`, captures raw trace and metadata, then
  exports those files for offline decoding.

Mac default builds never access `TRC*`, `TRB*`, `TRFCR_ELx`, CoreSight device
nodes, `/dev/mem`, perf, debugfs, root-only interfaces, or target SDK headers.

## Host Build

```sh
cmake -S . -B build-host -DETE_TRBE_TARGET=OFF
cmake --build build-host
ctest --test-dir build-host -LE "target|hardware"
```

The default option is `ETE_TRBE_TARGET=OFF`, so `cmake -S . -B build` is also a
host/mock build.

If CMake is unavailable on the Mac host, use the fallback Makefile:

```sh
make HOST_MOCK=1
make test
```

Run the current mock probe:

```sh
./build-host/ete_trace probe
```

Expected host output includes:

```text
FEAT_ETE: mock
FEAT_TRBE: mock
Status: host mock only, hardware validation required
```

Mock tests can explicitly simulate feature presence without claiming real
hardware validation:

```sh
ETE_TRBE_MOCK_HAS_ETE=1 ETE_TRBE_MOCK_HAS_TRBE=1 ./build-host/ete_trace probe
```

Run a host/mock one-shot record:

```sh
./build-host/ete_trace record --cpu 0 --size 64K --duration-ms 10 \
  --image app.elf --out trace_cpu0.bin --meta trace_cpu0.json
```

In host/mock mode the raw file can be empty because no hardware trace is
generated. The metadata warning records that hardware validation is still
required.

Run the decoder skeleton:

```sh
python3 tools/ete_decode/ete_decode.py \
  --trace trace_cpu0.bin \
  --meta trace_cpu0.json \
  --image app.elf \
  --out-flow flow.json \
  --out-branches branches.csv \
  --out-dot flow.dot
```

## Target Build Placeholder

Real target support must be enabled explicitly:

```sh
cmake -S . -B build-target \
  -DETE_TRBE_TARGET=ON \
  -DCMAKE_C_COMPILER=/path/to/aarch64-linux-gnu-gcc
cmake --build build-target
```

Makefile fallback:

```sh
make TARGET_AARCH64=1 CC=/path/to/aarch64-linux-gnu-gcc
```

The current target implementation is a skeleton and still requires Armv9-A
FEAT_ETE/FEAT_TRBE hardware validation.

## Current Scope

Implemented in the first stage:

- CMake project skeleton.
- Public capture/probe data structures.
- Host/mock `ete_trace probe`.
- `ete_trace probe --cpu N` argument parsing.
- Minimal metadata JSON writer.
- CTest host/mock tests.
- ETE/TRBE register name and bit definitions.
- Sysreg wrapper boundary that compiles to mock/unsupported by default.
- Host/mock TRBE buffer allocation, free, valid-size calculation, and wrap
  linearization.
- Host/mock capture state machine for config/start/stop.
- `ete_trace record` one-shot CLI for raw/metadata file output.
- `ete_decode.py` skeleton that reads trace, metadata, and image inputs and
  writes `flow.json`, `branches.csv`, and `flow.dot`.
- Metadata JSON includes TRCIDR placeholders, ETE config placeholders, and image
  address fields.

Not implemented yet:

- Target physical/translation-regime buffer setup and cache maintenance.
- ETE/TRBE start/stop sequencing.
- Real ETE/TRBE start/stop sequencing.
- `record`, `dump`, and `status` commands.
- Raw ETE packet decoding and AArch64 flow recovery.

## Offline Workflow Goal

```text
1. Mac/Codex generates code and pushes it.
2. Office machine clones or pulls the repository.
3. Office machine cross-compiles target tools.
4. Target board runs ete_trace probe/record.
5. Raw trace and metadata are copied back.
6. ete_decode reconstructs branch flow from raw trace, metadata, and ELF.
```
