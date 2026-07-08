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

static int parse_cpu_arg(int argc, char **argv, unsigned int *cpu)
{
    int i;

    *cpu = 0;

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--cpu") == 0) {
            char extra;
            if (i + 1 >= argc ||
                sscanf(argv[i + 1], "%u%c", cpu, &extra) != 1) {
                fprintf(stderr, "Invalid --cpu value\n");
                return 2;
            }
            ++i;
            continue;
        }

        fprintf(stderr, "Unknown probe option: %s\n", argv[i]);
        return 2;
    }

    return 0;
}

static int cmd_probe(int argc, char **argv)
{
    struct ete_trbe_caps caps;
    unsigned int cpu;
    int rc = parse_cpu_arg(argc, argv, &cpu);

    if (rc != 0) {
        return rc;
    }

    rc = ete_trbe_probe_cpu(cpu, &caps);

    if (rc != 0 && rc != ETE_TRBE_ERR_UNSUPPORTED) {
        fprintf(stderr, "CPU%u:\n  Status: probe failed: %s (%d)\n",
                cpu, ete_trbe_strerror(rc), rc);
        return 1;
    }

    printf("CPU%u:\n", cpu);
    if (caps.is_mock) {
        printf("  FEAT_ETE: %s\n", caps.has_ete ? "mock supported" : "mock");
        printf("  FEAT_TRBE: %s\n", caps.has_trbe ? "mock supported" : "mock");
        printf("  Status: host mock only, hardware validation required\n");
    } else {
        printf("  FEAT_ETE: %s\n", caps.has_ete ? "supported" : "not supported");
        printf("  FEAT_TRBE: %s\n", caps.has_trbe ? "supported" : "not supported");
        printf("  TraceVer: 0x%llx\n", (unsigned long long)caps.id_aa64dfr0_el1);
        printf("  TraceBuffer: 0x%llx\n", (unsigned long long)caps.trbidr_el1);
        printf("  Status: %s\n",
               rc == 0 ? "usable, hardware validation required" :
                         "not usable, hardware validation required");
    }

    return rc == ETE_TRBE_ERR_UNSUPPORTED ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "probe") == 0) {
        return cmd_probe(argc, argv);
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
