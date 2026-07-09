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
 * Public API status values. Keep these small and stable because the CLI maps
 * them directly to user-facing diagnostics and tests assert selected paths.
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
 * Probe result for one CPU. In host/mock builds the feature bits are synthetic
 * and can be overridden by ETE_TRBE_MOCK_HAS_ETE / ETE_TRBE_MOCK_HAS_TRBE.
 * hardware_validated must remain false until a real board run proves the path.
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
 * TRBE buffer description. vaddr is the host-accessible allocation; paddr is a
 * placeholder until target builds grow a real physical/translation setup. base,
 * limit, and write_ptr mirror the register-oriented view so metadata and tests
 * can use the same fields on host and target paths.
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
 * Capture configuration accepted by the library boundary. Not every field is
 * consumed by the current mock implementation yet, but keeping them here avoids
 * changing the CLI/API shape when target programming is added.
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
 * Stop-time capture result. The mock path fills this from the in-memory buffer;
 * target code will eventually copy final TRB register state here after stopping
 * ETE and TRBE in the hardware-required order.
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

/* Probe one CPU for ETE/TRBE capability without starting capture. */
int ete_trbe_probe_cpu(unsigned int cpu, struct ete_trbe_caps *caps);

/*
 * Allocate/free a capture buffer. Host builds require page-size alignment so
 * ring-buffer behavior and future TRBE register constraints are easy to review.
 */
int ete_trbe_alloc_buffer(unsigned int cpu, size_t size,
                          struct ete_trbe_buffer *buf);
void ete_trbe_free_buffer(struct ete_trbe_buffer *buf);

/* Return/copy the linearized valid bytes from a possibly wrapped TRBE buffer. */
size_t ete_trbe_buffer_valid_size(const struct ete_trbe_buffer *buf);
int ete_trbe_copy_valid_trace(const struct ete_trbe_buffer *buf,
                              uint8_t *out, size_t out_size,
                              size_t *bytes_written);

/* Configure, start, and stop one process-local capture slot. */
int ete_trbe_config_cpu(unsigned int cpu,
                        const struct ete_trbe_config *cfg,
                        const struct ete_trbe_buffer *buf);

int ete_trbe_start_cpu(unsigned int cpu);

int ete_trbe_stop_cpu(unsigned int cpu,
                      struct ete_trbe_result *result);

/* Placeholder for future persistent dump support; record uses copy/write now. */
int ete_trbe_dump_cpu(unsigned int cpu,
                      const char *trace_path,
                      const char *meta_path);

/* Write the raw-trace sidecar metadata contract consumed by ete_decode.py. */
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
