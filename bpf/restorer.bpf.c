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

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2048);
    __uint(key_size, sizeof(uintptr_t));
    __uint(value_size, sizeof(struct stored_values));
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} tsn_map SEC(".maps");

SEC("tc/ingress")
int restore_metadata(struct __sk_buff *skb)
{
    void *data_end = (void *)(unsigned long long)skb->data_end;
    void *data = (void *)(unsigned long long)skb->data;

    if (data + sizeof(struct ethhdr) > data_end)
        return TC_ACT_SHOT;

    unsigned long long key = (unsigned long long)skb;
    void *value = bpf_map_lookup_elem(&tsn_map, &key);

    if (value) {
        struct stored_values values = *((struct stored_values *)value);
        bpf_skb_set_tstamp(skb, values.tstamp, BPF_SKB_TSTAMP_DELIVERY_MONO);
        skb->priority = values.priority;
        bpf_map_delete_elem(&tsn_map, &key);
    }

    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
