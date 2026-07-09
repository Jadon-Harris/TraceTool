# ETE/TRBE Tool Progress

## Current Status

- 已完成：
  - 阶段 1 项目骨架。
  - CMake 默认 host/mock 构建入口。
  - Makefile host/mock fallback。
  - `ete_trace probe` mock CLI。
  - metadata JSON writer 最小实现。
  - host/mock CTest 测试标签。
  - 阶段 2 ETE/TRBE register definitions 初版。
  - sysreg wrapper host/mock 隔离层。
  - 阶段 3 feature probe MVP。
  - mock probe 环境变量覆盖：`ETE_TRBE_MOCK_HAS_ETE` / `ETE_TRBE_MOCK_HAS_TRBE`。
  - 阶段 4 host/mock TRBE buffer allocation/free。
  - 阶段 4 ring buffer wrap linearize。
  - metadata JSON 扩展到 TRCIDR / ETE config / image address placeholders。
  - 阶段 5 mock capture config/start/stop 状态机。
  - 阶段 6 `ete_trace record` one-shot raw/meta 输出。
  - 阶段 6 `ete_trace status` 明确说明当前无跨进程持久 capture。
  - 阶段 7 `ete_decode.py` skeleton，可读取 raw/meta/image 并输出 flow/branches/dot。
  - 阶段 8 `ete_decode.py` ETE packet parser MVP。
  - 阶段 8 识别 async、trace-info、timestamp、trace-on、exception、context、address、source address、Q、atom、commit/cancel/mispredict、overflow、discard、reserved/incomplete packet。
  - 阶段 8 flow.json 输出 `packet_count` 和 `packets` 列表，overflow/discard/bad/reserved/incomplete 映射为事件。
  - 阶段 9 speculation resolution MVP。
  - 阶段 9 flow.json 输出 `atom_count`、`atom_stream`、`speculation` summary/events。
  - 阶段 9 支持基于 atom、commit、cancel、cancel_mispredict、mispredict packet 标记 committed/canceled/pending/mispredict atom。
  - 阶段 10 AArch64 ELF 静态 branch catalog MVP。
  - 阶段 10 `ete_decode.py` 可解析 ELF64 little-endian AArch64 `.text`、section table、symtab/dynsym。
  - 阶段 10 可识别 AArch64 `B`、`BL`、`B.cond`、`CBZ/CBNZ`、`TBZ/TBNZ`、`RET`、`BR`、`BLR`、`ERET`，并输出 `branch_catalog`、branches.csv 静态行、flow.dot 静态边。
- 未完成：
  - 经目标工具链验证的完整 TRC/TRB sysreg encoding 读写。
  - target 物理连续 buffer、TRBE translation regime 配置和 cache/DMA 同步。
  - 真实 ETE/TRBE start/stop 寄存器编程序列。
  - start/stop/dump 持久 capture CLI。
  - 完整 ETE packet parser，包括所有配置相关变长字段、条件指令 trace、data trace、VMID/CtxtID 宽度感知解析。
  - 完整 speculation resolution，包括所有 ETE 配置模式、周期精确窗口、异常/trace restart 边界和与 instruction-flow 的联动验证。
  - 动态 AArch64 ELF/image 执行流恢复，即把 ETE packet/atom stream 和 branch catalog 严格合并为真实路径。
- 当前可运行命令：
  - `cmake -S . -B build-host -DETE_TRBE_TARGET=OFF`
  - `cmake --build build-host`
  - `ctest --test-dir build-host -LE "target|hardware"`
  - `make HOST_MOCK=1`
  - `make test`
  - `./build-host/test_sysreg_mock`
  - `./build-host/test_ring_buffer`
  - `./build-host/test_capture_state_mock`
  - `./build-host/ete_trace record --cpu 0 --size 64K --duration-ms 1 --image app.elf --out trace_cpu0.bin --meta trace_cpu0.json`
  - `python3 tests/unit/test_decoder_skeleton.py`
  - `env PYTHONPYCACHEPREFIX=/private/tmp/etrbe-pycache python3 -m py_compile tools/ete_decode/ete_decode.py tests/unit/test_decoder_skeleton.py`
  - `ETE_TRBE_MOCK_HAS_ETE=1 ETE_TRBE_MOCK_HAS_TRBE=1 ./build-host/ete_trace probe --cpu 0`
  - `./build-host/ete_trace probe`

## Last Verified

Host/Mac tests:
- 是否通过：是。
- 执行命令：
  - `make HOST_MOCK=1`
  - `make test`
  - `./build-host/ete_trace probe`
  - `git diff --check`
- 结果：阶段 1、阶段 2、阶段 3、阶段 4、阶段 5、阶段 6、阶段 7、阶段 8、阶段 9、阶段 10 静态 branch catalog MVP 均通过。`cmake` 当前 Mac shell 不可用时可使用 Makefile fallback 完成 host/mock 验证。

Target build:
- 是否已验证：否。
- 如果未验证，原因：真实 target 支持尚未实现，需要办公电脑交叉编译和目标板验证。

Hardware validation:
- 是否已上板验证：否。
- 如果未验证，原因：当前仅为 Mac host/mock skeleton，没有在 Armv9-A FEAT_ETE/FEAT_TRBE 单板运行。

## Next Steps

1. 阶段 10 后续：把 ETE atom stream 与 ELF branch catalog 合并为动态执行流。
2. 阶段 11：添加 integration sample 和文档。

## Hardware Validation Needed

- 是否需要 Armv9-A FEAT_ETE/FEAT_TRBE 真机验证：是。
- 当前哪些功能只通过 mock：`ete_trace probe`、`ete_trace record` raw/meta 输出、`ete_decode.py` packet parser/speculation/static branch catalog MVP、metadata JSON writer、TRBE buffer linearize、capture state machine、sysreg wrapper mock path、mock feature override、host/mock tests。
