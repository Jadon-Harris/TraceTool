#include "ete_trbe.h"

#include <stdio.h>
#include <string.h>

/*
 * metadata 是 ete_trace 与 ete_decode 之间的合约。这个测试固定 host/mock
 * review 和后续 target capture 都关心的字段。
 */

static int require_contains(const char *haystack, const char *needle)
{
    if (strstr(haystack, needle) == NULL) {
        fprintf(stderr, "missing JSON fragment: %s\n", needle);
        return 1;
    }
    return 0;
}

int main(void)
{
    struct ete_trbe_caps caps;
    struct ete_trbe_buffer buffer;
    struct ete_trbe_result result;
    char json[ETE_TRBE_METADATA_MIN_CAPACITY];
    int rc;

    memset(&caps, 0, sizeof(caps));
    memset(&buffer, 0, sizeof(buffer));
    memset(&result, 0, sizeof(result));

    caps.is_mock = true;
    caps.hardware_validated = false;
    buffer.size = 4096;
    buffer.base = 0x1000;
    buffer.limit = 0x2000;
    buffer.write_ptr = 0x1800;
    buffer.wrapped = false;
    result.trbsr = 0;

    rc = ete_trbe_write_metadata_json(json, sizeof(json), 0, &caps, &buffer,
                                      &result, "app.elf");
    if (rc <= 0) {
        fprintf(stderr, "metadata writer failed: %d\n", rc);
        return 1;
    }

    /* 硬件验证状态和寄存器占位字段必须显式可见。 */
    if (require_contains(json, "\"format\": \"ete-trbe-raw-v1\"") != 0 ||
        require_contains(json, "\"capture_mode\": \"mock\"") != 0 ||
        require_contains(json, "\"hardware_validated\": false") != 0 ||
        require_contains(json, "\"TRCIDR13\": \"0x0\"") != 0 ||
        require_contains(json, "\"TRCCONFIGR\": \"0x0\"") != 0 ||
        require_contains(json, "\"text_offset\": \"0x0\"") != 0 ||
        require_contains(json, "host mock only") != 0 ||
        require_contains(json, "\"path\": \"app.elf\"") != 0) {
        return 1;
    }

    return 0;
}
