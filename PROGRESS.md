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
- 未完成：
  - 经目标工具链验证的完整 TRC/TRB sysreg encoding 读写。
  - target 物理连续 buffer、TRBE translation regime 配置和 cache/DMA 同步。
  - 真实 ETE/TRBE start/stop 寄存器编程序列。
  - record/dump/status CLI。
  - raw trace decoder。
- 当前可运行命令：
  - `cmake -S . -B build-host -DETE_TRBE_TARGET=OFF`
  - `cmake --build build-host`
  - `ctest --test-dir build-host -LE "target|hardware"`
  - `make HOST_MOCK=1`
  - `make test`
  - `./build-host/test_sysreg_mock`
  - `./build-host/test_ring_buffer`
  - `./build-host/test_capture_state_mock`
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
- 结果：阶段 1、阶段 2、阶段 3、阶段 4、阶段 5 均通过。`cmake` 当前 Mac shell 不可用，因此使用 Makefile fallback 完成 host/mock 验证。

Target build:
- 是否已验证：否。
- 如果未验证，原因：真实 target 支持尚未实现，需要办公电脑交叉编译和目标板验证。

Hardware validation:
- 是否已上板验证：否。
- 如果未验证，原因：当前仅为 Mac host/mock skeleton，没有在 Armv9-A FEAT_ETE/FEAT_TRBE 单板运行。

## Next Steps

1. 阶段 6：扩展采集 CLI record/dump/status。
2. 阶段 7：新增 raw trace decoder skeleton。
3. 阶段 8：实现 ETE packet parser MVP。

## Hardware Validation Needed

- 是否需要 Armv9-A FEAT_ETE/FEAT_TRBE 真机验证：是。
- 当前哪些功能只通过 mock：`ete_trace probe`、metadata JSON writer、TRBE buffer linearize、capture state machine、sysreg wrapper mock path、mock feature override、host/mock tests。
