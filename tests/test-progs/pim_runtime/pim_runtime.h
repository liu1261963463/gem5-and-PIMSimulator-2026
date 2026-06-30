#ifndef PIM_RUNTIME_H
#define PIM_RUNTIME_H

#include <stdint.h>

#define PIM_CMD_GEMV 1
#define PIM_CMD_ADD  2
#define PIM_CMD_MUL  3
#define PIM_CMD_RELU 4

#define PIM_STATUS_BUSY  (1ULL << 0)
#define PIM_STATUS_DONE  (1ULL << 1)
#define PIM_STATUS_ERROR (1ULL << 2)

#define PIM_REG_CMD       0x00
#define PIM_REG_STATUS    0x08
#define PIM_REG_SRC0      0x10
#define PIM_REG_SRC1      0x18
#define PIM_REG_DST       0x20
#define PIM_REG_NUM_TILE  0x28
#define PIM_REG_M         0x30
#define PIM_REG_N         0x38
#define PIM_REG_K         0x40
#define PIM_REG_START     0x48
#define PIM_REG_CLEAR     0x50
#define PIM_REG_ERR       0x58

/*
 * SE 模式下，建议在 se.py 中把 0xFFFF0000 映射到 0x2F000000。
 * FS 模式下，建议通过 /dev/mem mmap 0x2F000000。
 */
#ifndef PIM_MMIO_BASE
#define PIM_MMIO_BASE 0xFFFF0000UL
#endif

static inline volatile uint64_t *
pim_reg(uint64_t offset)
{
    return (volatile uint64_t *)(PIM_MMIO_BASE + offset);
}

static inline void
pim_write(uint64_t offset, uint64_t value)
{
    *pim_reg(offset) = value;
}

static inline uint64_t
pim_read(uint64_t offset)
{
    return *pim_reg(offset);
}

static inline void
pim_wait_done(void)
{
    while (1) {
        uint64_t status = pim_read(PIM_REG_STATUS);

        if (status & PIM_STATUS_ERROR) {
            break;
        }

        if (status & PIM_STATUS_DONE) {
            break;
        }
    }
}

static inline uint64_t
pim_error(void)
{
    return pim_read(PIM_REG_ERR);
}

static inline void
pim_clear(void)
{
    pim_write(PIM_REG_CLEAR, 1);
}

/*
 * row 版本接口：直接匹配 PIMSimulator PIMKernel 的 row/tile 参数。
 */
static inline int
pim_add_rows(uint64_t input0_row,
             uint64_t input1_row,
             uint64_t output_row,
             uint64_t num_tile)
{
    pim_clear();

    pim_write(PIM_REG_SRC0, input0_row);
    pim_write(PIM_REG_SRC1, input1_row);
    pim_write(PIM_REG_DST, output_row);
    pim_write(PIM_REG_NUM_TILE, num_tile);
    pim_write(PIM_REG_CMD, PIM_CMD_ADD);
    pim_write(PIM_REG_START, 1);

    pim_wait_done();

    return (pim_read(PIM_REG_STATUS) & PIM_STATUS_ERROR) ? -1 : 0;
}

static inline int
pim_mul_rows(uint64_t input0_row,
             uint64_t input1_row,
             uint64_t output_row,
             uint64_t num_tile)
{
    pim_clear();

    pim_write(PIM_REG_SRC0, input0_row);
    pim_write(PIM_REG_SRC1, input1_row);
    pim_write(PIM_REG_DST, output_row);
    pim_write(PIM_REG_NUM_TILE, num_tile);
    pim_write(PIM_REG_CMD, PIM_CMD_MUL);
    pim_write(PIM_REG_START, 1);

    pim_wait_done();

    return (pim_read(PIM_REG_STATUS) & PIM_STATUS_ERROR) ? -1 : 0;
}

static inline int
pim_relu_rows(uint64_t input_row,
              uint64_t output_row,
              uint64_t num_tile)
{
    pim_clear();

    pim_write(PIM_REG_SRC0, input_row);
    pim_write(PIM_REG_DST, output_row);
    pim_write(PIM_REG_NUM_TILE, num_tile);
    pim_write(PIM_REG_CMD, PIM_CMD_RELU);
    pim_write(PIM_REG_START, 1);

    pim_wait_done();

    return (pim_read(PIM_REG_STATUS) & PIM_STATUS_ERROR) ? -1 : 0;
}

static inline int
pim_gemv_rows(uint64_t weight_row,
              uint64_t input_row,
              uint64_t output_row,
              uint32_t m,
              uint32_t n,
              uint64_t num_tile)
{
    pim_clear();

    pim_write(PIM_REG_SRC0, weight_row);
    pim_write(PIM_REG_SRC1, input_row);
    pim_write(PIM_REG_DST, output_row);
    pim_write(PIM_REG_M, m);
    pim_write(PIM_REG_N, n);
    pim_write(PIM_REG_NUM_TILE, num_tile);
    pim_write(PIM_REG_CMD, PIM_CMD_GEMV);
    pim_write(PIM_REG_START, 1);

    pim_wait_done();

    return (pim_read(PIM_REG_STATUS) & PIM_STATUS_ERROR) ? -1 : 0;
}

#endif