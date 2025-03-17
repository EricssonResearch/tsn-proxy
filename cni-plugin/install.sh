#!/bin/bash

# SPDX-FileCopyrightText: Copyright Ericsson Research
# SPDX-License-Identifier: BSD-2-Clause

BIN_DIR=/host/opt/cni/bin/tsn-metadata-proxy
CONF_PATH=/host/etc/cni/net.d
CONFLIST_JSON=$(ls -1 "$CONF_PATH" | head -n 1)

TSN_IFNAME=$(cat /etc/config/TSN_IFNAME)
TSN_INGRESS_OR_EGRESS=$(cat /etc/config/TSN_INGRESS_OR_EGRESS)

NEW_PLUGIN='{ "type": "tsn"'
if [ -n "${TSN_IFNAME}" ]; then
  NEW_PLUGIN=$(echo $NEW_PLUGIN", \"tsnIfname\": \"${TSN_IFNAME}\"")
fi
if [ -n "${TSN_INGRESS_OR_EGRESS}" ]; then
  NEW_PLUGIN=$(echo $NEW_PLUGIN", \"tsnIngressOrEgress\": \"${TSN_INGRESS_OR_EGRESS}\"")
fi
NEW_PLUGIN=$(echo $NEW_PLUGIN' }')


if grep -q "$(echo '"type": "tsn"')" "${CONF_PATH}/${CONFLIST_JSON}"; then
	echo "tsn plugin already in the conflist"
else
	UPDATED_CONFLIST_JSON=$(cat "${CONF_PATH}/${CONFLIST_JSON}" | ${BIN_DIR}/jq --argjson newPlugin "$NEW_PLUGIN" '.plugins += [$newPlugin]')
	echo "$UPDATED_CONFLIST_JSON" > "${CONF_PATH}/${CONFLIST_JSON}"
fi
