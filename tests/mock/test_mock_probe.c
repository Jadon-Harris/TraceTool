#include "ete_trbe.h"

#include <stdio.h>

int main(void)
{
    struct ete_trbe_caps caps;
    int rc = ete_trbe_probe_cpu(0, &caps);

    if (rc != 0) {
        fprintf(stderr, "mock probe failed: %d\n", rc);
        return 1;
    }

    if (!caps.is_mock || caps.hardware_validated) {
        fprintf(stderr, "host build must report mock and not hardware validated\n");
        return 1;
    }

    return 0;
}
