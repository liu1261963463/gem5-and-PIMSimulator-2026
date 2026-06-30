#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define PIM_BASE 0x1000000000UL
#define PIM_SIZE 0x1000

#define PIM_REG_CMD        0x000
#define PIM_REG_DIM        0x008
#define PIM_REG_INPUT0_ROW 0x010
#define PIM_REG_INPUT1_ROW 0x018
#define PIM_REG_RESULT_ROW 0x020
#define PIM_REG_BANK       0x028
#define PIM_REG_START      0x030
#define PIM_REG_STATUS     0x038
#define PIM_REG_LAST_CYC   0x040

#define PIM_OP_ADD   1
#define PIM_OP_MUL   2
#define PIM_OP_RELU  3

#define PIM_BANK_ALL 0

#define PIM_STATUS_DONE  0x1
#define PIM_STATUS_ERROR 0x4

static volatile uint64_t *pim;

static inline void wr(uint64_t off, uint64_t val)
{
    pim[off / 8] = val;
}

static inline uint64_t rd(uint64_t off)
{
    return pim[off / 8];
}

static void wait_done(void)
{
    while ((rd(PIM_REG_STATUS) & PIM_STATUS_DONE) == 0) {
        ;
    }

    if (rd(PIM_REG_STATUS) & PIM_STATUS_ERROR) {
        printf("PIM command failed\n");
        exit(1);
    }
}

static void pim_add_rows(uint64_t dim,
                         uint64_t input0_row,
                         uint64_t input1_row,
                         uint64_t result_row)
{
    wr(PIM_REG_CMD, PIM_OP_ADD);
    wr(PIM_REG_DIM, dim);
    wr(PIM_REG_INPUT0_ROW, input0_row);
    wr(PIM_REG_INPUT1_ROW, input1_row);
    wr(PIM_REG_RESULT_ROW, result_row);
    wr(PIM_REG_BANK, PIM_BANK_ALL);
    wr(PIM_REG_START, 1);
    wait_done();
}

int main(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    pim = (volatile uint64_t *)mmap(
        NULL,
        PIM_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        PIM_BASE
    );

    if (pim == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    /*
     * 这里的 row 编号要和 PIMSimulator 的 row-based data placement 对齐。
     * 示例沿用 PIMSimulator 测试中的常见设置：
     * input0_row = 0, input1_row = 128, result_row = 256。
     */
    pim_add_rows(
        1024,   // dim, 按 PIMSimulator executeEltwise 的 dim 语义
        0,      // input0_row
        128,    // input1_row
        256     // result_row
    );

    printf("PIM ADD finished, last latency ticks = %lu\n",
           rd(PIM_REG_LAST_CYC));

    munmap((void *)pim, PIM_SIZE);
    close(fd);

    return 0;
}