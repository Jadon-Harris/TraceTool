#include "ete_trbe_sysreg.h"

#include <stdio.h>

/*
 * sysreg 边界测试。默认 host 构建即使运行在 AArch64 Mac 上，也绝不能汇编
 * 或执行真实 trace-register 访问。
 */

int main(void)
{
    uint64_t value = 0xdeadbeef;

    if (ete_trbe_sysreg_real_target_enabled()) {
        fprintf(stderr, "default Mac build must not enable real sysregs\n");
        return 1;
    }

    if (ete_trbe_sysreg_read(ETE_TRBE_SYSREG_ID_AA64DFR0_EL1, &value) != -2) {
        fprintf(stderr, "mock sysreg read must report unsupported\n");
        return 1;
    }

    if (value != 0) {
        fprintf(stderr, "mock sysreg read should zero the output value\n");
        return 1;
    }

    if (ete_trbe_sysreg_write(ETE_TRBE_SYSREG_TRFCR_EL1, 0) != -2) {
        fprintf(stderr, "mock sysreg write must report unsupported\n");
        return 1;
    }

    /* host barrier wrapper 刻意是 no-op，但仍应保持可调用。 */
    ete_trbe_isb();
    ete_trbe_dsb_sy();
    ete_trbe_tsb_csync();

    return 0;
}
