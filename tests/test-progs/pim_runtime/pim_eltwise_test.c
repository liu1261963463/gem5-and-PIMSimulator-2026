#include <stdio.h>
#include <stdint.h>

#include "pim_runtime.h"

int main(void)
{
    /*
     * 这里传的是 PIMSimulator row/tile 参数。
     * 具体 row 值需要与你在 PIMSimulator 中的数据放置策略一致。
     */
    uint64_t input0_row = 0;
    uint64_t input1_row = 1;
    uint64_t output_row = 2;
    uint64_t num_tile = 16;

    printf("[PIM TEST] Start PIM ADD\n");

    int ret = pim_add_rows(input0_row, input1_row, output_row, num_tile);

    if (ret != 0) {
        printf("[PIM TEST] PIM ADD failed, err=%lu\n", pim_error());
        return 1;
    }

    printf("[PIM TEST] PIM ADD done\n");

    printf("[PIM TEST] Start PIM MUL\n");

    ret = pim_mul_rows(input0_row, input1_row, output_row, num_tile);

    if (ret != 0) {
        printf("[PIM TEST] PIM MUL failed, err=%lu\n", pim_error());
        return 1;
    }

    printf("[PIM TEST] PIM MUL done\n");

    printf("[PIM TEST] Start PIM RELU\n");

    ret = pim_relu_rows(input0_row, output_row, num_tile);

    if (ret != 0) {
        printf("[PIM TEST] PIM RELU failed, err=%lu\n", pim_error());
        return 1;
    }

    printf("[PIM TEST] PIM RELU done\n");

    return 0;
}