<!-- SPDX-FileCopyrightText: 2026 roolrz -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# HypeR I/O VM

A Linux appliance under development for HypeR's virtio-scsi storage and
virtio-net guest networking backends. This repository
owns Linux builds, external drivers, Linux-side services, complete initramfs
assembly, tests, and GHCR package publication. HypeR owns Native apps and DTS.

Linux is pinned to upstream **6.18.51 LTS** with focused changes in
`patches/linux/`: vhost work-queue ordering prevents lost work on AArch64,
and a per-instance vhost-scsi resource interface avoids oversized global defaults.
The build records each patch hash and includes the patches in corresponding
sources. Patch revisions use separate extracted source trees, so cached and
concurrent builds cannot silently share different kernel source contents.
Hyper-specific drivers build as external `.ko` modules.

At boot, the appliance prints its own Git revision (12 hexadecimal digits),
an optional `-dirty` suffix, build profile, and UTC build time. The same line
is available in `/etc/hyper-io-version`; the base manifest records the full
revision, dirty state and `build_timestamp`. Uncommitted tracked changes,
untracked source files, or different external module sources mark the build
dirty. Set `SOURCE_DATE_EPOCH` to a nonnegative Unix timestamp for a reproducible
appliance banner. The upstream Linux release and module ABI remain unchanged.

## Build

On Linux, install a C toolchain with static linking support, GNU make,
Python 3.12+, patch, bison, flex, bc, Perl, OpenSSL/libelf development headers, and kmod.

```sh
make fetch
make base JOBS=4
make test
```

Non-AArch64 builders set `CROSS_COMPILE=aarch64-linux-gnu-`.
The QEMU and Pi 5 profiles are built and published separately. Select
`PLATFORM=rpi5` for the existing upstream BCM2712 device configuration, or
`make all-platforms` to produce both. Platform annotations identify the build
profile; physical board and DMA qualification remain separate.

Pull requests targeting `main` and every push to `main` run the **Upstream kernel
and real-disk acceptance** GitHub workflow for both build profiles. It checks
source/packaging contracts and rebuilds the locked sources and external modules.
The new kernel and packaged BusyBox then exercise block discovery, bounded reads,
and immutable client ranges on disposable 512-byte and 4-KiB-sector disks,
including read-only enforcement and writable overlap rejection. TAP offload,
business boot/reset, diskless UART/virtio-console bring-up, and vhost-scsi
read/write/flush acceptance also run before merge; the backing image is
independently checked. Successful
main builds automatically publish the complete appliance as an immutable
prerelease package. These checks do not yet exercise the cross-VM
HypeR path; that remains part of qualifying a digest for the main repository.

The build also creates a complete appliance under `target/io-vm/boot/`, selected
by `qemu-boot.json`, and matching source archives under `source-artifacts/`.
Its explicit `hyper.role=io` boot argument loads the bridge modules, exports
`/dev/sda` through LIO iblock, and starts the control service. `hyper.role=business`
runs an ordinary Linux disk-driver read/write/flush test against `/dev/sda`;
use that role only with a disposable acceptance disk. Missing roles power off.
The base remains available separately for isolated Linux fixtures.

The control service handles setup/reset only. Queue kicks and completions use
kernel eventfd bindings, while vhost-scsi and vhost-net process shared queues.
Reset acknowledges only after endpoint drain and callback detach; a failed drain
retains its resources. Service exit powers off the I/O VM so its Native owner
can quiesce DMA before releasing imported memory. Cross-VM qualification is
still required before publishing a production package.

The current control protocol is version 3, including shared storage/network
memory ownership. HypeR must use the matching Native control plane and package
generation. An older notification module cannot distinguish the two device
kinds for one client and can fail probe with duplicate device names.

### Appliance console

Both kernel profiles include the standard Linux virtio-console driver. The boot
owner may pass `hyper.console=virtio` to send appliance userspace stdin, stdout
and stderr through `/dev/hvc0`, including the version banner and backend logs.
Init waits up to ten seconds for that tty and fails explicitly if it is absent;
it does not silently fall back to a UART. `hyper.console=uart`, or omitting the
argument, preserves the inherited UART stdio used by existing deployments.

The Linux `console=` argument is independent: keep `console=ttyAMA0` for kernel
and panic diagnostics. Do not add `console=hvc0` merely to select the userspace
terminal. HypeR owns the virtual console device and its guest-memory queues;
the appliance uses the upstream driver and does not route its own console through
the storage or network backend services.

The kernel CI matrix boots both profiles in each console mode. The virtio check
captures userspace output and UART kernel output separately, so a broken hvc0
path cannot pass using UART fallback. For a local built appliance:

```sh
python3 -B tests/io-vm/bringup.py \
  --boot-index target/io-vm/qemu-boot.json --console virtio \
  --log target/io-vm/bringup-virtio.log
```

This creates `bringup-virtio.log` for the terminal and
`bringup-virtio.uart.log` for kernel diagnostics. `make test` also executes init
against private tty fixtures, covering default UART behavior, redirected output,
missing virtio devices and invalid console selection without booting QEMU.

## Package delivery

Release complete boot images as OCI artifacts in GHCR using ORAS 1.3.
Authenticate with `oras login ghcr.io`; CI uses a token with `packages:write`.
The publisher validates the assembled kernel, initramfs, base identity, and
kernel configuration before uploading. It prints the immutable reference to
pin in HypeR; no moving `latest` tag is used by the consumer.

The GitHub workflow uses the repository's `GITHUB_TOKEN`; no registry secret is
required. A successful main push build triggers publication of that exact run's
artifact, using version `main-<commit>-run<run-id>-attempt<attempt>`. Failed or
cancelled builds, PRs, and manual build runs do not auto-publish. The publication
summary and `published-reference` artifact contain the digest. HypeR's lockfile
is updated separately after cross-VM qualification of those exact QEMU and
Pi 5 digests. The Pi 5 image-distribution repository then selects the merged
HypeR revision and inherits its appliance pins. A local boot-generation
directory is suitable for development tests, not a published release identity.

Manual build and qualified publication remain available:

```sh
gh workflow run kernel.yml --ref main -f operation=build
# After the build and HypeR cross-VM qualification succeed:
gh workflow run kernel.yml --ref main -f operation=publish \
  -f qualified_run=BUILD_RUN_ID -f revision=FULL_COMMIT_ID -f version=VERSION
```

Publication revalidates the build identity and sources, serializes requests for
one version, refuses an existing tag, and retains `published-reference.txt` as
an Actions artifact. HypeR pins that digest rather than the release tag. Repository
visibility and package visibility are separate: the package must allow anonymous
pull before it is adopted by public HypeR CI.

The equivalent local publisher is:

```sh
python3 -B tools/publish.py --boot /path/to/verified-boot \
  --base /path/to/verified-base --sources /path/to/sources.tar.xz \
  --revision FULL_COMMIT_ID --reference ghcr.io/roolrz/hyper-io-vm:VERSION-aarch64-qemu
```

`--oci-layout /path/to/layout` writes a local OCI package for verification
instead of uploading. Publication must follow successful appliance integration
checks; this tool alone cannot qualify a working backend.

The corresponding-source tar.xz must contain the exact upstream Linux,
BusyBox, QEMU, GLib, libffi, PCRE2 and zlib sources, module and service sources, resolved kernel/BusyBox configs,
build/assembly scripts, source locks, and license texts for the shipped build.
A URL list is insufficient. The source layer is part of the same immutable
OCI manifest as the binaries; HypeR fetches only `Image` and `initramfs.cpio.gz`.
The publisher verifies the source revision, recorded build inputs, upstream
archives, resolved configuration, and preservation of the compiled base payload.
Release review still establishes that the qualified appliance meets its runtime
contract.

## Licensing

Original build tools, configurations, and tests are Apache-2.0; see [LICENSE](LICENSE).
Project-authored `hyper_io.h` and `hyper_io_layout.h` offer Apache-2.0 OR
GPL-2.0-only for their userspace and kernel consumers.
Linux bridge modules in `modules/` are GPL-2.0-only; see
[GPL-2.0](LICENSES/GPL-2.0-only.txt) and per-file SPDX headers. Linux and BusyBox
retain their upstream licenses. The separate QEMU guest-agent executable and
its appliance extension are GPL-2.0-or-later; its static dependencies retain
their upstream licenses. Binary releases must carry matching source
materials and notices; the combined appliance is not Apache-only.

## Standby boot

HypeR can boot `hyper.role=io hyper.mode=standby` with only assigned storage
and a control mailbox. The service probes vhost-scsi and blocks on mailbox
requests without allocating client RAM, notification bindings, or virtqueues.
HELLO and RESET remain available; ACTIVATE is rejected while no client is
provisioned. Live client attachment is not implemented by this mode. The
existing attached deployment and disk acceptance remain unchanged.

### Managed block devices

HypeR supplies `/etc/hyper-clients.conf` with header `hyper.clients.v5` and
`client-id network-name mac-address request-queues sg-prealloc rx-backlog-packets tx-buffer-bytes`
lines. Build manifests and OCI packages report client-table version 5 so
consumers can reject an older appliance before constructing this deployment.
A missing network and MAC use `-`. For example,
`1 default 02:48:59:00:00:01 2 128 1000 0`
allocates two SCSI request queues and 128 scatterlist entries per request for
client 1. Queue counts are 1–4; SG preallocation is 2–2048. The table includes
exactly one owner entry, normally `0 - - 4 128 1000 0`. The original v3 three-field
format remains accepted with defaults of 4 queues and 128 SG entries, as does
the v4 five-field format with explicit SCSI policy. Both default to 1000 RX
backlog packets and unlimited TX buffering. Other entries identify authorized
transport slots; they contain no partition UUIDs,
volume names or disk offsets. Storage-capable slots have a disk notification
node in the HypeR-supplied DT; network-only slots do not. Network authorization
and MAC validation are unchanged.

Each service configures its own vhost-scsi file before `VHOST_SET_OWNER`.
No sysfs/module parameter is changed. HELLO reports control/event plus the
configured request queues; ACTIVATE still has six wire slots, and unused slots
must be zero. Larger requests use Linux's chained scatterlist allocation instead
of being rejected at the preallocation size. Reset/reopen preserves the supplied
policy. Changing a board policy requires regenerating the HypeR deployment and
restarting the appliance; live tuning is not supported.

The owner enumerates physical block devices through control endpoint zero,
reads their metadata into a bounded shared scratch buffer, and requests
per-client LBA ranges. GPT parsing, partition selection and filesystem policy
belong to HypeR Native userspace. Linux does not interpret a partition label or
UUID and does not mount exported filesystems. The returned disk IDs are local
to the appliance lifetime; the owner retains read-only device files throughout
that lifetime. Partition devices, device-mapper devices and loop/RAM devices
are not enumerated as independent backing hardware.

Enumeration at cursor zero refreshes the hardware inventory without replacing
existing IDs, so devices that finish probing after init remain discoverable.
The Native owner retries missing partition selectors up to its boot deadline.
An empty inventory is valid for diskless deployments. Retained descriptors are
checked against Linux disk sequence and capacity before metadata reads or
binding; replacement media cannot silently inherit an earlier disk ID.

A range binding creates a dm-linear device and LIO target named by client ID.
The final LUN symlink publishes the fully prepared export. Data HELLO and
ACTIVATE reject an unbound storage slot. Bindings are immutable until appliance
shutdown: an identical request is idempotent, while changing an existing range
returns BUSY. Overlapping bindings are rejected whenever either is writable;
read-only sharing is permitted. Read-only exports set both device-mapper's
read-only flag and LIO iblock's `readonly=1`. Data requests continue directly
through standard virtio-scsi/vhost-scsi; the owner is not a data relay.

Management commands extend protocol version 3. All integers are little-endian;
request headers and the first 48 reply bytes retain the existing identity and
status layout. All failures use a 48-byte reply. Only control endpoint zero
admits these operations; Native runtime brokers must never forward them from a
guest's data control channel.

| Operation | Request after the 40-byte header | Successful reply after byte 48 |
| --- | --- | --- |
| `BLOCK_HELLO` (11) | Empty | `u64 capabilities = 7`, `u32 max_read_bytes = 1048576`, `u32 reserved = 0` |
| `BLOCK_ENUM` (12) | `u32 cursor`, `u32 reserved = 0` | `u64 disk_id`, `u64 capacity_bytes`, `u32 logical_sector_size`, `u32 physical_sector_size`, `u32 flags`, `u32 next_cursor` |
| `BLOCK_READ` (13) | `u64 disk_id`, `u64 byte_offset`, `u64 byte_length`, `u64 scratch_offset` | Empty |
| `BLOCK_BIND` (14) | `u32 client`, `u32 flags`, `u64 disk_id`, `u64 first_lba`, `u64 sector_count` | Empty |

`BLOCK_HELLO` starts the owner's binding session even if no disk exists.
Capabilities bits 0/1/2 mean enumerate/read/bind. Enumeration starts at cursor
zero, follows `next_cursor`, and ends with an all-zero 32-byte payload.
Flags bit 0 means read-only; no other flags are accepted. Binding LBAs use the
enumerated logical sector size; the backend converts them to device-mapper's
512-byte units after overflow and capacity checks. Reads must be logical-sector
aligned and fit both the disk and the owner's prepared scratch mapping.
`PREPARE_MEMORY` maps at most 1 MiB for the owner, using the static alias and
mapping token zero. A read completes before its reply is published, and the
owner must not access its destination concurrently. No management operation
writes directly to a raw disk.

Each data client retains one service process, memory mapping and mailbox.
Storage and networking keep separate vhost fds, notifications and reset epochs.
Releasing a client's memory drains its vhost devices but retains the immutable
range for subsequent VM starts. Published LIO and dm mappings remain until
appliance shutdown; a manager process exit must not remove another client's
live DMA resources. Standalone single-disk qualification fixtures without the
client table retain their explicit whole-disk setup.

For managed slots, each mailbox, notification and guest-memory DTB node carries
`hyper,client-id = <N>` (0 through 127). The devices are named
`hyper-io-control-N`, `hyper-io-notification-N` and `hyper-memory-N`. Omitting this
property preserves legacy naming and fixture behavior. Static guest-memory nodes retain their reserved-memory backing for Native scratch and filesystem I/O pools. Dynamic nodes instead carry `hyper,dynamic-memory` plus a `reg`
aperture; they are not declared as Linux RAM and allocate neither host backing
nor Linux page metadata at boot. PREPARE registers only the authorized actual
extent using upstream `MEMORY_DEVICE_GENERIC` / `memremap_pages`. Dynamic
grants retain 4 KiB page granularity; shared 2 MiB device-page metadata is
created on demand and reference-counted across grants. The HypeR Native owner and filesystem clients each supply bounded static
pools; their extents come from the guest DT. A client is bounded to 1,048,576 pages (4 GiB).
The alias uses a fixed affine translation of the host physical address,
represented by standard dma-ranges without rewriting device DMA operations. The HypeR owner excludes
its I/O VM RAM and static translated pools from that translated range, and must
ensure the aperture fits Linux's linear-map window and avoids guest MMIO.

Managed sessions use protocol version 3's shared-memory operations.
`PREPARE_MEMORY` (5) is a
72-byte record: the existing 40-byte header followed by little-endian alias
physical address, frontend GPA base, byte length and mapping token. The HypeR owner installs the actual pages first; the
service maps only this length, bounded by its DTB window. `RELEASE_MEMORY` (6)
is a 40-byte record. Its success reply follows synchronous vhost drain, unmap, dynamic page-reference retirement
and memory-fd closure. Only then may HypeR revoke the alias mapping and free the
grant. Failure requires quarantine. `RESET` retains the prepared memory for a
virtio device reset. Reusing a released slot with a new binding requires HELLO
and a strictly increasing binding generation. A new notification route may restart
its queue epoch at one; epochs remain monotonic within one binding. Delayed
requests from old binding generations are rejected.
Incompatible appliances must fail setup rather than use
an unsafe whole-window fallback. Individual normally closed clients retire
without stopping other services; the owner must keep a slot mailbox alive when
it intends to reuse that slot via RELEASE/PREPARE.

Dynamic mapping tokens are one-use identities issued by HypeR. After all vhost,
VMA and pgmap references have retired, the Linux notification module writes the
token to the backend notification register at offset `0x20`. HypeR authenticates
the actual trapping VM before marking the mapping quiescent. A mailbox reply
alone is not permission to revoke a live mapping. Static Native pools use token 0
and does not produce this dynamic-grant proof. A failed memory retirement keeps
the slot owned and cannot be bypassed with HELLO for a new binding.

Before touching a dynamic grant, the notification driver writes its token to
`0x28` and checks the admission status at `0x40`, then verifies the immutable
frontend GPA and length at `0x30` and `0x38`. HypeR atomically changes Created to
Admitted and binds the token to that notification route. An already admitted or
retired token cannot be reused, including on another client slot. If metadata
differs, Linux retires the admitted but untouched token immediately. Native
cancellation of a never-admitted mapping is safe; admitted mappings require the
backend's route-authenticated quiescence proof.

Dynamic PREPARE uses alias 0: a scatter grant is identified by its token, not by
an invented contiguous physical allocation. After admission, the driver reads
extent count at `0x48`. It writes an index to `0x50` and reads alias, frontend
offset, length and query status at `0x58`, `0x60`, `0x68` and `0x70`. All are
64-bit registers. HypeR coalesces physically adjacent pages; Linux validates
complete ordered coverage before mapping anything. It builds a single userspace
VMA from the authorized PFNs, so the existing standard vhost memory table stays
one contiguous frontend GPA region. Unused neighboring pages in a metadata
granule are never inserted into that VMA. The extent walk happens only during
admission; it is not part of the storage data path.

## QEMU and Pi 5 profiles

`make all-platforms` builds two appliances from the same pinned upstream Linux
and BusyBox sources, using separate build and publication identities. Main
builds and publishes both matrix entries as `-aarch64-qemu` and
`-aarch64-rpi5`; each package includes its own resolved configs and matching
source archive. The Pi 5 profile includes SDHCI and its clock/GPIO/pinctrl
dependencies, plus the generic PCI host, GICv2m, RP1 PCI, RP1 clock/GPIO, MACB
and Broadcom PHY drivers. HypeR supplies a virtual PCI host for the assigned
whole RP1 function; Linux owns RP1's interrupt controller and peripherals.
The physical BCM2712 PCIe transport, resource claims and MSI routing remain
with HypeR. This is a trusted-driver deployment without IOMMU DMA isolation;
RP1 peripherals cannot be split across independent owners.
Running it on QEMU checks guest boot, not physical devices or DMA.

For initial Pi 5 guest bring-up, HypeR may select
`hyper.role=io hyper.mode=bringup`. This mode boots Linux userspace and waits
under Native VM supervision without probing storage, exporting a LUN, loading
bridge modules, or advertising backend-service readiness. Do not supply client configuration in this mode. Storage-backed deployments continue to
use `standby` or `attached`; bring-up is never an automatic error fallback.

Sharing this repository does not relicense Linux, BusyBox or the GPL bridge
modules. Both binary variants retain the same corresponding-source and notice
requirements described above.

### Guest networking

The board supplies `/etc/hyper-networks.conf`: its first line is
`hyper.networks.v1`; subsequent lines contain `network-name bridge uplink`, for
example `default hbr0 eth0`. An empty table leaves networking disabled. Bootstrap
creates Linux bridges without assigning IP addresses. In QEMU the passed-through
virtio-net uplink connects to QEMU user networking, which supplies DHCP and NAT.
The guest uses the bridged link directly; HypeR Native has no network stack or
active network endpoint. On Pi 5, the same bridge uses RP1 GEM as its physical
uplink, forwarding guest MAC addresses onto the LAN. The LAN supplies DHCP
and routing. The physical uplink inventory remains board-owned; physical Pi 5
networking and DMA retirement still require hardware qualification.

Bridge creation and uplink attachment run automatically from `/init`, before
the backend services start. Each guest's network startup is separate: the
Alpine image built by HypeR starts its own DHCP client. The appliance's
`network default ready on eth0` message confirms local bridge setup, not
external connectivity or a guest DHCP lease.

A configured business client owns one nonpersistent TAP, attached only when
its virtio-net device activates. `vhost-net` serves RX queue 0 and TX queue 1.
NETWORK_HELLO advertises modern virtio, a fixed locally administered MAC, guest
TX checksum completion (`VIRTIO_NET_F_CSUM`) and TCPv4/TCPv6 segmentation
(`VIRTIO_NET_F_HOST_TSO4/HOST_TSO6`). Both sides must negotiate these features;
TSO without CSUM is rejected before allocating activation resources. Older
frontends may still select VERSION_1 and MAC only. RX offloads, mergeable RX
buffers, control queues and multiqueue are not offered. The MTU is 1500 and each
of the two queues remains limited to 128 descriptors. This is a shared Ethernet
segment, not a network isolation or anti-spoofing policy.

Each client's `rx-backlog-packets` sets the TAP queue length before bridge
attachment. This is traffic headed to guest RX, although Linux names it TAP TX.
The supported range is 1–65536 packets; the default remains 1000. The optional
`tx-buffer-bytes` policy controls guest TX through `TUNSETSNDBUF`: zero preserves
Linux's unlimited default; nonzero values must be 65536–2147483647 bytes. It
counts socket-owned allocation overhead, not just payload, and an allocation can
exceed the threshold. These settings do not cap all per-VM network memory:
bridge, physical queues and other networking allocations remain shared.

Finite TX budgets below `INT_MAX` disable some batching fast paths in the pinned
Linux vhost/TAP implementation and may cost CPU or throughput. They do not change
CSUM/TSO negotiation. Policy remains with HypeR; no global network parameters are
changed. Network reset/rebind reapplies the same client policy. Configuration
failure aborts activation and uses the normal drain/cleanup path instead of
silently falling back to defaults.

TAP uses `IFF_VNET_HDR` with the modern 12-byte, little-endian virtio header,
even without mergeable RX buffers. Its configuration is probed before HELLO
and checked again before attaching either vhost queue. vhost's private
`VHOST_NET_F_VIRTIO_NET_HDR` stays clear so guest TX metadata reaches TAP intact;
CSUM/TSO bits are TAP capabilities, not VHOST_SET_FEATURES bits. Linux converts
the header to checksum/GSO state and uses software segmentation if the uplink
does not provide hardware offload. `TUNSETOFFLOAD` remains zero because it
controls Linux-to-guest traffic: guest RX must still receive completed checksums
and ordinary MTU-sized frames. The control protocol layout remains version 3.

Linux-side tests cover feature dependency rejection, TAP resource configuration,
reset/rebind, setup failures and partial queue attachment rollback. A fixture on
each pinned kernel profile bridges two TAPs with receive offloads disabled,
checking TCPv4/TCPv6 checksum
completion, segmentation sizes, sequence numbers and payload bytes, plus malformed
GSO rejection under default and finite TX budgets. It also checks per-TAP policy
isolation and RX queue resizing. HypeR's cross-VM acceptance additionally verifies
bidirectional HTTP payloads, reset/rebind and stop/start. These tests have no performance
thresholds.

Device reset drains only that device. `RELEASE_MEMORY` drains both devices before
unmapping RAM and acknowledging the grant's retirement. Failure of either drain
retains the mapping and withholds the retirement proof.

Network notifications carry `hyper,device-kind = "network"` beside
`hyper,client-id`, producing `/dev/hyper-io-notification-N-net`. A network-only
client uses this route for memory admission and retirement; it has no hidden
storage notification or LIO target. Native client 0 cannot request networking.

## Hotplugged virtio serial ports

The common AArch64 kernel enables PCIe port services, PCIe hotplug and the
standard virtio-console driver. A HypeR formal runtime can add a named virtio
serial port behind a root port that was present when this appliance booted.
Linux discovers the function from a slot-change interrupt; it does not require
a reboot or a manual PCI rescan.

This kernel configuration does not create ports or change `console=ttyAMA0`.
Device presentation and port access belong to the HypeR runtime; the agent
supervisor below waits for its named port. Existing storage and network
protocols remain unchanged.

## Guest usage agent

The I/O role starts the pinned upstream QEMU guest agent as a separate process.
It discovers `org.qemu.guest_agent.0` by its sysfs port name, including when the
virtio-serial controller appears after boot. Node numbers are not fixed. A lost
port causes rediscovery; agent failure does not restart or stop storage/network
backends. Userspace console selection and UART kernel diagnostics are unchanged.

Only `guest-sync`, `guest-sync-delimited`, `guest-ping`, `guest-info`,
`guest-get-cpustats`, and `guest-hyper-get-memory-stats` are enabled. File access,
command execution, shutdown, freeze and other mutating RPCs are disabled. CPU
statistics use the standard upstream command. The small appliance-specific memory
command reads and closes `/proc/meminfo` in one request and returns bytes for
MemTotal, MemAvailable, MemFree, Buffers, Cached and SReclaimable; it creates no
remote file handles. It is a QGA extension, not a standard upstream command.

`tools/guest_agent.py` builds a static AArch64 Linux agent using checksum-pinned
sources and QEMU's bundled Python wheels. Its build dependencies include
`python3-venv`, `ninja-build` and `pkg-config`. The appliance's existing archive
size limits still apply. Matching dependency archives, patches and build scripts
are included and checked in the corresponding-source package.

Both platform builds run `tests/io-vm/guest-agent.py`: boot without a serial
controller, hotplug it through PCIe, sample both CPUs and memory, reject unsafe
RPCs and reconnect the host stream. HypeR's own integration tests cover its
userspace device transport and read-only `vmm` observation path.
