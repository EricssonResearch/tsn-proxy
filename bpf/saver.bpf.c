// SPDX-FileCopyrightText: Copyright Ericsson Research
// SPDX-License-Identifier: BSD-2-Clause

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define TC_ACT_OK       0
#define TC_ACT_SHOT     2

struct stored_values {
    unsigned int priority;
    unsigned long long tstamp;
};

/* Hashmap for the TSN metadata (skb->priority, skb->tstamp)
 * if size too small, adjust max_entries accordingly*/
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2048);
    __uint(key_size, sizeof(uintptr_t));
    __uint(value_size, sizeof(struct stored_values));
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} tsn_map SEC(".maps");


SEC("tc/egress")
int save_metadata(struct __sk_buff *skb)
{
    void *data_end = (void *)(unsigned long long)skb->data_end;
    void *data = (void *)(unsigned long long)skb->data;

    if (data + sizeof(struct ethhdr) > data_end)
        return TC_ACT_SHOT;

    unsigned long long key = (unsigned long long)skb;
    void *value = bpf_map_lookup_elem(&tsn_map, &key);

    if (!value && skb->tstamp > 0) {
        struct stored_values new_value = {
            .tstamp = skb->tstamp,
            .priority = skb->priority
        };
        int ret = bpf_map_update_elem(&tsn_map, &key, &new_value, 0);
    } else if (!value && skb->tstamp == 0) {
        unsigned long long now = bpf_ktime_get_tai_ns();
        struct stored_values new_value = {
            .tstamp = now,
            .priority = skb->priority
        };
        int ret = bpf_map_update_elem(&tsn_map, &key, &new_value, 0);
    }

    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
