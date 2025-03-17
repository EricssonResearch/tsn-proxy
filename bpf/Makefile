# SPDX-FileCopyrightText: Copyright Ericsson Research
# SPDX-License-Identifier: BSD-2-Clause

CFLAGS=-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Werror
CC=clang

.PHONY: all clean
all: restorer.bpf.o saver.bpf.o tracking.bpf.o garbage_collector.bpf.o

vmlinux.h:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

restorer.bpf.o: restorer.bpf.c vmlinux.h
	$(CC) -g -O2 -target bpf -D__TARGET_ARCH_X86 -c $< -o $@

saver.bpf.o: saver.bpf.c vmlinux.h
	$(CC) -g -O2 -target bpf -D__TARGET_ARCH_X86 -c $< -o $@

tracking.bpf.o: tracking.bpf.c vmlinux.h
	$(CC) -g -O2 -target bpf -D__TARGET_ARCH_X86 -c $< -o $@

garbage_collector.bpf.o: garbage_collector.bpf.c vmlinux.h
	$(CC) -g -O2 -target bpf -D__TARGET_ARCH_X86 -c $< -o $@

clean:
	rm *.o vmlinux.h
