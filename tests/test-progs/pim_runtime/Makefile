CC ?= gcc
CFLAGS ?= -O2 -static

all: pim_eltwise_test pim_gemv_test

pim_eltwise_test: pim_eltwise_test.c pim_runtime.h
	$(CC) $(CFLAGS) -o $@ pim_eltwise_test.c

pim_gemv_test: pim_gemv_test.c pim_runtime.h
	$(CC) $(CFLAGS) -o $@ pim_gemv_test.c

clean:
	rm -f pim_eltwise_test pim_gemv_test