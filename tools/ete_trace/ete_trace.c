#include "ete_trbe.h"

#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: ete_trace <command> [options]\n"
            "\n"
            "Commands:\n"
            "  probe        Show ETE/TRBE capability status for CPU0\n"
            "  version      Show tool version\n"
            "\n"
            "Default builds use host/mock mode. Real ETE/TRBE access is only\n"
            "compiled when ETE_TRBE_TARGET=ON is set for an AArch64 target build.\n");
}

static int cmd_probe(void)
{
    struct ete_trbe_caps caps;
    int rc = ete_trbe_probe_cpu(0, &caps);

    if (rc != 0) {
        fprintf(stderr, "CPU0:\n  Status: probe failed, rc=%d\n", rc);
        return 1;
    }

    printf("CPU0:\n");
    if (caps.is_mock) {
        printf("  FEAT_ETE: mock\n");
        printf("  FEAT_TRBE: mock\n");
        printf("  Status: host mock only, hardware validation required\n");
    } else {
        printf("  FEAT_ETE: %s\n", caps.has_ete ? "supported" : "not supported");
        printf("  FEAT_TRBE: %s\n", caps.has_trbe ? "supported" : "not supported");
        printf("  TraceVer: 0x%llx\n", (unsigned long long)caps.id_aa64dfr0_el1);
        printf("  TraceBuffer: 0x%llx\n", (unsigned long long)caps.trbidr_el1);
        printf("  Status: target build, hardware validation required\n");
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "probe") == 0) {
        return cmd_probe();
    }

    if (strcmp(argv[1], "version") == 0) {
        puts(ete_trbe_version());
        return 0;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }

    fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
    print_usage(stderr);
    return 2;
}
