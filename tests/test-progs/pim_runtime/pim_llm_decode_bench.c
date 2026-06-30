#include <stdio.h>
#include <stdint.h>
#include "pim_runtime.h"

#define NUM_LAYERS 36
#define HIDDEN_SIZE 2048
#define INTERMEDIATE_SIZE 11008

static void run_one_layer(int layer)
{
    uint64_t base = layer * 1000;

    /*
     * 这些 row 编号只是第一阶段抽象映射。
     * 后续你可以把它们换成真实 weight row / activation row。
     */

    // Q/K/V/O projection
    pim_gemv_rows(base + 0, base + 1, base + 2,
                  HIDDEN_SIZE, HIDDEN_SIZE, 1);

    pim_gemv_rows(base + 10, base + 11, base + 12,
                  HIDDEN_SIZE, HIDDEN_SIZE, 1);

    pim_gemv_rows(base + 20, base + 21, base + 22,
                  HIDDEN_SIZE, HIDDEN_SIZE, 1);

    pim_gemv_rows(base + 30, base + 31, base + 32,
                  HIDDEN_SIZE, HIDDEN_SIZE, 1);

    // FFN gate/up/down
    pim_gemv_rows(base + 40, base + 41, base + 42,
                  INTERMEDIATE_SIZE, HIDDEN_SIZE, 1);

    pim_gemv_rows(base + 50, base + 51, base + 52,
                  INTERMEDIATE_SIZE, HIDDEN_SIZE, 1);

    pim_mul_rows(base + 42, base + 52, base + 62,
                 INTERMEDIATE_SIZE);

    pim_gemv_rows(base + 60, base + 62, base + 63,
                  HIDDEN_SIZE, INTERMEDIATE_SIZE, 1);

    // residual add
    pim_add_rows(base + 63, base + 64, base + 65,
                 HIDDEN_SIZE);
}

int main()
{
    int output_tokens = 16;

    printf("[LLM-PIM] Start decode benchmark\n");

    for (int t = 0; t < output_tokens; t++) {
        for (int l = 0; l < NUM_LAYERS; l++) {
            run_one_layer(l);
        }
    }

    printf("[LLM-PIM] Decode benchmark done\n");

    return 0;
}