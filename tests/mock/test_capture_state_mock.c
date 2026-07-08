#include "ete_trbe.h"

#include <stdio.h>

int main(void)
{
    struct ete_trbe_buffer buffer;
    struct ete_trbe_config config;
    struct ete_trbe_result result;

    if (ete_trbe_start_cpu(0) != ETE_TRBE_ERR_BAD_STATE) {
        fprintf(stderr, "start before config should fail with bad state\n");
        return 1;
    }

    if (ete_trbe_alloc_buffer(0, 4096, &buffer) != ETE_TRBE_OK) {
        fprintf(stderr, "buffer allocation failed\n");
        return 1;
    }

    config.cpu = 0;
    config.buffer_size = buffer.size;
    config.trace_id = 1;
    config.trace_el0 = true;
    config.trace_el1 = true;
    config.enable_cycle_count = false;
    config.enable_timestamp = false;
    config.use_addr_range = false;
    config.range_start = 0;
    config.range_end = 0;

    buffer.write_ptr = buffer.base + 128;

    if (ete_trbe_config_cpu(0, &config, &buffer) != ETE_TRBE_OK) {
        fprintf(stderr, "config failed\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    if (ete_trbe_start_cpu(0) != ETE_TRBE_OK) {
        fprintf(stderr, "start failed\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    if (ete_trbe_start_cpu(0) != ETE_TRBE_ERR_BAD_STATE) {
        fprintf(stderr, "double start should fail with bad state\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    if (ete_trbe_stop_cpu(0, &result) != ETE_TRBE_OK) {
        fprintf(stderr, "stop failed\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    if (result.trbbaser != buffer.base ||
        result.trblimitr != buffer.limit ||
        result.trbptr != buffer.write_ptr ||
        result.valid_size != 128) {
        fprintf(stderr, "unexpected stop result\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    if (ete_trbe_stop_cpu(0, &result) != ETE_TRBE_ERR_BAD_STATE) {
        fprintf(stderr, "double stop should fail with bad state\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    ete_trbe_free_buffer(&buffer);
    return 0;
}
