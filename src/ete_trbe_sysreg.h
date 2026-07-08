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

static inline bool ete_trbe_sysreg_real_target_enabled(void)
{
#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

#if defined(ETE_TRBE_TARGET) && defined(__aarch64__)

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
         * Stage 2 intentionally keeps TRC/TRB implementation register access
         * behind this API. Concrete encodings are enabled alongside the target
         * probe once they can be validated with the target toolchain.
         */
        return -2;
    }
#else
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
    (void)reg;
    (void)value;
    return -2;
#endif
}

#endif
