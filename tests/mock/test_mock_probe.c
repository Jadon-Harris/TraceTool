#include "ete_trbe.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * probe override 测试。环境变量让 host 测试能覆盖“feature present”路径，
 * 但不表示完成了真实 ETE/TRBE 硬件验证。
 */

int main(void)
{
    struct ete_trbe_caps caps;
    if (setenv("ETE_TRBE_MOCK_HAS_ETE", "1", 1) != 0 ||
        setenv("ETE_TRBE_MOCK_HAS_TRBE", "1", 1) != 0) {
        fprintf(stderr, "setenv failed\n");
        return 1;
    }

    int rc = ete_trbe_probe_cpu(0, &caps);

    if (rc != 0) {
        fprintf(stderr, "mock probe failed: %d\n", rc);
        return 1;
    }

    if (!caps.is_mock || caps.hardware_validated) {
        fprintf(stderr, "host build must report mock and not hardware validated\n");
        return 1;
    }

    if (!caps.has_ete || !caps.has_trbe) {
        fprintf(stderr, "mock feature override did not set capabilities\n");
        return 1;
    }

    return 0;
}
