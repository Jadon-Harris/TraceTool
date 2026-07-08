#ifndef ETE_TRBE_H
#define ETE_TRBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETE_TRBE_TRCIDR_COUNT 14u
#define ETE_TRBE_METADATA_MIN_CAPACITY 768u

struct ete_trbe_caps {
    bool has_ete;
    bool has_trbe;
    bool is_mock;
    bool hardware_validated;
    uint64_t id_aa64dfr0_el1;
    uint64_t trbidr_el1;
    uint64_t trcidr[ETE_TRBE_TRCIDR_COUNT];
};

struct ete_trbe_buffer {
    void *vaddr;
    uint64_t paddr;
    size_t size;
    uint64_t base;
    uint64_t limit;
    uint64_t write_ptr;
    bool wrapped;
};

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

struct ete_trbe_result {
    uint64_t trbsr;
    uint64_t trbptr;
    uint64_t trblimitr;
    uint64_t trbbaser;
    bool wrapped;
    size_t valid_size;
};

const char *ete_trbe_version(void);

int ete_trbe_probe_cpu(unsigned int cpu, struct ete_trbe_caps *caps);

int ete_trbe_alloc_buffer(unsigned int cpu, size_t size,
                          struct ete_trbe_buffer *buf);

int ete_trbe_config_cpu(unsigned int cpu,
                        const struct ete_trbe_config *cfg,
                        const struct ete_trbe_buffer *buf);

int ete_trbe_start_cpu(unsigned int cpu);

int ete_trbe_stop_cpu(unsigned int cpu,
                      struct ete_trbe_result *result);

int ete_trbe_dump_cpu(unsigned int cpu,
                      const char *trace_path,
                      const char *meta_path);

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
