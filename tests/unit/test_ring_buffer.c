#include "ete_trbe.h"

#include <stdint.h>
#include <stdio.h>

/*
 * TRBE ring-buffer linearize 单元测试。decoder 期望 trace 字节按时间顺序排列，
 * 因此 wrapped buffer 必须先拷 tail 再拷 head。
 */

static int check_bytes(const uint8_t *actual, const uint8_t *expected,
                       size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "byte %zu: got 0x%02x expected 0x%02x\n",
                    i, actual[i], expected[i]);
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    struct ete_trbe_buffer buffer;
    uint8_t *raw;
    uint8_t out[4096];
    size_t written = 0;
    uint8_t expected_wrap[] = { 4, 5, 6, 7, 0, 1, 2, 3 };
    uint8_t expected_nowrap[] = { 0, 1, 2, 3 };
    size_t i;

    if (ete_trbe_alloc_buffer(0, 4096, &buffer) != ETE_TRBE_OK) {
        fprintf(stderr, "buffer allocation failed\n");
        return 1;
    }

    raw = (uint8_t *)buffer.vaddr;
    for (i = 0; i < 8; ++i) {
        raw[i] = (uint8_t)i;
    }

    buffer.limit = buffer.base + 8;
    buffer.size = 8;
    buffer.write_ptr = buffer.base + 4;
    buffer.wrapped = false;

    /* 未 wrap：有效字节就是 [base, write_ptr)。 */
    if (ete_trbe_copy_valid_trace(&buffer, out, sizeof(out), &written) !=
        ETE_TRBE_OK ||
        written != 4 ||
        check_bytes(out, expected_nowrap, sizeof(expected_nowrap)) != 0) {
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    buffer.wrapped = true;

    /* 已 wrap：write_ptr 指向环形 buffer 中最老的字节。 */
    if (ete_trbe_copy_valid_trace(&buffer, out, sizeof(out), &written) !=
        ETE_TRBE_OK ||
        written != 8 ||
        check_bytes(out, expected_wrap, sizeof(expected_wrap)) != 0) {
        ete_trbe_free_buffer(&buffer);
        return 1;
    }

    ete_trbe_free_buffer(&buffer);
    return 0;
}
