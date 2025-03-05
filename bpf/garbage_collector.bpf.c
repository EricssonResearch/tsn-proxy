// SPDX-FileCopyrightText: Copyright Ericsson Research
// SPDX-License-Identifier: BSD-2-Clause

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define TC_ACT_OK		0
#define TC_ACT_SHOT		2
#define CLOCK_MONOTONIC	1

struct stored_values {
    unsigned int priority;
    unsigned long long tstamp;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2048);
    __uint(key_size, sizeof(uintptr_t));
    __uint(value_size, sizeof(struct stored_values));
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} tsn_map SEC(".maps");

struct timer_map_elem {
    int counter;
    struct bpf_timer timer;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1);
    __type(key, int);
    __type(value, struct timer_map_elem);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} timer_map SEC(".maps");


#undef bpf_printk
#define bpf_printk(fmt, ...)                            \
({                                                      \
        static const char ____fmt[] = fmt;              \
        bpf_trace_printk(____fmt, sizeof(____fmt),      \
                         ##__VA_ARGS__);                \
})

static inline int remove_elapsed_pkts(struct bpf_map *map, const void *key, void *value, void *ctx)
{
    unsigned long long now = bpf_ktime_get_tai_ns();
    struct stored_values values = *((struct stored_values *)value);
    unsigned long long tstamp = values.tstamp;
    uintptr_t map_key = ((uintptr_t)*(uintptr_t *)key);

    if (now > tstamp) {
        int ret = bpf_map_delete_elem(&tsn_map, key);
    }

    return 0;
}

static int timer_callback(void *map, int *key) {
    bpf_for_each_map_elem(&tsn_map, remove_elapsed_pkts, NULL, 0);
    struct timer_map_elem *elem = bpf_map_lookup_elem(map, key);
    if (elem) {
        bpf_timer_start(&elem->timer, 1000000000, 0);
    }
    return 0;
}

SEC("tc")
int garbage_collector(void *ctx) {
    struct timer_map_elem elem = {};
    int key = 0;
    bpf_map_update_elem(&timer_map, &key, &elem, 0);

    struct timer_map_elem *elem_ptr = bpf_map_lookup_elem(&timer_map, &key);
    if (elem_ptr) {
        bpf_timer_init(&elem_ptr->timer, &timer_map, CLOCK_MONOTONIC);
        bpf_timer_set_callback(&elem_ptr->timer, timer_callback);
        bpf_timer_start(&elem_ptr->timer, 1000000000, 0);
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";