#include "ete_trbe.h"

#include "ete_trbe_regs.h"
#include "ete_trbe_sysreg.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    (void)size;
    (void)buf;
    return ETE_TRBE_ERR_UNSUPPORTED;
}

int ete_trbe_config_cpu(unsigned int cpu,
                        const struct ete_trbe_config *cfg,
                        const struct ete_trbe_buffer *buf)
{
    (void)cpu;
    (void)cfg;
    (void)buf;
    return ETE_TRBE_ERR_UNSUPPORTED;
}

int ete_trbe_start_cpu(unsigned int cpu)
{
    (void)cpu;
    return ETE_TRBE_ERR_UNSUPPORTED;
}

int ete_trbe_stop_cpu(unsigned int cpu,
                      struct ete_trbe_result *result)
{
    (void)cpu;
    (void)result;
    return ETE_TRBE_ERR_UNSUPPORTED;
}

int ete_trbe_dump_cpu(unsigned int cpu,
                      const char *trace_path,
                      const char *meta_path)
{
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

    if (out == NULL || out_size == 0 || caps == NULL) {
        return ETE_TRBE_ERR_INVALID_ARGUMENT;
    }

    mode = caps->is_mock ? "mock" : "target";
    image = (image_path != NULL) ? image_path : "";

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
                       "  \"image\": {\n"
                       "    \"path\": \"%s\"\n"
                       "  },\n"
                       "  \"warnings\": []\n"
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
                       image);

    if (written < 0 || (size_t)written >= out_size) {
        return ETE_TRBE_ERR_IO;
    }

    return written;
}
