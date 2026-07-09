#include "ete_trbe.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: ete_trace <command> [options]\n"
            "\n"
            "Commands:\n"
            "  probe        Show ETE/TRBE capability status for CPU0\n"
            "  record       Capture one trace window and write raw/meta files\n"
            "  status       Show process-local capture status\n"
            "  version      Show tool version\n"
            "\n"
            "Default builds use host/mock mode. Real ETE/TRBE access is only\n"
            "compiled when ETE_TRBE_TARGET=ON is set for an AArch64 target build.\n");
}

struct record_args {
    unsigned int cpu;
    size_t size;
    unsigned int duration_ms;
    const char *image;
    const char *out;
    const char *meta;
};

/* 解析 64K、1M 这类面向用户的 buffer size 写法。 */
static int parse_size_arg(const char *text, size_t *size)
{
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);

    if (end == text || value == 0) {
        return 1;
    }

    if (strcmp(end, "K") == 0 || strcmp(end, "k") == 0) {
        value *= 1024ull;
    } else if (strcmp(end, "M") == 0 || strcmp(end, "m") == 0) {
        value *= 1024ull * 1024ull;
    } else if (*end != '\0') {
        return 1;
    }

    *size = (size_t)value;
    return 0;
}

static int write_bytes_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        perror(path);
        return 1;
    }

    if (size != 0 && fwrite(data, 1, size, file) != size) {
        perror(path);
        fclose(file);
        return 1;
    }

    if (fclose(file) != 0) {
        perror(path);
        return 1;
    }

    return 0;
}

/* metadata 写入和 raw 写入分开，record 出错清理会更直接。 */
static int write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);

    if (file == NULL) {
        perror(path);
        return 1;
    }

    if (fwrite(text, 1, size, file) != size) {
        perror(path);
        fclose(file);
        return 1;
    }

    if (fclose(file) != 0) {
        perror(path);
        return 1;
    }

    return 0;
}

static int parse_record_args(int argc, char **argv, struct record_args *args)
{
    int i;

    /* 默认值刻意保持 host-safe；输出路径必须显式传入。 */
    args->cpu = 0;
    args->size = 1024u * 1024u;
    args->duration_ms = 0;
    args->image = "";
    args->out = NULL;
    args->meta = NULL;

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            char extra;
            if (sscanf(argv[++i], "%u%c", &args->cpu, &extra) != 1) {
                fprintf(stderr, "Invalid --cpu value\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (parse_size_arg(argv[++i], &args->size) != 0) {
                fprintf(stderr, "Invalid --size value\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--duration-ms") == 0 && i + 1 < argc) {
            char extra;
            if (sscanf(argv[++i], "%u%c", &args->duration_ms, &extra) != 1) {
                fprintf(stderr, "Invalid --duration-ms value\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            args->image = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            args->out = argv[++i];
        } else if (strcmp(argv[i], "--meta") == 0 && i + 1 < argc) {
            args->meta = argv[++i];
        } else {
            fprintf(stderr, "Unknown or incomplete record option: %s\n", argv[i]);
            return 2;
        }
    }

    if (args->out == NULL || args->meta == NULL) {
        fprintf(stderr, "record requires --out and --meta\n");
        return 2;
    }

    return 0;
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
    /*
     * mock 输出必须足够醒目，避免日志被误认为是目标板能力探测。
     */
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

static int cmd_record(int argc, char **argv)
{
    struct record_args args;
    struct ete_trbe_caps caps;
    struct ete_trbe_buffer buffer;
    struct ete_trbe_config config;
    struct ete_trbe_result result;
    uint8_t *linear = NULL;
    size_t bytes = 0;
    char metadata[ETE_TRBE_METADATA_MIN_CAPACITY];
    int rc;

    rc = parse_record_args(argc, argv, &args);
    if (rc != 0) {
        return rc;
    }

    /*
     * record 是 one-shot：allocate、configure、start、stop、linearize、write。
     * 当前还没有跨进程 capture 状态，status 命令会明确说明这一点。
     */
    rc = ete_trbe_probe_cpu(args.cpu, &caps);
    if (rc != ETE_TRBE_OK && rc != ETE_TRBE_ERR_UNSUPPORTED) {
        fprintf(stderr, "probe failed: %s (%d)\n", ete_trbe_strerror(rc), rc);
        return 1;
    }

    rc = ete_trbe_alloc_buffer(args.cpu, args.size, &buffer);
    if (rc != ETE_TRBE_OK) {
        fprintf(stderr, "buffer allocation failed: %s (%d)\n",
                ete_trbe_strerror(rc), rc);
        return 1;
    }

    /*
     * 这些默认值让 CLI 形状贴近 target capture，同时在 host/mock 模式下无害。
     */
    memset(&config, 0, sizeof(config));
    config.cpu = args.cpu;
    config.buffer_size = args.size;
    config.trace_id = 1;
    config.trace_el0 = true;
    config.trace_el1 = true;

    rc = ete_trbe_config_cpu(args.cpu, &config, &buffer);
    if (rc == ETE_TRBE_OK) {
        rc = ete_trbe_start_cpu(args.cpu);
    }
    if (rc == ETE_TRBE_OK) {
        /*
         * mock 构建不 sleep，也不合成 trace。target 构建后续会把 duration_ms
         * 用在真实硬件 start/stop 序列周围。
         */
        (void)args.duration_ms;
        rc = ete_trbe_stop_cpu(args.cpu, &result);
    }
    if (rc != ETE_TRBE_OK) {
        fprintf(stderr, "record failed: %s (%d)\n", ete_trbe_strerror(rc), rc);
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    /*
     * 按整个 buffer 分配输出空间，这样 wrapped capture 可以直接 linearize，
     * 不需要第二次 sizing pass。
     */
    linear = malloc(buffer.size == 0 ? 1 : buffer.size);
    if (linear == NULL) {
        fprintf(stderr, "trace output allocation failed\n");
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    rc = ete_trbe_copy_valid_trace(&buffer, linear, buffer.size, &bytes);
    if (rc != ETE_TRBE_OK) {
        fprintf(stderr, "trace linearize failed: %s (%d)\n",
                ete_trbe_strerror(rc), rc);
        free(linear);
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    rc = ete_trbe_write_metadata_json(metadata, sizeof(metadata), args.cpu,
                                      &caps, &buffer, &result, args.image);
    if (rc < 0) {
        fprintf(stderr, "metadata generation failed: %s (%d)\n",
                ete_trbe_strerror(rc), rc);
        free(linear);
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    if (write_bytes_file(args.out, linear, bytes) != 0 ||
        write_text_file(args.meta, metadata) != 0) {
        free(linear);
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    printf("CPU%u:\n", args.cpu);
    printf("  raw trace: %s (%zu bytes)\n", args.out, bytes);
    printf("  metadata: %s\n", args.meta);
    printf("  Status: %s\n",
           caps.is_mock ? "host mock only, hardware validation required" :
                          "target capture completed, hardware validation required");

    free(linear);
    ete_trbe_free_buffer(&buffer);
    return 0;
}

static int cmd_status(void)
{
    /* status 刻意讲实话：目前还没有 daemon 或持久 capture。 */
    printf("Status: process-local CLI skeleton\n");
    printf("Capture persistence: not implemented; use record for one-shot capture\n");
    printf("Hardware validation: required for real ETE/TRBE\n");
    return 0;
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

    if (strcmp(argv[1], "record") == 0) {
        return cmd_record(argc, argv);
    }

    if (strcmp(argv[1], "status") == 0) {
        return cmd_status();
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
