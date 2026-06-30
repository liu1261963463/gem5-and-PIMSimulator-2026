#ifndef __PIM_RUNTIME_H__
#define __PIM_RUNTIME_H__

#include <stdint.h>

#define PIM_BASE        0x2F000000UL

#define PIM_REG_CMD        0x000
#define PIM_REG_DIM        0x008
#define PIM_REG_INPUT0_ROW 0x010
#define PIM_REG_INPUT1_ROW 0x018
#define PIM_REG_RESULT_ROW 0x020
#define PIM_REG_BANK       0x028
#define PIM_REG_START      0x030
#define PIM_REG_STATUS     0x038
#define PIM_REG_LAST_CYC   0x040

#define PIM_REG_BATCH      0x048
#define PIM_REG_INPUT_DIM  0x050
#define PIM_REG_OUTPUT_DIM 0x058

#define PIM_OP_NOP   0
#define PIM_OP_ADD   1
#define PIM_OP_MUL   2
#define PIM_OP_RELU  3
#define PIM_OP_GEMV  4

#define PIM_BANK_ALL  0
#define PIM_BANK_EVEN 1
#define PIM_BANK_ODD  2

#define PIM_STATUS_DONE  0x1
#define PIM_STATUS_BUSY  0x2
#define PIM_STATUS_ERROR 0x4

static volatile uint64_t *pim_mmio = (volatile uint64_t *)PIM_BASE;

static inline void pim_write(uint64_t offset, uint64_t value)
{
    pim_mmio[offset / 8] = value;
}

static inline uint64_t pim_read(uint64_t offset)
{
    return pim_mmio[offset / 8];
}

static inline void pim_wait_done(void)
{
    while ((pim_read(PIM_REG_STATUS) & PIM_STATUS_DONE) == 0) {
        ;
    }
}

static inline void pim_eltwise_rows(uint64_t opcode,
                                    uint64_t dim,
                                    uint64_t input0_row,
                                    uint64_t input1_row,
                                    uint64_t result_row)
{
    pim_write(PIM_REG_CMD, opcode);
    pim_write(PIM_REG_DIM, dim);
    pim_write(PIM_REG_INPUT0_ROW, input0_row);
    pim_write(PIM_REG_INPUT1_ROW, input1_row);
    pim_write(PIM_REG_RESULT_ROW, result_row);
    pim_write(PIM_REG_BANK, PIM_BANK_ALL);
    pim_write(PIM_REG_START, 1);
    pim_wait_done();
}

static inline void pim_add_rows(uint64_t dim,
                                uint64_t input0_row,
                                uint64_t input1_row,
                                uint64_t result_row)
{
    pim_eltwise_rows(PIM_OP_ADD, dim, input0_row, input1_row, result_row);
}

static inline void pim_mul_rows(uint64_t dim,
                                uint64_t input0_row,
                                uint64_t input1_row,
                                uint64_t result_row)
{
    pim_eltwise_rows(PIM_OP_MUL, dim, input0_row, input1_row, result_row);
}

static inline void pim_relu_rows(uint64_t dim,
                                 uint64_t input0_row,
                                 uint64_t result_row)
{
    pim_eltwise_rows(PIM_OP_RELU, dim, input0_row, 0, result_row);
}

static inline void pim_gemv_dummy(uint64_t batch,
                                  uint64_t input_dim,
                                  uint64_t output_dim)
{
    pim_write(PIM_REG_CMD, PIM_OP_GEMV);
    pim_write(PIM_REG_BATCH, batch);
    pim_write(PIM_REG_INPUT_DIM, input_dim);
    pim_write(PIM_REG_OUTPUT_DIM, output_dim);
    pim_write(PIM_REG_START, 1);
    pim_wait_done();
}

#endif