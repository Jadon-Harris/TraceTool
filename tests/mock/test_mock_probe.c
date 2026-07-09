#include "ete_trbe.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Probe override test. Environment variables let host tests exercise the
 * "feature present" path without implying real ETE/TRBE hardware validation.
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
