# ETE/TRBE Tool Progress

## Current Status

- 已完成：
  - 阶段 1 项目骨架。
  - CMake 默认 host/mock 构建入口。
  - Makefile host/mock fallback。
  - `ete_trace probe` mock CLI。
  - metadata JSON writer 最小实现。
  - host/mock CTest 测试标签。
- 未完成：
  - ETE/TRBE 寄存器定义与真实 sysreg 访问封装。
  - target feature probe。
  - TRBE buffer allocation / wrap handling。
  - ETE/TRBE start/stop。
  - record/dump/status CLI。
  - raw trace decoder。
- 当前可运行命令：
  - `cmake -S . -B build-host -DETE_TRBE_TARGET=OFF`
  - `cmake --build build-host`
  - `ctest --test-dir build-host -LE "target|hardware"`
  - `make HOST_MOCK=1`
  - `make test`
  - `./build-host/ete_trace probe`

## Last Verified

Host/Mac tests:
- 是否通过：是。
- 执行命令：
  - `make HOST_MOCK=1`
  - `make test`
  - `./build-host/ete_trace probe`
  - `git diff --check`
- 结果：全部通过。`cmake` 当前 Mac shell 不可用，因此阶段 1 使用 Makefile fallback 完成 host/mock 验证。

Target build:
- 是否已验证：否。
- 如果未验证，原因：真实 target 支持尚未实现，需要办公电脑交叉编译和目标板验证。

Hardware validation:
- 是否已上板验证：否。
- 如果未验证，原因：当前仅为 Mac host/mock skeleton，没有在 Armv9-A FEAT_ETE/FEAT_TRBE 单板运行。

## Next Steps

1. 阶段 2：新增 ETE/TRBE register definitions 和 sysreg wrapper。
2. 阶段 3：实现 feature probe。
3. 阶段 4：实现 TRBE buffer 和完整 metadata。

## Hardware Validation Needed

- 是否需要 Armv9-A FEAT_ETE/FEAT_TRBE 真机验证：是。
- 当前哪些功能只通过 mock：`ete_trace probe`、metadata JSON writer、host/mock tests。
