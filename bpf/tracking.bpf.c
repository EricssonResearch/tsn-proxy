// SPDX-FileCopyrightText: Copyright Ericsson Research
// SPDX-License-Identifier: BSD-2-Clause

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

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


SEC("fexit/skb_clone")
int BPF_PROG(fexit_skb_clone, struct sk_buff *old, gfp_t mask, struct sk_buff *new) {
    if (old && new){
        unsigned long long old_key = (unsigned long long)old;
        unsigned long long new_key = (unsigned long long)new;
        struct stored_values values = {
            .tstamp = old->tstamp,
            .priority = old->priority
        };

        void *value = bpf_map_lookup_elem(&tsn_map, &old_key);
        if (value) {
            int ret = bpf_map_update_elem(&tsn_map, &new_key, &values, 0);
        }
    }

    return BPF_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
