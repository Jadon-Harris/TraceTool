#ifndef ETE_TRBE_H
#define ETE_TRBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETE_TRBE_TRCIDR_COUNT 14u
#define ETE_TRBE_METADATA_MIN_CAPACITY 2048u

/*
 * 公共 API 返回码。这里保持数值小而稳定，因为 CLI 会直接把这些状态映射
 * 成用户可见诊断，测试也会断言部分错误路径。
 */
enum ete_trbe_status {
    ETE_TRBE_OK = 0,
    ETE_TRBE_ERR_INVALID_ARGUMENT = -1,
    ETE_TRBE_ERR_UNSUPPORTED = -2,
    ETE_TRBE_ERR_PERMISSION = -3,
    ETE_TRBE_ERR_IO = -4,
    ETE_TRBE_ERR_BAD_STATE = -5
};

/*
 * 单个 CPU 的 probe 结果。host/mock 构建下 feature bit 是合成值，可通过
 * ETE_TRBE_MOCK_HAS_ETE / ETE_TRBE_MOCK_HAS_TRBE 覆盖。hardware_validated
 * 必须保持 false，直到真实单板运行证明这条路径。
 */
struct ete_trbe_caps {
    bool has_ete;
    bool has_trbe;
    bool is_mock;
    bool hardware_validated;
    uint64_t id_aa64dfr0_el1;
    uint64_t trbidr_el1;
    uint64_t trcidr[ETE_TRBE_TRCIDR_COUNT];
};

/*
 * TRBE buffer 描述。vaddr 是 host 可访问分配；paddr 在 target 真实物理 /
 * translation 配置完成前只是占位。base、limit、write_ptr 模拟寄存器视角，
 * 这样 metadata 和测试在 host/target 路径上能使用同一组字段。
 */
struct ete_trbe_buffer {
    void *vaddr;
    uint64_t paddr;
    size_t size;
    uint64_t base;
    uint64_t limit;
    uint64_t write_ptr;
    bool wrapped;
};

/*
 * library 边界接受的采集配置。当前 mock 实现还不会消费所有字段，但先保留
 * 在这里，后续加入 target 寄存器编程时不需要再改变 CLI/API 形状。
 */
struct ete_trbe_config {
    unsigned int cpu;
    size_t buffer_size;
    uint64_t trace_id;
    bool trace_el0;
    bool trace_el1;
    bool enable_cycle_count;
    bool enable_timestamp;
    bool use_addr_range;
    uint64_t range_start;
    uint64_t range_end;
};

/*
 * stop 时的采集结果。mock 路径从内存 buffer 填充这些字段；target 代码后续
 * 会在按硬件要求顺序停止 ETE/TRBE 后，把最终 TRB 寄存器状态复制到这里。
 */
struct ete_trbe_result {
    uint64_t trbsr;
    uint64_t trbptr;
    uint64_t trblimitr;
    uint64_t trbbaser;
    bool wrapped;
    size_t valid_size;
};

const char *ete_trbe_version(void);
const char *ete_trbe_strerror(int status);

/* 对单个 CPU 做 ETE/TRBE 能力探测，不启动采集。 */
int ete_trbe_probe_cpu(unsigned int cpu, struct ete_trbe_caps *caps);

/*
 * 分配/释放采集 buffer。host 构建要求页大小对齐，方便 review ring buffer
 * 行为，也贴近后续 TRBE 寄存器对 buffer 的约束。
 */
int ete_trbe_alloc_buffer(unsigned int cpu, size_t size,
                          struct ete_trbe_buffer *buf);
void ete_trbe_free_buffer(struct ete_trbe_buffer *buf);

/* 从可能已经 wrap 的 TRBE buffer 返回/复制线性化后的有效 trace 字节。 */
size_t ete_trbe_buffer_valid_size(const struct ete_trbe_buffer *buf);
int ete_trbe_copy_valid_trace(const struct ete_trbe_buffer *buf,
                              uint8_t *out, size_t out_size,
                              size_t *bytes_written);

/* 配置、启动、停止一个进程内 capture slot。 */
int ete_trbe_config_cpu(unsigned int cpu,
                        const struct ete_trbe_config *cfg,
                        const struct ete_trbe_buffer *buf);

int ete_trbe_start_cpu(unsigned int cpu);

int ete_trbe_stop_cpu(unsigned int cpu,
                      struct ete_trbe_result *result);

/* 持久化 dump 支持的占位接口；当前 record 直接走 copy/write。 */
int ete_trbe_dump_cpu(unsigned int cpu,
                      const char *trace_path,
                      const char *meta_path);

/* 写出 ete_decode.py 消费的 raw trace sidecar metadata 合约。 */
int ete_trbe_write_metadata_json(char *out, size_t out_size,
                                 unsigned int cpu,
                                 const struct ete_trbe_caps *caps,
                                 const struct ete_trbe_buffer *buffer,
                                 const struct ete_trbe_result *result,
                                 const char *image_path);

#ifdef __cplusplus
}
#endif

#endif
