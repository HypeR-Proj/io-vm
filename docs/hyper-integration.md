<!-- SPDX-FileCopyrightText: 2026 roolrz -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Linux I/O VM

Keeping an independent I/O VM and preferring device drivers outside the HypeR
kernel is an [architectural decision](https://github.com/HypeR-Proj/kernel/blob/main/docs/architecture/README.md#device-driver-placement),
not a temporary bring-up arrangement. Native userspace drivers are another
supported placement; applications consume their services through controlled
interfaces.

The I/O VM is a trusted Linux service VM. HypeR owns the Native applications,
virtual hardware, launch policy, and authoritative DTS/DTB. The separate
[HypeR-io-vm repository](https://github.com/HypeR-Proj/io-vm) owns everything
needed to produce the Linux appliance: pinned upstream Linux, configuration,
external modules, Linux userspace services, initramfs assembly, tests, and
package publication. This repository does not build Linux or modify appliance binaries. Board
deployment adds a separate configuration archive to the Linux initramfs.

The default QEMU deployment assigns physical virtio-PCI devices through
host-owned SMMUv3 domains, restricting DMA to the I/O VM's RAM and explicit
shared-memory grants. Guests still trust the backend for I/O data and
availability. The physical MMIO compatibility and Pi 5 paths retain their
trusted-I/O-VM requirement. See [I/O isolation](https://github.com/HypeR-Proj/kernel/blob/main/docs/design/io-isolation.md)
for the permission, memory-lifetime and qualification boundaries.

The AArch64 QEMU baseline runs two Linux VMs: the I/O VM directly drives a
QEMU virtio-scsi disk, and a business VM reaches it through standard virtio-scsi
and upstream vhost-scsi/LIO. A Native test deployment owns both VMs, their DTBs,
shared memory and control transactions. It verifies disk contents independently
on the host and checks DMA memory admission after both VMs retire.
See [implementation status](https://hyper-proj.github.io/HypeR/docs/project/status.html#linux-io-vm-baseline) for the acceptance
boundary. Pi 5 device assignment has basic storage/network measurements;
extended hardware qualification remains deferred.
The board deployment path is described in [board storage](https://hyper-proj.github.io/build/docs/deployment/board-storage.html).

## Selected backend interfaces

The I/O VM architecture selects three interface families:

| Interface | Purpose | Current status |
| --- | --- | --- |
| virtio-scsi | Storage for Native clients and guest disks | Implemented with vhost-scsi/LIO |
| virtio-net | Guest network connectivity | Implemented with vhost-net/TAP; QEMU GICv2/GICv3 acceptance and an initial Pi 5 TCP survey completed |
| vfio-user | General device backends beyond the storage and network interfaces | Planned; cross-VM integration remains to be designed |

Storage and guest networking have implementations; vfio-user remains a design
direction. Other device models are assessed against actual requirements;
there is no promise of universal support. Native userspace services remain an
option for devices that do not fit these interfaces.

The third family is **vfio-user**, not virtio-user or vhost-user. Its integration
must define device presentation, cross-VM transport, memory authorization,
interrupts, DMA and reset/retirement. The existing storage bridge is not a
vfio-user implementation, and this decision does not claim wire compatibility
or automatic physical-device passthrough.

## Resident startup

`make run` on AArch64 uses the QEMU board configuration, keeps the HypeR shell,
and starts the bootstrap `/svc/io-runtime` from the generated core manifest.
Bootstrap init explicitly delegates the physical-device
capability only to this service and charges its VM to an independent, bounded
I/O resource domain. Its startup does not require the business VM manager.
The service owns the VM, assigned physical controllers and control mailboxes. Guest power-off,
control failure or runtime exit initiates VM retirement; failed physical reset
continues to use the kernel's existing quarantine semantics. There is no
automatic restart of an uncertain device owner.

Native io-runtime discovers GPT partitions and asks the Linux backend to bind
their authorized ranges. Bootstrap mounts the system image at `/system` and
switches it to `/`. Formal system-startup adopts the running I/O VM and filesystem
providers, then starts the mount oneshot for `/etc/fstab` (including `/data`) and
the VM manager with its configured fleet. Business
VMs are started only when their configuration requests autostart or through
`vmm start`. `make run RUN_PROFILE=io` retains the explicit standby diagnostic
profile: it boots a resident backend without mounting Native storage or
provisioning business clients. This QEMU diagnostic uses physical virtio-MMIO
storage with no network uplink, independently of the production board's PCI
selection.

Building the default board profile creates `target/board/qemu/disk.img` once
and preserves existing disks. `make run` validates the disk's GPT against the
selected board configuration, then launches the existing kernel, ramdisk and
disk. `BOARD_IMAGE` selects another board disk. The diagnostic `RUN_PROFILE=io` instead creates
`target/app/aarch64/io-disk.img`; `IO_VM_DISK` overrides that diagnostic disk.
`RUN_PROFILE=native` selects a synthetic no-device test fixture: its test-only
init copies the supplied formal payload into an independent RAMFS before the
real stage-1 supervisor performs the handoff. It has no I/O appliance and is
not a product deployment mode. `IO_VM_PACKAGE` can override the downloaded
package with a complete, locally qualified boot generation. The importer also
accepts `IO_VM_REFERENCE` for an explicit digest and `IO_VM_ORAS` for ORAS.

`make test-io-standby` verifies backend readiness, slow interactive shell input,
continued idle operation and an unchanged, preexisting physical disk. The
`test-io-vm` acceptance remains separate and deliberately writes its own
unique disposable disk through the full two-VM virtio-scsi path.

## Repository and package ownership

| HypeR | HypeR-io-vm |
| --- | --- |
| Native apps, VM lifecycle, grants, notifications | Upstream Linux build and out-of-tree drivers |
| Guest virtio device models and negotiation | Linux-side services, vhost-scsi/LIO and vhost-net/TAP |
| DTS/DTB and device assignment policy | Kernel configuration and complete Linux initramfs |
| Digest-pinned package download and validation | GHCR package publication and corresponding source materials |
| Hyper integration and board tests | Linux appliance build and backend tests |

The Linux service is not a Native app or a member of the Native app workspace.
Linux ioctl and kernel-module ABI details stay in the I/O VM repository.
HypeR keeps its Apache-2.0 source license. Linux and its modules retain their
own licenses; distributing their binaries requires the corresponding source
materials and notices regardless of which repository contains the build scripts.

The delivery boundary is an OCI artifact in GitHub Container Registry (GHCR),
not a container to execute. A boot package contains `Image`, a complete
`initramfs.cpio.gz`, a versioned manifest, and corresponding source materials.
Ordinary builds resolve `ghcr.io/hyper-proj/io-vm:main-aarch64-qemu` or
`main-aarch64-rpi5` to an immutable SHA-256 digest, then verify and cache that
exact generation. Qualified main publication advances these channels automatically;
contributors do not update package-version files. Explicit `IO_VM_REFERENCE`
digests remain available for reproduction, and `IO_VM_PACKAGE` selects a local build.
Source materials remain available with the same package generation, outside the boot ramdisk.

An incompatible package format, wrong architecture/platform, missing artifact or failed
checksum aborts import. Download and verification finish before a new generation becomes
visible. A failed update leaves existing cached generations intact and reports the failure.
Channel lookup requires registry access; an explicit already-cached digest works offline.
`make run` starts the last packaged image without fetching or rebuilding its appliance.

System topic tests build the selected I/O VM source directly. Pi image publication also
requires source-matching appliance materials. Platform qualification in Linux/QEMU does
not certify physical Pi operation; retain separate hardware evidence in release inventories.
Final boot images follow the [image release contract](https://hyper-proj.github.io/pi5-images/docs/distribution.html).

The I/O VM repository owns the pinned upstream LTS version and builds each
external module against the exact kernel configuration and release. HypeR
consumes the resulting coherent image and does not independently replace a
module. Bridge protocol compatibility is negotiated separately from Linux's
internal module ABI.

DTBs are generated by HypeR from admitted hardware and mapping facts. A Linux
package must not supply the authoritative DTB. A host DTB must not be copied
wholesale into a VM: that would advertise resources the VM does not own.
`hyper-vm-image` remains the owner of the runtime-generated guest DTB.

## Board-managed clients

`make board-run BOARD=qemu` uses the board-generated client policy. Client zero
is an owner-only physical block management channel, with a private metadata-read
scratch pool. HypeR io-runtime enumerates disks, validates GPT, resolves selectors
from `etc/hyper/storage.json`, and binds immutable ranges to data clients.
Linux validates range bounds/read-only access/writable overlap before creating
its dm-linear/LIO exports. Linux never interprets partition labels or GPT.

Native data clients use IDs from 8 down; guest clients use IDs from 1 up, sharing
eight slots. Each Native data client has its own shared-memory pool and
mailbox/notification routes. io-runtime activates the connection and moves its
exclusive block capability plus discovered partition UUID/label to fs-backend.
The manager first applies only the bootstrap fstab to mount `/system`. After
handoff and root switching, formal init asks it to apply `/etc/fstab`,
loading the FAT worker from `/system` for `/data`. Fleet configuration follows
these formal mounts; separate workers own each mounted volume.
Native clients are absent from the business broker's admission table.

Owner block commands are additive protocol-v3 operations 11–14 (`BLOCK_HELLO`,
`BLOCK_ENUMERATE`, `BLOCK_READ`, `BLOCK_BIND`). The handshake requires all three
management feature bits. The guest decoder excludes these operations; ordinary
queue and network protocols are unchanged. An older appliance without this
feature cannot run the new board bootstrap and fails readiness cleanly. The
committed appliance pin supplies this extension; no local package override is
needed for the default board deployment.

A VM definition may include `"disk": {"client": 1, "volume": "alpine"}` and/or
`"network": {"client": 1, "network": "default", "mac": "02:48:59:00:00:01"}`.
Both devices use the same client ID. The manager creates one capability session
for the runtime; the I/O runtime checks its complete device policy against
`/etc/hyper/io-clients.conf`, generated from the same board JSON as the Linux
client and network tables. Duplicate active bindings are refused.
The board source assigns client IDs when it generates the runtime definitions;
see [the QEMU board](https://github.com/HypeR-Proj/manifest/blob/main/workspace/boards/qemu.json) for the source format.
`vmm create NAME --config FILE` reads disk/network assignments and runtime settings from
the named definition in the configuration file; `--from SOURCE` selects a template.

The resident I/O VM has 64 MiB of ordinary RAM for Linux, services and
vhost queue allocations; shared business-guest pages are additional mappings,
not allocator memory for Linux. The isolated two-VM fixture below retains its
64 MiB per-VM layout.

The HypeR board's per-VM disk node also owns backend resource sizing:

```json
"disk": {
  "source": "PARTLABEL=alpine",
  "backend": {"request-queues": 4, "sg-prealloc": 128}
}
```

`request-queues` is 1–4 (default 4), excluding the SCSI control and event queues.
`sg-prealloc` is 2–2048 (default 128) scatterlist entries per request. Larger
requests allocate additional chained entries as needed; this setting does not
reduce the advertised transfer limit. Lower preallocation saves resident memory
at the cost of possible allocation work during larger transfers.

The build projects these values into the Linux `hyper.clients.v5` table; they
remain deployment policy, not fields interpreted by the HypeR kernel. Native
storage clients use 4/128. Linux applies the settings to each newly opened
vhost-scsi instance before acquiring its owner, without changing global module
parameters. The SCSI HELLO reply reports the selected capacity; both MMIO and PCI
frontends expose that number of queues. The activation record retains six wire
slots and requires unused slots to be zero. Device reset retains the policy;
reopening the backend reapplies it. Board policy changes require rebuilding the
deployment and restarting the I/O VM; editing `/data/vms.json` does not override
the board's backend allocation policy.

This requires an I/O appliance supporting the v5 client table and its per-instance
vhost-scsi resource extension. The image build checks the appliance's client-table
version and rejects older packages before constructing a storage deployment.
The updated appliance still accepts v3 tables from older HypeR deployments,
using the 4/128 defaults and the original six-slot wire layout.

The business fleet has a finite allowance for eight VMs and manager overhead;
the resident I/O VM remains in the core-services domain outside that fleet. Board I/O client slots can
further limit that fleet when extra Native volumes are configured. Physical
memory admission remains fallible; quotas do not reserve backing RAM in advance.

The I/O runtime owns one control mailbox per client and a separate notification
capability for each configured device. The
per-VM runtime handles virtio configuration MMIO and exchanges only negotiation
and notification enable/disable commands over its dedicated channel. Virtqueue
kicks, completions, disk data and packet buffers use direct kernel routes and
shared pages. Neither Native service relays individual I/O requests or packets.

Normal guest RAM remains noncontiguous. A single guest-memory grant is mapped
into the frontend and admitted into the I/O VM using an immutable, token-scoped
extent table. Dynamic aliases use the fixed physical-address translation;
static I/O RAM and all Native shared pools are excluded from that
translation to avoid ambiguous DMA reverse mappings.

Closing a runtime session revokes its connection: disable new notifications,
reset and drain every configured vhost endpoint, release the Linux mappings, obtain the kernel's
quiescence proof, then remove the old notification routes. A new binding gets
a new generation. Any failure to establish quiescence stops the I/O VM and
retains uncertain DMA storage until actual device retirement. A timeout alone
never authorizes memory reuse.

## Device protocol and control ownership

The guest-facing devices use standard virtio-scsi and virtio-net. A business VM's
`configuration.virtio-transport` selects `mmio` (when omitted) or `pci` on AArch64.
The QEMU board explicitly selects PCI for its default Alpine guest.
The two transports share feature negotiation, queue validation and backend
activation/reset state. For example:

```json
{
  "configuration": {
    "memory-bytes": 268435456,
    "vcpus": 2,
    "bootargs": "console=ttyAMA0 earlycon hyper.root=/dev/sda",
    "virtio-transport": "pci"
  }
}
```

The PCI profile is modern-only: one ECAM bus at `0x0a800000`, a 1 MiB memory
aperture at `0x0a000000`, and one relocatable 4 KiB memory BAR per endpoint.
SCSI is `00:01.0` and networking is `00:02.0`; absent endpoints read as absent
PCI functions. Vendor capabilities describe common configuration, the queue
notification register, the read-to-clear ISR, device configuration and the
PCI_CFG indirect-access window. Both register access paths share state. INTx
uses the guest GIC, including mask/unmask with retained pending status; MSI-X,
legacy/transitional virtio remain unsupported. This business-VM profile has a
fixed endpoint topology. The virtual bus is independent of physical RP1
assignment and cannot address host PCI resources.

The I/O appliance has a separate userspace PCIe host bus in domain 2. Bootstrap
advertises two ECAM buses at `0x0a600000` and a 2 MiB memory window at
`0x0aa00000`. Bus 0 contains a root port with an empty, standard PCIe hotplug
slot; bus 1 is its downstream bus. Physical RP1 assignment remains in domain 0,
with its own DMA translations. The virtual bus uses guest addresses and cannot
access physical PCI configuration or business-VM memory aliases.

After online adoption, the formal I/O runtime can populate the empty slot and
raise the standard presence-change notification. The Linux PCIe hotplug driver
then enumerates the endpoint without a reboot or an explicit sysfs rescan.
Hotplug uses INTA/INTID 120; the console endpoint uses INTB/INTID 121, preserving
ordinary PCI interrupt swizzling through slot zero. Guest-programmed bus
numbers, bridge window and interrupt controls survive the runtime transfer.
See [the handoff contract](https://hyper-proj.github.io/lib-bootstrap-handoff/docs/handoff.html#running-provider-transfer) and
[virtio-console](https://hyper-proj.github.io/kernel/docs/design/virtio-console.html) for device ownership and lifecycle.

Queue notifications remain direct kernel routes. Only ECAM, BAR configuration
and virtio configuration enter userspace. BAR relocation is confined to the
VM's registered MMIO aperture and cannot change the backend connection or its
IRQ. Clearing bus master on an active endpoint drains its backend before the
configuration write completes and requests a virtio reset; live queue
suspend/resume is not supported. Memory decoding and interrupt masking alone
do not prove DMA retirement. No change to the I/O VM protocol or image is
required; the Alpine guest image includes both transport drivers.

Linux vhost-scsi/LIO and vhost-net consume the shared virtqueues.
Neither vm-runtime nor the Linux management service forwards individual disk
requests or packets.

Configuration MMIO uses a bounded per-vCPU request, published after hardware
detach and completed through the owning vCPU capability. Reading a request is
not acknowledgement. Completion matches the exact generation and advances the
instruction exactly once. Administrative stop cancels the continuation.
Only registered device ranges may use this path; unknown MMIO retains its
diagnostic terminal disposition.

Queue kicks and completion interrupt status/acknowledgement use prevalidated
kernel notification bindings. They do not enter the userspace device loop.
Durable pending state and virtqueue indices are authoritative; a physical IPI
is only a prompt to a remote CPU. Idle backends block, and notification arming
must recheck pending work to avoid lost wakeups.

Virtio features are the intersection of the guest transport, bridge, and Linux
backend capabilities. Queue activation commits only after mappings and backend
configuration have succeeded. Device reset first disables new submissions,
quiesces the backend, and then releases resources. Linux vhost ioctl numbers
remain Linux-local: the cross-VM bridge is not an ioctl forwarding ABI.

A notification binding exposes a Native `PEER_CLOSED` signal when either VM
retires. The surviving guest can still read and acknowledge its transport
interrupt state; losing the backend does not turn those accesses into terminal
MMIO faults. The runtime backend API can mark the device `DEVICE_NEEDS_RESET`
and raise a configuration-change interrupt. The current acceptance fixture
stops both VMs on backend failure instead of keeping the business guest running.
A closed binding cannot be enabled
again, and late backend completions cannot reactivate it. Queue descriptions
and pending transactions remain retained until independent CPU/DMA quiescence
has been established; failure notification is not permission to release pages.

The AArch64 reference controller exposes 256 interrupt IDs on GICv2 and GICv3.
Assigned physical devices and I/O notification/control endpoints admit shared
interrupt IDs 40 through 255, with collision checks against existing routes.
The I/O VM may own both storage and network controllers; activation rollback
and final retirement must quiesce every assigned controller before RAM reuse.

## Guest networking

The QEMU path is guest virtio-net → Linux vhost-net/TAP → Linux bridge → assigned
virtio-net uplink → QEMU user networking. The bridge has no IP address; QEMU
supplies DHCP and NAT. Native HypeR does not establish a network link. Client
zero remains reserved for future Native networking, but no Native network
frontend, TAP or network notification binding is created.

Appliance startup creates the configured bridge (by default `hbr0`), attaches
the uplink and brings both interfaces up. Each guest gets its own nonpersistent
TAP when its virtio-net queues activate; reset or retirement removes that TAP
after vhost drains. Guests on the same bridge share a layer-2 segment. QEMU's
NAT sits beyond the uplink; the I/O VM does not provide DHCP or IP routing.

The board's `io-vm.network-device` selects the physical controller and
`io-vm.networks` names the bridge/uplink. Each business VM's optional `network`
entry selects a network and stable locally administered unicast MAC. Disk and
network are independent options: a network-only VM does not receive a hidden
storage endpoint or LIO target. The shared Ethernet segment does not provide
anti-spoofing or per-VM network isolation policy.

The board's per-VM network node can also specify backend buffering:

```json
"network": {
  "network": "default",
  "mac": "02:48:59:00:00:01",
  "backend": {"rx-backlog-packets": 1000, "tx-buffer-bytes": 0}
}
```

`rx-backlog-packets` limits the TAP backlog headed toward guest RX to 1–65536
packets (default 1000). `tx-buffer-bytes` sets the TAP socket accounting threshold
for guest TX: zero preserves Linux's unlimited default; nonzero settings accept
65536–2147483647 bytes. This threshold includes packet allocation overhead and
can be exceeded by an allocation; neither setting is a hard cap on all network
memory attributable to the VM. Physical uplink and bridge resources are shared.

These are per-client deployment settings, projected only into the I/O VM's
seven-field `hyper.clients.v5` table, after the SCSI resource fields. They do not
enter the editable fleet file or change negotiated virtio features. A compatible
appliance is required; rebuild the deployment and restart it to change policy.
Each TAP activation reapplies the settings before attaching vhost queues.
Defaults preserve existing behavior. In the pinned Linux implementation, a
`tx-buffer-bytes` value below `INT_MAX` disables some vhost/TAP batching fast paths;
measure throughput and CPU cost before selecting a tighter production budget.

The guest device uses modern virtio over the selected MMIO or PCI transport,
RX queue 0 and TX queue 1, with at most
128 descriptors per queue, with MTU 1500. It offers `VIRTIO_F_VERSION_1` and
`VIRTIO_NET_F_MAC`. Guest TX checksum completion (`VIRTIO_NET_F_CSUM`) and TCP
segmentation (`VIRTIO_NET_F_HOST_TSO4/HOST_TSO6`) are offered only when the
backend's NETWORK_HELLO advertises them; TSO additionally requires CSUM in the
guest's negotiated features. RX offloads, mergeable RX buffers, a control
virtqueue and multiqueue are not offered.

Modern virtio uses a 12-byte packet header even without mergeable RX buffers.
An offload-capable backend preserves that header through vhost-net to an
`IFF_VNET_HDR` TAP, with a 12-byte little-endian header. TAP converts guest TX
metadata into Linux checksum/GSO state; the uplink can complete it in hardware
or Linux can fall back to software. `VHOST_NET_F_VIRTIO_NET_HDR` stays clear in
this mode. TAP's `TUNSETOFFLOAD` flags remain zero: those describe packets sent
*to* the guest, whose receive path still requires ordinary MTU-sized frames.

The qualified QEMU and Pi 5 appliances support these optional TX offloads.
The fetcher resolves the selected platform channel and imports its immutable OCI digest.
TX offloads themselves need no local override; the new owner block-management
feature has the separate appliance requirement described below. The pinned Linux build also orders vhost
work-node reuse before concurrent requeue, preventing pending work from being
lost. Older backends and guests may still negotiate VERSION_1 and MAC alone.

Each client has one memory owner, binding identity and control transaction
sequence. Storage and network have independent queue epochs and can reset
independently. The primary endpoint is storage when present, otherwise network;
its HELLO admits the binding and its epoch labels PREPARE_MEMORY/RELEASE_MEMORY.
NETWORK_HELLO returns 80 bytes, including MAC and MTU; NETWORK_ACTIVATE uses two
queue records in a 112-byte request. Only the shared memory owner may acknowledge
RELEASE_MEMORY, after both backends have drained. A failed drain retains the
mapping and withholds the retirement proof.

After that proof, HypeR removes the retired alias mappings and detaches empty
stage-2 child tables from the still-running I/O VM. Detached table pages and
their quota charges are released only after every CPU acknowledges translation
invalidation. Faults retry without rebuilding detached parent slots until that
barrier completes. Spare table-owner metadata is then compacted when allocation
permits; live mappings and the I/O VM's own page-table root remain owned.

The Pi 5 profile uses the same guest/frontend/backend contract with RP1 GEM as
the physical uplink. `boards/rpi5.json` combines SDIO1 storage with RP1
Ethernet. The Linux bridge forwards guest MAC addresses onto the attached LAN;
the LAN supplies DHCP and routing. Neither Native HypeR nor the I/O VM bridge
has an IP address.

The assignment unit is the complete RP1 PCI function (`pci-function`, selected
by PCI vendor/device ID `1de4:0001`). HypeR owns the BCM2712 host bridge,
firmware-initialized PCI windows, exclusive BAR ownership and physical MSI
routing. Linux sees a generic ECAM host bridge, all admitted BARs and a GICv2m
MSI frame. PCI configuration and MSI-X table accesses are mediated: BAR probes
and relocations affect only the guest view, and messages can target only the
assignment's guest SPI range. Linux owns RP1's interrupt controller, clocks,
GPIO, GEM, PHY and other enabled function devices. Native projects the firmware
subtree and its admitted dependencies without operating RP1 function registers.

RP1 uses a separate noncoherent DMA bus with a 64 GiB bus-address offset;
SDHCI retains its own DMA translation. The current PCI host transport admits one
firmware-initialized, single-function endpoint with memory BARs and MSI-X.
It has no IOMMU isolation and is intended only for the trusted I/O VM.
Whole BARs can include vendor aliases for controller configuration; standard
PCI mediation alone does not contain a malicious function driver. RP1
peripherals share that owner and cannot be assigned independently. Stopping an
activated I/O VM quarantines its physical devices and memory; clearing PCI bus
mastering is not a DMA-drain proof. Ordinary business-VM stop/start still uses
the shared backend retirement protocol.

See the [Pi 5 Ethernet bring-up procedure](https://github.com/HypeR-Proj/kernel/blob/main/docs/usage/rpi5.md#ethernet-backend-qualification).
Host tests and QEMU acceptance do not qualify the physical RP1 link, cache
maintenance or interrupt ordering. The physical TCP survey records basic
operation; sustained traffic, concurrent I/O and fault coverage remain
[deferred qualification](https://hyper-proj.github.io/HypeR/docs/project/roadmap.html#deferred-hardware-qualification).
The [manual network tools](https://github.com/HypeR-Proj/ci/blob/main/hardware/network/README.md) are opt-in guest
additions and do not change the default Alpine rootfs or I/O VM appliance.

## Shared pages and DMA

Direct queue consumption requires Linux to access queue metadata, responses,
and every guest data buffer referenced by descriptors. The trusted deployment
currently grants the entire RAM VMO of each business VM with an I/O-backed disk
or network device. Both devices share one grant and Linux mapping.
Each Native volume client uses a dedicated 1 MiB shared I/O pool, including
separate pools for the default configuration and system volumes.
Bridge protocol version 3 retains six storage
split queues: control, event, and four request queues. The Native client batches
notifications for newly published commands and refills slots as individual
commands complete. Its 512 KiB data pool supports up to four small concurrent
commands or one merged 512 KiB command. Each command owns its descriptor chain
and allocated data span until completion. Overlapping writes and flushes remain
ordered by the device engine; independent logical producers use bounded
[Native block queues](https://hyper-proj.github.io/lib-block/docs/block-io.html). Unretired requests disconnect the
device on failure; their memory lease remains retained until backend quiescence.
The rest of HypeR memory is not exported.

### Allocation and zero-copy scope

The business disk path shares the original guest pages: Linux vhost-scsi reads
virtqueue descriptors and accesses payload buffers through an I/O VM mapping
of those same physical pages. HypeR and vm-runtime do not copy each disk payload
through an intermediate buffer. This is cross-VM zero-copy, not a guarantee
that every Linux block driver or physical device avoids bounce buffers.

Guest RAM starts as a sparse VMO. However, admitting that VMO to the I/O VM
currently populates **every page**, before the backend starts serving requests.
`Mapping::prepare` in `kernel/src/vm/memory/live.rs` walks the full VMO,
calls `populate_page`, obtains physical addresses and constructs the shared
extents. The current alias address is derived from the host physical address,
so the complete extent description requires physical pages to exist first.
Thus attaching a disk or network device removes the memory-saving benefit of demand allocation
for that guest, even if stage-2 entries are subsequently installed on faults.
Guests without this backend admission can retain sparse backing; their actual
resident footprint still depends on image loading and guest accesses.

The resident I/O VM has a separate allocation policy: its own 64 MiB RAM is
allocated eagerly as one physically contiguous VMO for the current physical
device DMA layout. Each Native volume client also eagerly allocates its
1 MiB pool. Imported business-guest pages are shared data, not free RAM that
Linux can use for its own allocator or vhost metadata.

On AArch64, Stage-2 keeps the 4 KiB translation granule but can install a
2 MiB block for an aligned, physically contiguous range of already-resident
RAM. Primary guest RAM must belong to one backing region, have no previously
installed page mappings in that block, and have uniform instruction-publication
state. Dynamic I/O aliases must fit wholly inside one admitted grant extent.
Preallocation alone does not guarantee physical contiguity: the I/O VM's
contiguous RAM benefits directly, while scattered business-guest backing falls
back to 4 KiB mappings. The optimization does not populate additional RAM,
move shared pages, or change DMA addresses.

Device mappings retain 4 KiB granularity. A page-specific execute-permission
change splits a RAM block into 4 KiB descriptors naming the original physical
pages, with architectural break-before-make and translation invalidation.
Complete grant extents can be revoked without allocating split tables; backing
remains retained until the existing CPU/DMA retirement protocol completes.
Split tables are not automatically promoted back into blocks. This reduces
translation-table storage for eligible dense RAM; actual TLB and throughput
benefits depend on the processor and workload.

Whole-guest preallocation is a property of this implementation, **not a virtio
or vhost protocol requirement**. Supporting first-touch allocation from either
VM would require coordinated allocation from the same backing object, an import
address model that does not require a physical address in advance, and DMA
admission that makes the relevant pages resident and stable before submission.
CPU stage-2 faults alone cannot service a physical device's DMA access.
There is currently no configurable buffered-copy alternative; a bounded shared
pool would also require descriptor translation, backpressure and safe request
cancellation/reset handling.

### Memory observations and queue overhead

`vmm` reports two different quantities:

- `RAM capacity` is the configured guest RAM size.
- `allocated VM backing` counts resident physical backing in the VM's primary
  backing regions, deduplicating overlapping ranges of the same VMO within that
  snapshot. It does not count installed stage-2 entries, Linux's internal used
  memory, or all VM overhead such as page tables and vCPU stacks. It is not an
  exhaustive census of dynamically imported device mappings or a system-wide
  exclusive-memory accounting value.

For the default resident I/O VM, 64 MiB RAM plus the 1 MiB management pool
and two 1 MiB Native data pools account for 67 MiB (70,254,592 bytes) of
backing. The ramfs system layout needs only the data volume pool plus management,
for 66 MiB (69,206,016 bytes). A disk-backed 256 MiB Alpine guest can therefore
require roughly 323 MiB of combined VM backing before Native and kernel overhead.

Virtio-scsi permits multiple request queues but does not require one per guest
CPU. The guest driver chooses how many to use within the advertised per-client
limit (four by default). HypeR configures 128 preallocated scatterlist entries
per command by default, without changing Linux's global module defaults. At a
queue depth of 128 and a 32-byte entry, those entries alone account for
approximately 512 KiB per request queue, excluding command
metadata and other allocations. This is an implementation-dependent estimate,
not a fixed per-vCPU cost or measured peak. Queue depth, enabled queue count and
concurrent clients must all inform the I/O VM's RAM budget. These kernel
allocations cannot be replaced with swap; shared pages must also remain stable
through backend CPU/DMA use and retirement.

Business guests may enable one through four request queues; unused optional
queues have canonical zero entries in activation records. Protocol versions 1
and 2 are rejected; both repositories must use a matching version 3 generation.
Image loading uses the public SDK `hyper_os::fs::transfer::copy_range` mechanism,
which pipelines two 512 KiB userspace buffers between a scoped reader and an
arbitrary consumer. The VM consumer writes into guest memory; `cp` uses the same
mechanism for ordinary files. Read-ahead stays inside the selected range and the
reader is joined on success, failure or consumer unwind. The VM libraries and
bootstrap contain no private transfer implementation.

Sparse image validation uses the SDK's `fs::BufferedReader` over the common
`fs::ReadAt` contract. Its four-page local buffer reduces small-read syscall
cost; the kernel still owns the coherent, reclaimable file page cache. A new
buffered view is created for each validation and dropped before payload copying.
It is not a snapshot and does not make concurrent file mutation safe.

The optional `hyper-vm-runtime/startup-profile` Cargo feature reports image
validation, payload read/write, memory preparation, and installation timings.
Read and write durations overlap in the pipeline; their sum is not wall time.
The existing startup total ends at vCPU start submission, before guest entry.
Ordinary builds omit the detailed phase measurements.

The same physical pages must remain owned and stable until all backend CPU and
DMA users have retired. Guest physical, I/O VM physical, Linux virtual, host
physical, and device DMA addresses are distinct domains. A stage-2 alias does
not translate a physical device's DMA transaction.

The Native guest-memory grant freezes an explicitly supplied writable VMO and
may be mapped into multiple pending VMs. Mapping regions are page aligned,
non-overlapping, and must cover the configured RAM before sealing. Closing the
grant handle does not release installed mappings; Native access remains
excluded until the last retained hardware lease retires. The grant may be
delegated through startup capabilities or a capability-channel rendezvous.

`vmo_create_contiguous` provides eagerly allocated, zeroed physical backing for
power-of-two sizes from one page through the ABI's current 128 MiB limit. The
whole allocation is charged to its resource domain and retained until the last
derived page owner retires. This is an explicit alternative to sparse VMOs,
not implicit DMA authority: device assignment, DMA address translation, and
quiescence still require their own ownership and validation.

Static imports, including the owner and Native clients' pools and the legacy test
fixture, appear as Linux memory and reserved-memory nodes without `no-map` or
`reusable`. Linux creates `struct page` metadata but cannot allocate those pages.
Dynamic business clients instead advertise an aperture, without declaring it
as RAM. After token admission, the external module registers device-page
metadata and maps only the authorized scatter extents into a contiguous
userspace view. The vhost memory table translates frontend GPAs into that view.
Release drains vhost and drops Linux mappings and page references before the
module reports quiescence. The physical device bus has explicit `dma-ranges` tuples
(HypeR HPA, I/O VM GPA, length), covering both ordinary and imported RAM.

The QEMU test exercises imported-page I/O with nonidentity DMA translation and
`ACCESS_PLATFORM`. Shared queues and payload pages are not copied through the
Native control service. This does not promise that every Linux block-layer or
device alignment case is free of bounce buffers.

Stopping I/O VM vCPUs does not stop physical DMA. On a platform without DMA
isolation, pages cannot be reclaimed after a crash until the assigned hardware
has been reset and outstanding transactions have been drained. If quiescence
cannot be established, quarantine the pages; a timeout never grants permission
to reuse them.

## Build and validation

### Protocol version 3 appliance requirement

The qualified main channels select protocol version 3 QEMU and Pi 5 appliances.
Default builds resolve and verify immutable GHCR digests. The appliance boot banner and
`/etc/hyper-io-version` identify source revision, dirty state, build profile and UTC time.
A completed SD image includes its appliance and needs no registry access at boot.

For coordinated changes, open topic PRs and run manual system verification against the
combined sources before merging. The I/O VM main build then publishes qualified platform
channels automatically; `pi5-images` resolves source inputs and downloads a qualified
RPi5 package together with its digest-bound corresponding sources. Keep physical Pi results separate from Linux/QEMU
checks. Use `IO_VM_PACKAGE` for local appliance changes and `IO_VM_REFERENCE` for an
explicit previously published digest.

The default QEMU board includes storage and networking for Alpine:

```sh
make board-build ARCH=aarch64 BOARD=qemu
make board-run ARCH=aarch64 BOARD=qemu
```

`boards/qemu.json` owns the assigned network controller and guest interface;
the launcher creates the matching QEMU user-network uplink. Alpine automatically
loads virtio-net, brings up its interfaces and starts a background DHCP client
which configures addresses, routes and DNS and renews leases. A missing DHCP
server does not block the guest shell; client logs are in
`/var/log/udhcpc-<interface>.log`. This applies to both RAM and disk roots.
Existing persistent disks keep their on-disk VM definitions, guest FITs and
root filesystems across builds. Follow
[updating existing deployments](https://hyper-proj.github.io/build/docs/deployment/board-storage.html#updating-existing-deployments)
when changing device policy or guest startup files.

### Startup diagnostics

- `hyper-io-bridge ... failed with error -17`: an old appliance can treat a
  network notification as a storage notification and attempt to register the
  same device name twice. Verify that the packaged appliance recognizes
  `hyper,device-kind` and uses protocol version 3. Rebuild the bootstrap from
  the current appliance pin; changing only the guest disk does not
  replace the I/O VM.
- `vm-runtime: failed: OperatingSystem(Status(Status(-15)))`: `-15` means
  `PEER_CLOSED`. Inspect preceding io-runtime diagnostics for the cause. A
  `rejected client ... does not match the board I/O policy` message means the
  requested volume, network or MAC differs from the bootstrap policy. Compare
  `/data/vms.json` with `/boot/etc/hyper/io-clients.conf`; an existing disk can still
  contain a storage-only VM definition after a network-enabled bootstrap rebuild.
- Alpine reaches its shell but has no address: check `ip addr`, `ip route` and
  `/var/log/udhcpc-eth0.log`. The appliance's `network default ready on eth0`
  message confirms bridge setup, not a DHCP lease or a working external link.
  QEMU supplies DHCP through user networking; Pi 5 needs an external LAN DHCP server.

### Network acceptance

The [network verifier](https://github.com/HypeR-Proj/ci/blob/main/qemu/verify-network.py) derives a test fixture
from the default board and adds a second, network-only guest with a distinct
MAC address. This second guest belongs to the test, not the default deployment.
`test-board-network` prepares and runs this fixture with a fresh disposable disk
under `target/board-tests/network.*`. It retains the configuration and log for
diagnosis and writes a small proof file in the disk guest's `/root`:

```sh
make test-board-network ARCH=aarch64 \
  QEMU_MACHINE=virt,virtualization=on,gic-version=3
make test-board-network ARCH=aarch64 \
  QEMU_MACHINE=virt,virtualization=on,gic-version=2
```

The **HypeR / I/O VM integration** workflow runs separate GICv2 and GICv3 network
jobs on pull requests and main pushes. Its local suite is
`sh tests/ci/run.sh io-network`; `QEMU_MACHINE` selects the GIC version.
The suite imports the committed appliance pin unless `IO_VM_REFERENCE` selects
another immutable generation. The Make target also accepts `IO_VM_PACKAGE`.

The test serves a random 256 KiB + 137 byte payload on localhost, verifies its
SHA-256 inside each guest, then uploads it and checks the server's independently
computed SHA-256 receipt. It repeats both directions after reset and restart.
`NETWORK_TX_OFFLOAD=enabled` requires CSUM and both HOST_TSO bits in the guest's
negotiated state; `disabled` verifies fallback with an older appliance selected
through `IO_VM_REFERENCE`. The default accepts either, while always rejecting
unsupported RX offloads. CI needs no public HTTP endpoint. A direct
`verify-network.py run` invocation can add `--external-url http://...` with a
small public file to also check DNS and outbound HTTP; the external fetch
checks for a successful, nonempty response, while the local payload provides
the integrity check. The guest image must include its `virtio_net` module and automatic DHCP
startup. The verifier checks the address, default route, DNS configuration and
live DHCP client before issuing any manual network setup. Manual setup is used
only after the intentional driver unbind/rebind. Direct verifier invocations
must use the same generated board configuration for image construction and
execution; rebuild their disposable disk when guest files change.

Both GIC variants have passed DHCP, payload integrity, network driver
unbind/rebind, and stop/start followed by another transfer for a disk+network
guest and a network-only guest. The disk proof survives network reset and VM
restart; the network-only guest has no SCSI device. Both guests remain usable
after the other's network reset, and the verifier waits for all four
`RELEASE_MEMORY: ok` acknowledgements. An optional fetch from Alpine's public
HTTP server also passed for both guests, covering DNS and outbound connectivity.
The existing isolated storage acceptance continues to pass.

These are functional tests without performance thresholds. Concurrent
disk/network load, sustained traffic and fault stress require further coverage;
these results do not qualify physical RPi5 networking or a remote appliance
release. Backend protocol and injected retirement-failure tests separately
exercise shared memory ownership and device epochs.

### Package import and storage acceptance

Import the latest qualified QEMU package (ORAS is discovered
or downloaded automatically):

```sh
make io-vm-fetch
```

The standalone fetch target does not forward `IO_VM_ORAS`. To select an ORAS
executable for this step, run
`python3 -B scripts/fetch-io-vm.py --platform qemu --oras /path/to/oras`.
Board and I/O initramfs composition targets accept `IO_VM_ORAS`.

The command prints the verified generation directory under
`target/io-vm/packages/`. Channel resolution uses the registry; an explicit cached
digest verifies and reuses that directory without network access. A private GHCR package requires `oras login ghcr.io` first;
credentials belong to the local registry configuration, never to the repository.
Only the runtime payload is fetched. The OCI manifest also identifies the
matching configuration and corresponding-source archive for redistribution.
To qualify another generation, pass
`IO_VM_REFERENCE=ghcr.io/OWNER/PACKAGE@sha256:DIGEST` explicitly.
`make io-vm-fetch IO_VM_PLATFORM=rpi5` downloads and verifies the separately
built Pi package against its platform metadata. Platform metadata is not a hardware qualification claim.

Linux build instructions and the vhost-scsi reserved-page acceptance test live
in [HypeR-io-vm](https://github.com/HypeR-Proj/io-vm). They run in that
repository. The HypeR fixture defaults to the qualified QEMU channel:

```sh
make test-io-vm ARCH=aarch64
make test-io-vm ARCH=aarch64 \
  IO_VM_TEST=reset QEMU_CPUS=1 QEMU_MACHINE=virt,virtualization=on,gic-version=2
```

The fixture constructs identical FIT payloads from the same appliance. Separate
JSON configurations supply `hyper.role=io` and `hyper.role=business`; the runtime
adds HypeR-owned device trees.
It boots the independent static `tests/native/io-smoke` fixture as a privileged
Native `/init`; it does not replace
the normal init/service configuration. Each guest has 64 MiB of ordinary RAM;
the I/O VM additionally maps the business VM's RAM as reserved shared pages.
The reset case unbinds and rebinds the business virtio-scsi driver between two
32-round direct-write/flush/read runs.

Every run creates a fresh disposable QEMU disk. The harness verifies the final
sector and all untouched bytes after stopping its QEMU process, retaining the
disk and `result.json` beside the log. This uses LIO iblock, not a RAM disk.
A Linux-only successful appliance test is a separate prerequisite.

System integration runs manually in `HypeR-Proj/ci` against resolved source candidates.
The latest qualified QEMU package is resolved once to an immutable digest and reused
throughout the run. System verification never compiles the I/O appliance.
The local I/O reset suite remains `sh tests/ci/run.sh io-vm`; set `IO_VM_PACKAGE`
for a local appliance or `IO_VM_REFERENCE` for an exact published generation.
Board-managed Native FAT and business-client acceptance paths are documented in
[board storage](https://hyper-proj.github.io/build/docs/deployment/board-storage.html).

Physical virtio discovery accepts the trigger declared by firmware, including
QEMU's edge-triggered SPIs. Active physical edge sources remain enabled;
level sources are masked until guest acknowledgement. The guest-facing line
is level-triggered and follows durable virtio interrupt status.

## Compatibility and qualification

Before release, bridge layouts must define explicit widths, endianness, lengths, capability
bits, and generation-tagged identities. Extensions are negotiated; existing
fields and operation numbers do not change meaning. Unsupported mandatory
features fail connection setup. The Native syscall ABI and Linux-local UAPI
are separate interfaces and do not inherit bridge version numbers.

The storage baseline passes with one vCPU per guest, under both GICv3 with
four host CPUs and GICv2 with one host CPU. It covers direct read/write/flush,
driver unbind/rebind, exact host disk contents, nonidentity DMA translations,
and memory admission after retirement. Separate Native guest tests cover
mailbox/notification interrupts, backend exit, stale completions and interrupted
MMIO retirement.

Sustained multi-vCPU storage load, concurrent reset during I/O, forced I/O VM exit with
physical requests in flight, and physical read/write/flush failure injection
still need dedicated qualification. Pi 5 must additionally validate DMA address
translation, cache maintenance, interrupt ordering, device reset, and measured
image sizes. Failed physical quiescence quarantines the owner and its pages;
this baseline does not provide automatic recovery of quarantined devices.

### Native mapping release retries

`guest_mapping_release` reports `WOULD_BLOCK` when another operation owns the
all-CPU translation synchronization transport. No mapping state has changed;
retry the same release without repeating Linux RESET/RELEASE. `BUSY` instead
means that the backend has not yet supplied the mapping's quiescence proof.
Other failures are not treated as proof or silently retried. The I/O broker
bounds transport retries to five seconds, sleeps between attempts, and keeps
processing backend console and power notifications. A failed retirement keeps
its mapping ownership and enters the backend shutdown/quarantine path.

### Forwarded appliance logs

The bootstrap I/O runtime prefixes forwarded guest console lines with
`[    1.234567][HypeR bootstrap stage 1][io-runtime] IO VM: `. The timestamp
is sampled from HypeR's boot clock at the start of each line, before buffering;
it uses the same seconds and six-digit microseconds format as kernel logs.
After adoption, the formal runtime uses `HypeR IO VM:`. Both preserve prefixes across split serial reads. Native runtime messages
retain their own service name. Prefixing does not buffer a whole line or alter
its bytes; a partial line remains visible, so output from other services can
still interleave before the guest emits its next newline.

An appliance message such as `HypeR I/O [naa.5001405000000001]` identifies its
LIO/vhost-scsi target. This NAA-format world-wide name binds the vhost endpoint
to its storage target; it is not a VM ID, memory address, or disk capacity.
The appliance constructs target names from the authorized client ID (client
one uses the suffix `000000001`; client zero has no storage target). These locally generated
names do not assert a globally assigned hardware identity.

### PCI transport acceptance

Run `make test-board-network QEMU_CPUS=4` for physical and guest PCI enumeration,
guest disk read/write, networking, driver rebind and repeated VM retirement.
The default QEMU board selects physical virtio-SCSI and virtio-net PCI functions
behind an SMMUv3, using host-mediated INTx. The I/O VM's physical network and
storage projections use PCI domains 0 and 1; domain 2 is reserved for hotplug.
`make test-board-mmio` explicitly selects MMIO for both physical and guest
transports and covers the same lifecycle. Both use
fresh disposable disks and functional checks, without performance thresholds.
The protected PCI configuration requires QEMU 9.2+ and its `virt-9.2` or newer
machine model. Older models expose a stage-1-only SMMU and are rejected by the
stage-2 driver; selecting an old model is not a supported isolation fallback.

### DMA fault reporting

On SMMUv3-backed physical PCI devices, the kernel blocks the offending stream
and retains device-local fault evidence. Formal `io-runtime` reads that evidence
after adoption and observes later fault signals. It writes one report per device
to its stderr journal stream, visible with `journalctl -c /io-runtime`. Reports
include the device role, SID, fault code, available address/direction and original
boot timestamp. Bootstrap does not consume or report these notifications.

This reporting does not stop or restart the I/O VM. The quarantined device cannot
resume DMA until host reboot; unaffected devices can continue working. Kernel
serial logs remain available independently. Platforms without SMMU enforcement
do not gain DMA protection from the reporting API. See
[SMMUv3 fault handling](https://github.com/HypeR-Proj/kernel/blob/main/docs/design/smmuv3.md#formal-io-runtime-notification).

### Hotplugged agent transport

The formal runtime can insert a named virtio serial port into the running I/O
VM after ownership transfer. Stage1 supplies only the empty PCIe slot. See
[virtio-console](https://hyper-proj.github.io/kernel/docs/design/virtio-console.html) for the Linux kernel requirements, bounded
byte-stream interface, reset semantics and explicit hotplug acceptance target.
The appliance runs upstream `qemu-ga` on `org.qemu.guest_agent.0` once that port
appears. Its SBSA boot/log console remains unchanged. The formal runtime samples
Linux CPU counters and memory accounting asynchronously every five seconds;
`vmm list` and `vmm status NAME` read the cached observation through the existing
read-only broker. Neither tool receives the serial port or VM control authority.

CPU busy and I/O wait are deltas of `guest-get-cpustats`, normalized across all
guest CPUs to 0–100%. Linux guest/guestnice counters are already included in
user/nice and are not added twice. Memory comes from the stateless, read-only
`guest-hyper-get-memory-stats` appliance extension: used is `MemTotal` minus
`MemAvailable`, with free, buffers, cached and reclaimable slab shown separately.
These are guest-reported Linux statistics, not the hypervisor's allocated VM
backing or a trusted resource-accounting boundary.

The first CPU sample establishes a baseline. Counter reset or CPU membership
change establishes a new baseline. RPCs have a two-second deadline, records are
bounded to 16 KiB, and each reactor turn consumes at most four channel chunks.
Disconnects/timeouts clear the sample; samples older than 15 seconds are marked
stale. The client reconnects without restarting storage, networking or the VM.
An older agent lacking these commands is reported as unsupported.

The appliance enables only synchronization, ping, version/command discovery and
these two statistics commands. Generic execution, file access and guest shutdown
RPCs are disabled. `qemu-ga` is a separate GPL executable in the Linux appliance;
HypeR's Native client implements only the wire protocol.
