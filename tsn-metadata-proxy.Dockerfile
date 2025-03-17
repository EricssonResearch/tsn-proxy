# SPDX-FileCopyrightText: Copyright Ericsson Research
# SPDX-License-Identifier: BSD-2-Clause

# syntax=docker/dockerfile:1

# Note on bpftool: if main branch fail to build or
# builds but unable to load the BPF programs use the
# tag commented below instead main (required for tc egress prog type):
#ARG BPFTOOL_TAG=7.6.0
ARG BPFTOOL_TAG=main
ARG BPFTOOL_SRC=https://github.com/libbpf/bpftool.git
ARG BBOX_VER=1.37
ARG DEPS_DIR=tsn-metadata-proxy

FROM debian:bookworm-slim AS build
ARG BPFTOOL_TAG BPFTOOL_SRC BBOX_VER

# Install dependencies for building bpftool
# and the CNI plugin artifacts (*.bpf.o)
RUN apt update --quiet && \
  apt install --quiet --yes --no-install-recommends \
    build-essential \
    clang \
    llvm \
    git \
    libelf-dev \
    binutils-dev \
    libbpf-dev \
    libcap-dev \
    wget \
    ca-certificates \
    gpg \
    gpg-agent

WORKDIR /tmp/
COPY bpf/Makefile /tmp/
COPY bpf/saver.bpf.c /tmp/
COPY bpf/restorer.bpf.c /tmp/
COPY bpf/tracking.bpf.c /tmp/
COPY bpf/garbage_collector.bpf.c /tmp/

# Convoluted command for download bpftool source, build static binary form it,
# strip down debuginfos resulting ~2Mb binary.
# Then download jq which is reqired for the CNI plugin installation process.
RUN git clone --quiet --recurse-submodules --depth 1 --single-branch \
  --branch ${BPFTOOL_TAG} ${BPFTOOL_SRC} 2>/dev/null && \
  EXTRA_CFLAGS=--static OUTPUT=/usr/bin/ make -C bpftool/src -j && \
  strip /usr/bin/bpftool && \
  wget --quiet https://github.com/jqlang/jq/releases/download/jq-1.7.1/jq-linux-amd64 -O jq && \
  chmod +x jq
# Make the eBPF artifacts (bpf/*.bpf.c)
RUN make

# Drop the first stage: every artifact built, we can leverage busybox for the installation.
# This results ~10Mb container image with everything required for the DaemonSet.
# To delete the temporary dagling image use: "docker image prune -f"
FROM busybox:${BBOX_VER} AS image
ARG DEPS_DIR
RUN mkdir /${DEPS_DIR}
COPY --from=build /usr/bin/bpftool /${DEPS_DIR}/
COPY --from=build /tmp/*.bpf.o /${DEPS_DIR}/
COPY --from=build /tmp/jq /${DEPS_DIR}/
COPY cni-plugin/install.sh /${DEPS_DIR}/
COPY cni-plugin/tsn  /
RUN chmod +x /tsn /${DEPS_DIR}/install.sh

LABEL description="Container image for TSN metadata proxy CNI plugin. \
Contain the plugin, its dependencies and install script."
LABEL author.names="Ferenc Fejes, Ferenc Orosi" \
      author.emails="ferenc.{fejes,orosi}@ericsson.com" \
      author.company="Ericsson Research"
LABEL arch="x86_64"

