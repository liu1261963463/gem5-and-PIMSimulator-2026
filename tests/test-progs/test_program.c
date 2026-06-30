#include <stdio.h>
#include <stdint.h>

int main(void)
{
    volatile uint64_t sum = 0;

    for (uint64_t i = 0; i < 1000000ULL; i++) {
        sum += i;
    }

    printf("Hello from gem5 guest! sum=%lu\n", (unsigned long)sum);

    return 0;
}