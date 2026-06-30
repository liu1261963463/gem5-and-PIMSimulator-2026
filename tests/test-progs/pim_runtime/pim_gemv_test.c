#include <stdio.h>
#include <stdint.h>

#include "pim_runtime.h"

int main(void)
{
    uint64_t weight_row = 0;
    uint64_t input_row = 64;
    uint64_t output_row = 128;

    uint32_t m = 128;
    uint32_t n = 128;

    uint64_t num_tile = 16;

    printf("[PIM TEST] Start PIM GEMV\n");

    int ret = pim_gemv_rows(
        weight_row,
        input_row,
        output_row,
        m,
        n,
        num_tile
    );

    if (ret != 0) {
        printf("[PIM TEST] PIM GEMV failed, err=%lu\n", pim_error());
        return 1;
    }

    printf("[PIM TEST] PIM GEMV done\n");

    return 0;
}