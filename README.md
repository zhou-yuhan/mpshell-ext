# Hostshell

Extend [`mpshell`](https://github.com/ravinet/mahimahi/releases/tag/old%2Fmpshell_scripted) from [mahimahi](https://github.com/ravinet/mahimahi) with support for:

- Arbitrary number of links (net interfaces)
- AQM schemes (DropHead, DropTail, Codel, PIE) instead of infinite queue only

## Mimicking a Network Host
This tool tries to mimic a network end host instead of in-network links (which is emulated by original mahimahi). 

### Double Queues
A network host typically consists of a netdev buffer managed by qdisc of Linux kernel and a driver/NIC buffer managed by the device driver. More importantly, when the driver buffer/queue is full, it is usually blocked and the driver informs the kernel to backlog packets in qdisc buffer (check [NSDI'24 paper](https://www.usenix.org/conference/nsdi24/presentation/wang-chengke) for detailed explanation). 

Therefore, this tool emulates a network interface by concatenating two queues acting as qdisc and NIC driver, respectively. The qdisc queue only begins to backlog data when the driver queue is full. `hostshell` supports attaching AQM schemes to each queue, note that the qdisc queue is never backlogged if the NIC driver queue is configured to be `infinite` (see usage).

### Exposing Queue Status
Sometimes it is useful and convient to check queue status (i.e. packets/bytes backlogged) of a network interface on a host. `hostshell` provides a UNIX domain socket to provide host queue status to outside. Clients can open `/tmp/unix-hostqueue-[id]` and query queue status of emulated interface `id` with sub-millisecond control delay. `hostshell` accepts any numbers of clients on any interface. Currently the query result is returned immediately in JSON format no matter what the client query format is.

## Build
```
./autogen.sh
./configure
make
sudo make install
```

## Usage
```
hostshell config_file program
```

`config_file` is a configuration file in JSON format listing the link number and properties for each link. See `example-config.json`.