#include "ete_trbe.h"

#include "ete_trbe_regs.h"
#include "ete_trbe_sysreg.h"

#include <stdio.h>
#include <string.h>

const char *ete_trbe_version(void)
{
    return "ete-trbe-tool-0.1";
}

int ete_trbe_probe_cpu(unsigned int cpu, struct ete_trbe_caps *caps)
{
    (void)cpu;

    if (caps == NULL) {
        return -1;
    }

    memset(caps, 0, sizeof(*caps));

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    caps->is_mock = false;
    caps->hardware_validated = false;
    return -2;
#else
    caps->has_ete = false;
    caps->has_trbe = false;
    caps->is_mock = true;
    caps->hardware_validated = false;
    return 0;
#endif
}

int ete_trbe_alloc_buffer(unsigned int cpu, size_t size,
                          struct ete_trbe_buffer *buf)
{
    (void)cpu;
    (void)size;
    (void)buf;
    return -2;
}

int ete_trbe_config_cpu(unsigned int cpu,
                        const struct ete_trbe_config *cfg,
                        const struct ete_trbe_buffer *buf)
{
    (void)cpu;
    (void)cfg;
    (void)buf;
    return -2;
}

int ete_trbe_start_cpu(unsigned int cpu)
{
    (void)cpu;
    return -2;
}

int ete_trbe_stop_cpu(unsigned int cpu,
                      struct ete_trbe_result *result)
{
    (void)cpu;
    (void)result;
    return -2;
}

int ete_trbe_dump_cpu(unsigned int cpu,
                      const char *trace_path,
                      const char *meta_path)
{
    (void)cpu;
    (void)trace_path;
    (void)meta_path;
    return -2;
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
        return -1;
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
        return -2;
    }

    return written;
}
