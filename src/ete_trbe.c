#define _POSIX_C_SOURCE 200112L

#include "ete_trbe.h"

#include "ete_trbe_regs.h"
#include "ete_trbe_sysreg.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ETE_TRBE_BUFFER_ALIGNMENT 4096u
#define ETE_TRBE_MAX_CPUS 256u

/*
 * The capture state is process-local by design at this stage. A future daemon
 * or kernel-facing implementation can replace this table without changing the
 * CLI record/probe contract.
 */
enum capture_state {
    CAPTURE_IDLE = 0,
    CAPTURE_CONFIGURED,
    CAPTURE_RUNNING,
    CAPTURE_STOPPED
};

/* One slot tracks the mock lifecycle for a single CPU id. */
struct capture_slot {
    enum capture_state state;
    struct ete_trbe_config config;
    struct ete_trbe_buffer buffer;
    struct ete_trbe_result result;
};

static struct capture_slot g_capture_slots[ETE_TRBE_MAX_CPUS];

const char *ete_trbe_version(void)
{
    return "ete-trbe-tool-0.1";
}

const char *ete_trbe_strerror(int status)
{
    switch (status) {
    case ETE_TRBE_OK:
        return "ok";
    case ETE_TRBE_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case ETE_TRBE_ERR_UNSUPPORTED:
        return "unsupported in this build or on this platform";
    case ETE_TRBE_ERR_PERMISSION:
        return "trace register access denied or trapped";
    case ETE_TRBE_ERR_IO:
        return "I/O error";
    case ETE_TRBE_ERR_BAD_STATE:
        return "invalid capture state";
    default:
        return "unknown error";
    }
}

static bool mock_env_enabled(const char *name)
{
    const char *value = getenv(name);

    return value != NULL && strcmp(value, "0") != 0 && value[0] != '\0';
}

int ete_trbe_probe_cpu(unsigned int cpu, struct ete_trbe_caps *caps)
{
    (void)cpu;

    if (caps == NULL) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

    memset(caps, 0, sizeof(*caps));

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    /*
     * Never key real sysreg access on __aarch64__ alone: Apple Silicon hosts
     * are AArch64 too. ETE_TRBE_TARGET is the explicit opt-in for board builds.
     */
    uint64_t id_aa64dfr0 = 0;
    uint64_t tracever;
    uint64_t tracebuffer;
    int rc;

    rc = ete_trbe_sysreg_read(ETE_TRBE_SYSREG_ID_AA64DFR0_EL1, &id_aa64dfr0);
    if (rc != 0) {
        return ETE_TRBE_ERR_PERMISSION;
    }

    tracever = ete_trbe_field_get(id_aa64dfr0,
                                  ETE_TRBE_ID_AA64DFR0_TRACEVER_MASK,
                                  ETE_TRBE_ID_AA64DFR0_TRACEVER_SHIFT);
    tracebuffer = ete_trbe_field_get(id_aa64dfr0,
                                     ETE_TRBE_ID_AA64DFR0_TRACEBUFFER_MASK,
                                     ETE_TRBE_ID_AA64DFR0_TRACEBUFFER_SHIFT);

    caps->has_ete = tracever != 0;
    caps->has_trbe = tracebuffer != 0;
    caps->is_mock = false;
    caps->hardware_validated = false;
    caps->id_aa64dfr0_el1 = id_aa64dfr0;

    rc = ete_trbe_sysreg_read(ETE_TRBE_SYSREG_TRBIDR_EL1, &caps->trbidr_el1);
    if (rc != 0) {
        caps->trbidr_el1 = 0;
    }

    return (caps->has_ete && caps->has_trbe) ? ETE_TRBE_OK :
                                                ETE_TRBE_ERR_UNSUPPORTED;
#else
    /*
     * Host builds are deliberately boring: feature presence is synthetic and
     * visible in output as mock-only, so a Mac test cannot be mistaken for
     * hardware validation.
     */
    caps->has_ete = mock_env_enabled("ETE_TRBE_MOCK_HAS_ETE");
    caps->has_trbe = mock_env_enabled("ETE_TRBE_MOCK_HAS_TRBE");
    caps->is_mock = true;
    caps->hardware_validated = false;
    return ETE_TRBE_OK;
#endif
}

int ete_trbe_alloc_buffer(unsigned int cpu, size_t size,
                          struct ete_trbe_buffer *buf)
{
    (void)cpu;
    void *mem = NULL;

    /*
     * The alignment check mirrors the granularity a real TRBE sink will need
     * while staying implementable with portable host allocation.
     */
    if (buf == NULL || size == 0 ||
        (size % ETE_TRBE_BUFFER_ALIGNMENT) != 0) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

    if (posix_memalign(&mem, ETE_TRBE_BUFFER_ALIGNMENT, size) != 0) {
        return ETE_TRBE_ERR_IO;
    }

    memset(mem, 0, size);
    memset(buf, 0, sizeof(*buf));
    buf->vaddr = mem;
    buf->paddr = 0;
    buf->size = size;
    buf->base = (uint64_t)(uintptr_t)mem;
    buf->limit = buf->base + size;
    buf->write_ptr = buf->base;
    buf->wrapped = false;

    return ETE_TRBE_OK;
}

void ete_trbe_free_buffer(struct ete_trbe_buffer *buf)
{
    if (buf == NULL) {
        return;
    }

    free(buf->vaddr);
    memset(buf, 0, sizeof(*buf));
}

size_t ete_trbe_buffer_valid_size(const struct ete_trbe_buffer *buf)
{
    if (buf == NULL || buf->size == 0 ||
        buf->write_ptr < buf->base || buf->write_ptr > buf->limit) {
        return 0;
    }

    if (buf->wrapped) {
        return buf->size;
    }

    return (size_t)(buf->write_ptr - buf->base);
}

int ete_trbe_copy_valid_trace(const struct ete_trbe_buffer *buf,
                              uint8_t *out, size_t out_size,
                              size_t *bytes_written)
{
    size_t valid;
    size_t head;
    size_t tail;
    size_t write_off;
    const uint8_t *base;

    if (bytes_written != NULL) {
        *bytes_written = 0;
    }

    if (buf == NULL || out == NULL || buf->vaddr == NULL ||
        buf->write_ptr < buf->base || buf->write_ptr > buf->limit) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

    valid = ete_trbe_buffer_valid_size(buf);
    if (out_size < valid) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

    base = (const uint8_t *)buf->vaddr;
    write_off = (size_t)(buf->write_ptr - buf->base);

    if (!buf->wrapped) {
        memcpy(out, base, valid);
    } else {
        /*
         * TRBE wrap means the oldest byte is at write_ptr. Copy tail then head
         * to produce the chronological byte stream expected by the decoder.
         */
        head = buf->size - write_off;
        tail = write_off;
        memcpy(out, base + write_off, head);
        memcpy(out + head, base, tail);
    }

    if (bytes_written != NULL) {
        *bytes_written = valid;
    }

    return ETE_TRBE_OK;
}

int ete_trbe_config_cpu(unsigned int cpu,
                        const struct ete_trbe_config *cfg,
                        const struct ete_trbe_buffer *buf)
{
    struct capture_slot *slot;

    if (cpu >= ETE_TRBE_MAX_CPUS || cfg == NULL || buf == NULL ||
        buf->vaddr == NULL || buf->size == 0) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    /*
     * Target config will eventually program TRBBASER/TRBLIMITR/TRBMAR and ETE
     * filters here. Until those encodings are cross-toolchain validated, target
     * builds fail explicitly instead of silently using host behavior.
     */
    return ETE_TRBE_ERR_UNSUPPORTED;
#else
    /* Mock config only snapshots the requested shape for start/stop tests. */
    slot = &g_capture_slots[cpu];
    if (slot->state == CAPTURE_RUNNING) {
        return ETE_TRBE_ERR_BAD_STATE;
    }

    memset(slot, 0, sizeof(*slot));
    slot->config = *cfg;
    slot->buffer = *buf;
    slot->state = CAPTURE_CONFIGURED;
    return ETE_TRBE_OK;
#endif
}

int ete_trbe_start_cpu(unsigned int cpu)
{
    struct capture_slot *slot;

    if (cpu >= ETE_TRBE_MAX_CPUS) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    /*
     * Target implementation must configure the sink before enabling ETE:
     * program TRBE, enable TRBLIMITR_EL1.E, synchronize, then set
     * TRCPRGCTLR.EN with bounded TRCSTATR polling.
     */
    return ETE_TRBE_ERR_UNSUPPORTED;
#else
    /* Mock start/stop validates lifecycle ordering without touching hardware. */
    slot = &g_capture_slots[cpu];
    if (slot->state != CAPTURE_CONFIGURED && slot->state != CAPTURE_STOPPED) {
        return ETE_TRBE_ERR_BAD_STATE;
    }

    slot->state = CAPTURE_RUNNING;
    return ETE_TRBE_OK;
#endif
}

int ete_trbe_stop_cpu(unsigned int cpu,
                      struct ete_trbe_result *result)
{
    struct capture_slot *slot;

    if (cpu >= ETE_TRBE_MAX_CPUS || result == NULL) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    /*
     * Target implementation must stop source before sink: disable program-flow
     * trace, ISB, TSB CSYNC, DSB, bounded TRCSTATR polling, then disable TRBE
     * and read final TRB state.
     */
    return ETE_TRBE_ERR_UNSUPPORTED;
#else
    slot = &g_capture_slots[cpu];
    if (slot->state != CAPTURE_RUNNING) {
        return ETE_TRBE_ERR_BAD_STATE;
    }

    /*
     * The mock result echoes the software buffer view. Real target code should
     * replace these with final TRB register reads after synchronization.
     */
    memset(&slot->result, 0, sizeof(slot->result));
    slot->result.trbptr = slot->buffer.write_ptr;
    slot->result.trblimitr = slot->buffer.limit;
    slot->result.trbbaser = slot->buffer.base;
    slot->result.wrapped = slot->buffer.wrapped;
    slot->result.valid_size = ete_trbe_buffer_valid_size(&slot->buffer);

    *result = slot->result;
    slot->state = CAPTURE_STOPPED;
    return ETE_TRBE_OK;
#endif
}

int ete_trbe_dump_cpu(unsigned int cpu,
                      const char *trace_path,
                      const char *meta_path)
{
    /* Persistent start/stop/dump is intentionally not wired yet. */
    (void)cpu;
    (void)trace_path;
    (void)meta_path;
    return ETE_TRBE_ERR_UNSUPPORTED;
}

int ete_trbe_write_metadata_json(char *out, size_t out_size,
                                 unsigned int cpu,
                                 const struct ete_trbe_caps *caps,
                                 const struct ete_trbe_buffer *buffer,
                                 const struct ete_trbe_result *result,
                                 const char *image_path)
{
    int written;
    const char *mode;
    const char *image;
    const char *warning;

    if (out == NULL || out_size == 0 || caps == NULL) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

    mode = caps->is_mock ? "mock" : "target";
    image = (image_path != NULL) ? image_path : "";
    /*
     * The warning array is part of the metadata contract: downstream tools and
     * reviewers can tell host/mock captures from hardware-validated captures.
     */
    warning = caps->is_mock ?
        "\"host mock only, hardware validation required\"" : "";

    written = snprintf(out, out_size,
                       "{\n"
                       "  \"format\": \"ete-trbe-raw-v1\",\n"
                       "  \"cpu\": %u,\n"
                       "  \"arch\": \"aarch64\",\n"
                       "  \"trace_source\": \"ETE\",\n"
                       "  \"trace_sink\": \"TRBE\",\n"
                       "  \"environment\": {\n"
                       "    \"capture_mode\": \"%s\",\n"
                       "    \"hardware_validated\": %s\n"
                       "  },\n"
                       "  \"buffer_size\": %zu,\n"
                       "  \"base\": \"0x%llx\",\n"
                       "  \"limit\": \"0x%llx\",\n"
                       "  \"write_ptr\": \"0x%llx\",\n"
                       "  \"wrapped\": %s,\n"
                       "  \"trbsr\": \"0x%llx\",\n"
                       "  \"trbidr\": \"0x%llx\",\n"
                       "  \"trcidr\": {\n"
                       "    \"TRCIDR0\": \"0x%llx\",\n"
                       "    \"TRCIDR1\": \"0x%llx\",\n"
                       "    \"TRCIDR2\": \"0x%llx\",\n"
                       "    \"TRCIDR3\": \"0x%llx\",\n"
                       "    \"TRCIDR4\": \"0x%llx\",\n"
                       "    \"TRCIDR5\": \"0x%llx\",\n"
                       "    \"TRCIDR6\": \"0x%llx\",\n"
                       "    \"TRCIDR7\": \"0x%llx\",\n"
                       "    \"TRCIDR8\": \"0x%llx\",\n"
                       "    \"TRCIDR9\": \"0x%llx\",\n"
                       "    \"TRCIDR10\": \"0x%llx\",\n"
                       "    \"TRCIDR11\": \"0x%llx\",\n"
                       "    \"TRCIDR12\": \"0x%llx\",\n"
                       "    \"TRCIDR13\": \"0x%llx\"\n"
                       "  },\n"
                       "  \"ete_config\": {\n"
                       "    \"TRCCONFIGR\": \"0x0\",\n"
                       "    \"TRCTRACEIDR\": \"0x0\",\n"
                       "    \"TRCSYNCPR\": \"0x0\",\n"
                       "    \"TRCVICTLR\": \"0x0\"\n"
                       "  },\n"
                       "  \"image\": {\n"
                       "    \"path\": \"%s\",\n"
                       "    \"load_base\": \"0x0\",\n"
                       "    \"text_vaddr\": \"0x0\",\n"
                       "    \"text_offset\": \"0x0\"\n"
                       "  },\n"
                       "  \"warnings\": [%s]\n"
                       "}\n",
                       cpu,
                       mode,
                       caps->hardware_validated ? "true" : "false",
                       buffer != NULL ? buffer->size : 0u,
                       (unsigned long long)(buffer != NULL ? buffer->base : 0u),
                       (unsigned long long)(buffer != NULL ? buffer->limit : 0u),
                       (unsigned long long)(buffer != NULL ? buffer->write_ptr : 0u),
                       (buffer != NULL && buffer->wrapped) ? "true" : "false",
                       (unsigned long long)(result != NULL ? result->trbsr : 0u),
                       (unsigned long long)caps->trbidr_el1,
                       (unsigned long long)caps->trcidr[0],
                       (unsigned long long)caps->trcidr[1],
                       (unsigned long long)caps->trcidr[2],
                       (unsigned long long)caps->trcidr[3],
                       (unsigned long long)caps->trcidr[4],
                       (unsigned long long)caps->trcidr[5],
                       (unsigned long long)caps->trcidr[6],
                       (unsigned long long)caps->trcidr[7],
                       (unsigned long long)caps->trcidr[8],
                       (unsigned long long)caps->trcidr[9],
                       (unsigned long long)caps->trcidr[10],
                       (unsigned long long)caps->trcidr[11],
                       (unsigned long long)caps->trcidr[12],
                       (unsigned long long)caps->trcidr[13],
                       image,
                       warning);

    if (written < 0 || (size_t)written >= out_size) {
        return ETE_TRBE_ERR_IO;
    }

    return written;
}
