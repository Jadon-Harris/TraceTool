#ifndef ETE_TRBE_SYSREG_H
#define ETE_TRBE_SYSREG_H

#include "ete_trbe_regs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum ete_trbe_sysreg_id {
    ETE_TRBE_SYSREG_ID_AA64DFR0_EL1 = 0,
    ETE_TRBE_SYSREG_TRFCR_EL1,
    ETE_TRBE_SYSREG_TRFCR_EL2,
    ETE_TRBE_SYSREG_TRBBASER_EL1,
    ETE_TRBE_SYSREG_TRBLIMITR_EL1,
    ETE_TRBE_SYSREG_TRBPTR_EL1,
    ETE_TRBE_SYSREG_TRBSR_EL1,
    ETE_TRBE_SYSREG_TRBMAR_EL1,
    ETE_TRBE_SYSREG_TRBIDR_EL1,
    ETE_TRBE_SYSREG_TRBTRG_EL1,
    ETE_TRBE_SYSREG_TRCPRGCTLR,
    ETE_TRBE_SYSREG_TRCSTATR,
    ETE_TRBE_SYSREG_TRCCONFIGR,
    ETE_TRBE_SYSREG_TRCTRACEIDR,
    ETE_TRBE_SYSREG_TRCSYNCPR,
    ETE_TRBE_SYSREG_TRCVICTLR,
    ETE_TRBE_SYSREG_TRCSTALLCTLR
};

/*
 * 真实寄存器访问边界的统一判断。条件后半部分是架构，但 ETE_TRBE_TARGET
 * 才是关键安全锁：ARM64 host 机器仍必须编译 mock 路径。
 */
static inline bool ete_trbe_sysreg_real_target_enabled(void)
{
#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)

/*
 * 只有编译器/工具链认识架构寄存器名时才使用 named sysreg 访问。
 * implementation-defined 的 TRC/TRB encoding 在目标工具链验证完成前继续关着。
 */
#define ETE_TRBE_READ_SYSREG_NAMED(name)             \
    ({                                               \
        uint64_t _value;                             \
        __asm__ volatile("mrs %0, " #name            \
                         : "=r"(_value));            \
        _value;                                      \
    })

#define ETE_TRBE_WRITE_SYSREG_NAMED(name, value)     \
    do {                                             \
        uint64_t _value = (uint64_t)(value);          \
        __asm__ volatile("msr " #name ", %0"         \
                         :                           \
                         : "r"(_value));             \
    } while (0)

static inline void ete_trbe_isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

static inline void ete_trbe_dsb_sy(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void ete_trbe_tsb_csync(void)
{
    __asm__ volatile("tsb csync" ::: "memory");
}

#else

/* host barrier 是 no-op，默认测试不会汇编 target-only 指令。 */
static inline void ete_trbe_isb(void)
{
}

static inline void ete_trbe_dsb_sy(void)
{
}

static inline void ete_trbe_tsb_csync(void)
{
}

#endif

static inline int ete_trbe_sysreg_read(enum ete_trbe_sysreg_id reg,
                                       uint64_t *value)
{
    if (value == NULL) {
        return -1;
    }

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    switch (reg) {
    case ETE_TRBE_SYSREG_ID_AA64DFR0_EL1:
        *value = ETE_TRBE_READ_SYSREG_NAMED(ID_AA64DFR0_EL1);
        return 0;
    default:
        /*
         * 当前阶段刻意把 TRC/TRB implementation register 访问挡在这个 API 后面。
         * 等目标工具链能验证具体 encoding 后，再和 target probe 一起打开。
         */
        return -2;
    }
#else
    /*
     * host 读返回 unsupported 且把值清零，使 probe 确定可重复，也让误用
     * target-only 寄存器访问能在测试里暴露出来。
     */
    (void)reg;
    *value = 0;
    return -2;
#endif
}

static inline int ete_trbe_sysreg_write(enum ete_trbe_sysreg_id reg,
                                        uint64_t value)
{
#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    switch (reg) {
    case ETE_TRBE_SYSREG_TRFCR_EL1:
        ETE_TRBE_WRITE_SYSREG_NAMED(TRFCR_EL1, value);
        return 0;
    default:
        return -2;
    }
#else
    /* host 构建绝不写架构 trace 寄存器。 */
    (void)reg;
    (void)value;
    return -2;
#endif
}

#endif
