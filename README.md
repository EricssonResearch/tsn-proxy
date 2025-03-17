# TSN metadata proxy CNI plugin for Kubernetes

__Warning__: This is a proof-of-concept experiment.
Use on a production system at your own risk!

__Authors__: Ferenc Orosi, Ferenc Fejes

## Table of contents

- [Objective](#objective)
- [Requirements](#requirements)
- [Kubernetes setup](#example-kubernetes-setup)
  - [Kind (v0.24.0)](#kind-v0240)
    - [Change default CNI to Flannel or Calico](#change-default-cni-to-flannel-or-calico)
    - [Testing](#testing-kind)
    - [Cleanup](#cleanup-kind)
  - [Minikube (v1.34.0)](#minikube-v1340)
    - [Testing](#testing-minikube)
    - [Cleanup](#cleanup-minikube)

## Objective

TSN metadata proxy is a thin CNI plugin enable time-sensitive microservices to use the TSN network.
Linux provide time-sensitive APIs through the socket API.
With socket options or ancillary messages the application can set priority or scheduling metadata.
If the application deployed as a microservice e.g.: Kubernetes pod, this metadata removed by
the Linux kernel's network namespace isolation mechanism.
This CNI plugin ensures the metadata is preserved even after the packets leave the pod.

Main features:

- Does not require modification of the time-sensitive microservice
- Compatible with various Kubernetes distributions (like normal deployment, Minikube or Kind)
- Act as secondary CNI plugin used together with a primary plugin like Calico, Kindnet, Flannel, etc.
- Single `kubectl apply` configuration

## Requirements

- Kubernetes with Docker container engine
- Linux with eBPF enabled

## Example Kubernetes setup

A minimal example deployment consist:

- `talker` - a time-sensitive microservice pod with application(s) inside
- `bridge` - a bridge created by the primary CNI plugin in host (`node`) network namespace
- `node` - Linux host Kubernetes node with physical TSN network interface `tsn0`

```text
┌─────────────────────────────────────────────────────────────────┐
│                                                             node│
│ ┏━━━━━━━━━━━━━━━━┓        ┏━━━━━━━━━━━━━━━━┓                    │
│ ┃talker (pod)    ┃        ┃bridge          ┃        ┌───────────┤
│ ┃    ┌───────────┨        ┠───────────┐    ┃        │           │
│ ┃    │eth0 (veth)┃        ┃veth_xxxxx │    ┠ ─ ─ ─ ▶│tsn0 (NIC) │
│ ┃    │           ┠───────▶┃(veth)     │    ┃        └────●──────┤
│ ┗━━━━┷━━━●━━━━━━━┛        ┗━━━━━━●━━━━┷━━━━┛          ┌──┘      │
│         ┌┘                     ┌─┘              TSN compatible  │
│ eBPF program (saver)      eBPF program          network card,   │
│ to store TSN metadata     (restorer)            with TSN Qdisc  │
│                           to retore TSN                         │
│                           metadata                              │
└─────────────────────────────────────────────────────────────────┘
```

In this deployment there is a `talker` which is a pod.
The application inside `talker` will generate traffic with priority metadata.
The destination address of the metadata is outside of the node.
Therefore the packet leaves the pod network namespace,
goes straight to the CNI bridge in the `node` network namespace,
where it will be NAT-ed and routed and sent towards the `tsn0` NIC.
On the NIC traffic control Qdisc configured such as `taprio` or `mqprio`.

First, build the proxy container image that compile `BPF` programs
and include all the necessary components of the TSN proxy.
This required for every Kubernetes setup, same image
can be used for node deployment or Minikube, Kind based test setup.

```bash
docker build . -t tsn-metadata-proxy:latest -f tsn-metadata-proxy.Dockerfile
```

In the rest of the guide, we only focusing on Kind and Minikube.
These are popular Kubernetes distributions for quick deployment testing.
Compared to the example deployment diagram above,
if Kind or Minikube used, the `node` itself also a pod.
In normal production deployments `kubelet` and other core components,
are running withing the `node`'s root network namespace.

### Kind (v0.24.0)

Start a Kubernetes host with a custom configuration that mounts the `BPF` file system:

```bash
kind create cluster --name=demo --config cluster.yaml
```

The content of the `cluster.yaml` is the following:

```yaml
kind: Cluster
apiVersion: kind.x-k8s.io/v1alpha4
networking:
  disableDefaultCNI: false
nodes:
  - role: control-plane
    extraMounts:
      - hostPath: /sys/fs/bpf
        containerPath: /sys/fs/bpf
  - role: worker
    extraMounts:
      - hostPath: /sys/fs/bpf
        containerPath: /sys/fs/bpf
```

There are two nodes, a worker and a control-plane.
The BPF filesystem `/sys/fs/bpf/` exposed for both node in the default path.

> #### Change default CNI to Flannel or Calico
>
> This setup will create a cluster using the default `CNI`, which is Kindnet. 
> To replace it with another `CNI`, set `disableDefaultCNI: false` to `true` in the `cluster.yaml` file.
> This change only disables the default `CNI`.
> However, for functional networking basic CNI plugins like `bridge` must be downloaded separately:
>
> ```bash
> docker exec -it demo-control-plane /bin/bash
> curl -LO https://github.com/containernetworking/plugins/releases/download/v1.1.1/cni-plugins-linux-amd64-v1.1.1.tgz
> tar -xvzf cni-plugins-linux-amd64-v1.1.1.tgz -C /opt/cni/bin
> ```
>
> Once ready, you can deploy the `DaemonSet` of your desired `CNI` for example Flannel:
>
> ```bash
> kubectl apply -f https://github.com/flannel-io/flannel/releases/latest/download/kube-flannel.yml
> ```
>
> Similar for Calico (only choose one, do not install multiple primary CNI plugins)
>
> ```bash
> kubectl apply -f https://docs.projectcalico.org/manifests/calico.yaml
> ```

Copy the custom `Docker` images to the Kubernetes control plane:

```bash
kind load docker-image tsn-metadata-proxy:latest --name demo
```

On older `kind` versions this method might not work.
If that is the case, try the following two commands:

```bash
TMPDIR=<any folder> kind load docker-image tsn-metadata-proxy:latest --name demo
```

If `TMPDIR=/tmp` the metadata proxy CNI will be deleted on node restart.
It is advised to use different folder!

Check if the `Docker` image is loaded:

```bash
docker exec -it demo-control-plane crictl images
```

To check the running pods in a Kubernetes cluster use the following command (in new terminal):

```bash
watch -n 0.1 kubectl get pods
# or kubect get pods -w
```

With having the Kubernetes setup ready deploy the TSN metadata proxy:

```bash
kubectl apply -f daemonset.yaml
```

Verify the necessary files are present on the control plane,
the `BPF` map is created, and the CNI plgins config includes the `TSN plugin`.
(Note: if you use other `CNI`, replace `10-kindnet.conflist`):

```bash
docker exec -it demo-control-plane ls -la /opt/cni/bin/
docker exec -it demo-control-plane ls -la /sys/fs/bpf
docker exec -it demo-control-plane ls -la /sys/fs/bpf/demo-control-plane
docker exec -it demo-control-plane cat /etc/cni/net.d/10-kindnet.conflist
```

#### Testing Kind

Normally on a VM or bare-metal node the testing would require the config of the TSN NIC.
We have an Kind cluster, therefore the nodes are emulated as well.
To continue, configure the `taprio` qdisc on the `tsn0` NIC.
__Note: by default `tsn0` is likely named as `eth0`, the `tsn0` used here to stick with the example figure__
Right now this happened to have on the `demo-control-plane` node:

```bash
docker exec -it demo-control-plane ethtool -L tsn0 tx 4
docker exec -it demo-control-plane tc qdisc replace dev tsn0 parent root handle 100 stab overhead 24 taprio \
  num_tc 4 \
  map 0 1 2 3 \
  queues 1@0 1@1 1@2 1@3 \
  base-time 1693996801300000000 \
  sched-entry S 03 10000 \
  sched-entry S 05 10000 \
  sched-entry S 09 20000 \
  clockid CLOCK_TAI
```

For testing purposes, we deploy a pod using the `alpine/socat` image as the base image
With this Kind based Kubernetes setup, we need to tell where we want to schedule the pod.
For this, modify `nodeSelector` in `talker.yaml` to select the control plane:

```bash
kubectl apply -f talker.yaml
```

When deploying a pod on the control plane in our multi-node setup, we have to remove `NoSchedule` taint.
This taint set by default on nodes with role `control-plane`.
To remove this taint, we can use the `-` (minus) sing with the `NoSchedule` role:

```bash
kubectl taint nodes demo-control-plane node-role.kubernetes.io/control-plane:NoSchedule-
```

Now we have to generate traffic, and send packets with different priorities.
To do that, execute `socat` from a terminal in the `talker` node:

```bash
kubectl exec -it talker -- socat udp:8.8.8.8:1234,so-priority=1 -
```

Verify that packet metadata were preserved (restored), we can check statistics of the `taprio` qdisc:

```bash
docker exec -it demo-control-plane tc -s -d qdisc show dev tsn0
```

As in the output below, packets with priority 1 are transmitted through the second queue.
In the output queue numbering starts with 1 and priorities starting with 0.
Therefore `qdisc pfifo 0: parent 100:2` is the second `taprio` leaf (priority 1).

```bash
qdisc taprio 100: root refcnt 9 tc 4 map 0 1 2 3 0 0 0 0 0 0 0 0 0 0 0 0
queues offset 0 count 1 offset 1 count 1 offset 2 count 1 offset 3 count 1
clockid TAI base-time 1693996801300000000 cycle-time 40000 cycle-time-extension 0
 index 0 cmd S gatemask 0x3 interval 10000
 index 1 cmd S gatemask 0x5 interval 10000
 index 2 cmd S gatemask 0x9 interval 20000

 overhead 24 
 Sent 10717397 bytes 17292 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:4 limit 1000p
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:3 limit 1000p
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:2 limit 1000p
 Sent 2010 bytes 30 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:1 limit 1000p
 Sent 10713846 bytes 17239 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
```

#### Cleanup Kind

Delete the whole cluster:

```bash
kind delete clusters --all
```

If the BPF programs remain loaded, you can remove them using the following commands:
On a real (not Kind emulated cluster) the `rm` commands must be executed on every node.

```bash
sudo rm /sys/fs/bpf/restorer
sudo rm /sys/fs/bpf/saver
sudo rm /sys/fs/bpf/tracking
sudo rm /sys/fs/bpf/garbage_collector
sudo rm /sys/fs/bpf/timer_map
sudo rm /sys/fs/bpf/tsn_map
sudo rm -r /sys/fs/bpf/demo-control-plane
sudo rm -r /sys/fs/bpf/demo-worker
```

---

### Minikube (v1.34.0)

Start a Kubernetes cluster, mount the `BPF` file system, and set the `CNI` plugin to `Flannel`.
With Minikube no cluster manifest required.
As a limitation Minikube can only emulate single node cluster.

```bash
minikube start --mount-string="/sys/fs/bpf:/sys/fs/bpf" --mount --cni=flannel
```

Add the TSN metadata proxy container image to the Kubernetes local repository:

```bash
minikube image load tsn-metadata-proxy:latest
```

Check if the `Docker` image is loaded:

```bash
docker exec -it minikube crictl images
```

To watch the running pods in a Kubernetes cluster,
we can open a new terminal and run the following command:

```bash
watch -n 0.1 kubectl get pods
# or kubect get pods -w
```

With having the Kubernetes setup ready deploy the TSN metadata proxy:

```bash
kubectl apply -f daemonset.yaml
```

Verify the necessary files are present on the control plane,
the `BPF` map is created, and the CNI plgins config includes the `TSN plugin`.
(Note: if you use other `CNI`, replace `10-flannel.conflist`):

```
docker exec -it minikube ls -la /opt/cni/bin/
docker exec -it minikube ls -la /sys/fs/bpf
docker exec -it minikube ls -la /sys/fs/bpf/minikube
docker exec -it minikube cat /etc/cni/net.d/10-flannel.conflist
```

#### Testing Minikube

Normally on a VM or bare-metal node the testing would require the config of the TSN NIC.
We have a Minikube cluster, therefore the nodes are emulated as well.
To continue, configure the `taprio` qdisc on the `tsn0` NIC.
__Note: by default `tsn0` is likely named as `eth0`, the `tsn0` used here to stick with the example figure.__
Right now this happened to have on the `minikube` node:

```bash
docker exec -it minikube ethtool -L tsn0 tx 4
docker exec -it minikube tc qdisc replace dev tsn0 parent root handle 100 stab overhead 24 taprio \
  num_tc 4 \
  map 0 1 2 3 \
  queues 1@0 1@1 1@2 1@3 \
  base-time 1693996801300000000 \
  sched-entry S 03 10000 \
  sched-entry S 05 10000 \
  sched-entry S 09 20000 \
  clockid CLOCK_TAI
```

For testing purposes, we deploy a pod using the `alpine/socat` image as the base image.

```bash
kubectl apply -f talker.yaml
```

Now we have to generate traffic, and send packets with different priorities.
To do that, execute `socat` from a terminal in the `talker` node:

```bash
kubectl exec -it talker -- socat udp:8.8.8.8:1234,so-priority=1 -
```

Verify that packet metadata were preserved (restored), we can check statistics of the `taprio` qdisc:

```bash
docker exec -it minikube tc -s -d qdisc show dev tsn0
```

As in the output below, packets with priority 1 are transmitted through the second queue.
In the output queue numbering starts with 1 and priorities starting with 0.
Therefore `qdisc pfifo 0: parent 100:2` is the second `taprio` leaf (priority 1).

```bash
qdisc taprio 100: root refcnt 9 tc 4 map 0 1 2 3 0 0 0 0 0 0 0 0 0 0 0 0
queues offset 0 count 1 offset 1 count 1 offset 2 count 1 offset 3 count 1
clockid TAI base-time 1693996801300000000 cycle-time 40000 cycle-time-extension 0
 index 0 cmd S gatemask 0x3 interval 10000
 index 1 cmd S gatemask 0x5 interval 10000
 index 2 cmd S gatemask 0x9 interval 20000

 overhead 24 
 Sent 3366617 bytes 5240 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:4 limit 1000p
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:3 limit 1000p
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:2 limit 1000p
 Sent 2709 bytes 40 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
qdisc pfifo 0: parent 100:1 limit 1000p
 Sent 3363908 bytes 5200 pkt (dropped 0, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0
```

#### Cleanup minikube

Delete the whole cluster (__sudo is important!!!__):

```bash
sudo minikube delete --all
```

If there is an error, and the cleanup cannot complete,
we can remove every remnant of the TSN metadata proxy's with the following commands.
This does not uninstall the TSN metadata proxy CNI plugin itself, only the running configs.

```bash
sudo rm /sys/fs/bpf/restorer
sudo rm /sys/fs/bpf/saver
sudo rm /sys/fs/bpf/tracking
sudo rm /sys/fs/bpf/garbage_collector
sudo rm /sys/fs/bpf/timer_map
sudo rm /sys/fs/bpf/tsn_map
sudo rm -r /sys/fs/bpf/minikube/
```

## Acknowledgement

This work was supported by the European Union’s Horizon 2020 research and innovation programme through
DETERMINISTIC6G project under Grant Agreement no.
101096504
