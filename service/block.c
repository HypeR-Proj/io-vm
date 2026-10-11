// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include "block.h"
#include "backend.h"
#include "hyper_block_policy.h"
#include "hyper_clients.h"
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct physical_disk {
	int fd;
	dev_t device;
	uint64_t bytes, sequence;
	unsigned logical, physical, read_only;
};
struct block_owner {
	struct physical_disk disks[HYPER_BLOCK_DISKS];
	unsigned disk_count;
	unsigned clients[HYPER_BLOCK_CLIENTS];
	struct hyper_block_range bindings[HYPER_BLOCK_CLIENTS];
};

/* Keep read-only open files, rather than reopening disk names supplied by the
 * caller. An ID cannot silently select a replacement device during this boot.
 */
static int discover_disks(struct block_owner *owner)
{
	struct dirent **entries;
	int count = scandir("/sys/class/block", &entries, NULL, alphasort);
	if (count < 0)
		return -1;
	int result = 0;
	for (int i = 0; i < count; ++i) {
		char path[512];
		const char *name = entries[i]->d_name;
		if (name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/block/%s/partition",
			 name);
		if (!access(path, F_OK))
			continue;
		/* Virtual mapper/loop/RAM devices are not backing hardware. */
		snprintf(path, sizeof(path), "/sys/class/block/%s/device",
			 name);
		if (access(path, F_OK))
			continue;
		struct physical_disk disk = {.fd = -1};
		snprintf(path, sizeof(path), "/dev/%s", name);
		disk.fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		/* devtmpfs publication can lag asynchronous driver discovery.
		 */
		if (disk.fd < 0 &&
		    (errno == ENOENT || errno == ENXIO || errno == ENOMEDIUM))
			continue;
		struct stat status;
		if (disk.fd < 0 || fstat(disk.fd, &status) ||
		    !S_ISBLK(status.st_mode) ||
		    ioctl(disk.fd, BLKGETSIZE64, &disk.bytes) ||
		    ioctl(disk.fd, BLKGETDISKSEQ, &disk.sequence) ||
		    ioctl(disk.fd, BLKSSZGET, &disk.logical) ||
		    ioctl(disk.fd, BLKPBSZGET, &disk.physical) ||
		    ioctl(disk.fd, BLKROGET, &disk.read_only) || !disk.bytes ||
		    disk.logical < 512 || disk.logical > 65536 ||
		    (disk.logical & (disk.logical - 1)) ||
		    disk.bytes % disk.logical || disk.physical < disk.logical ||
		    (disk.physical & (disk.physical - 1))) {
			if (disk.fd >= 0)
				close(disk.fd);
			result = -1;
			break;
		}
		disk.device = status.st_rdev;
		unsigned existing;
		for (existing = 0; existing < owner->disk_count; ++existing)
			if (owner->disks[existing].device == disk.device)
				break;
		if (existing < owner->disk_count) {
			/* Recycled device numbers must not redirect a
			 * previously issued ID. Replacement requires an
			 * appliance restart. */
			if (owner->disks[existing].sequence != disk.sequence ||
			    owner->disks[existing].bytes != disk.bytes ||
			    owner->disks[existing].logical != disk.logical)
				result = -1;
			close(disk.fd);
			if (result)
				break;
			continue;
		}
		if (owner->disk_count == HYPER_BLOCK_DISKS) {
			close(disk.fd);
			result = -1;
			break;
		}
		owner->disks[owner->disk_count++] = disk;
	}
	for (int i = 0; i < count; ++i)
		free(entries[i]);
	free(entries);
	return result;
}

int block_owner_create(struct block_owner **output, const char *clients_path)
{
	struct hyper_client clients[HYPER_BLOCK_CLIENTS];
	unsigned count;
	struct block_owner *owner = calloc(1, sizeof(*owner));
	if (!owner)
		return -1;
	if (hyper_clients_read(clients_path, clients, &count))
		goto failed;
	for (unsigned i = 0; i < count; ++i) {
		char notification[80];
		snprintf(notification, sizeof(notification),
			 "/dev/hyper-io-notification-%u", clients[i].id);
		owner->clients[clients[i].id] =
		    clients[i].id && !access(notification, F_OK);
	}
	*output = owner;
	return 0;
failed:
	block_owner_destroy(owner);
	return -1;
}
void block_owner_destroy(struct block_owner *owner)
{
	if (!owner)
		return;
	for (unsigned i = 0; i < owner->disk_count; ++i)
		close(owner->disks[i].fd);
	/* Published ranges live until appliance shutdown. Other client
	 * processes may still own vhost requests here, so manager teardown
	 * never removes them. */
	free(owner);
}

static int disk_current(const struct physical_disk *disk)
{
	uint64_t sequence, bytes;
	return !ioctl(disk->fd, BLKGETDISKSEQ, &sequence) &&
	       !ioctl(disk->fd, BLKGETSIZE64, &bytes) &&
	       sequence == disk->sequence && bytes == disk->bytes;
}

static unsigned bind_range(struct block_owner *owner,
			   const struct hyper_io_block_bind *request)
{
	unsigned client = le32toh(request->client);
	struct hyper_block_range range = {.disk = le64toh(request->disk),
					  .first = le64toh(request->first_lba),
					  .count = le64toh(request->sectors),
					  .read_only = le32toh(request->flags)};
	if (!client || client >= HYPER_BLOCK_CLIENTS ||
	    !owner->clients[client] || !range.disk ||
	    range.disk > owner->disk_count)
		return HYPER_IO_INVALID;
	struct physical_disk *disk = &owner->disks[range.disk - 1];
	if (!disk_current(disk))
		return HYPER_IO_BACKEND_FAILURE;
	if (!hyper_block_range_valid(disk->bytes, disk->logical,
				     disk->read_only, &range))
		return HYPER_IO_INVALID;
	const struct hyper_block_range *old = &owner->bindings[client];
	if (old->disk)
		return old->disk == range.disk && old->first == range.first &&
			       old->count == range.count &&
			       old->read_only == range.read_only
			   ? HYPER_IO_OK
			   : HYPER_IO_BUSY;
	for (unsigned i = 1; i < HYPER_BLOCK_CLIENTS; ++i)
		if (hyper_block_ranges_conflict(&owner->bindings[i], &range))
			return HYPER_IO_BUSY;
	if (block_target_publish(client, disk->device, disk->logical, &range))
		return HYPER_IO_BACKEND_FAILURE;
	owner->bindings[client] = range;
	return HYPER_IO_OK;
}

unsigned block_dispatch(struct backend *backend, const unsigned char *message,
			size_t size, void *reply, size_t *reply_size)
{
	struct hyper_io_header header;
	memcpy(&header, message, sizeof(header));
	if (!backend->blocks)
		return HYPER_IO_UNSUPPORTED;
	unsigned operation = le16toh(header.operation);
	if (operation == HYPER_IO_BLOCK_HELLO && size == 40) {
		struct hyper_io_reply *response = reply;
		response->features = htole64(7);
		response->queues = htole32(HYPER_IO_BLOCK_MAX_READ);
		*reply_size = 64;
		return HYPER_IO_OK;
	}
	if (operation == HYPER_IO_BLOCK_ENUM && size == 48) {
		struct hyper_io_block_enum request;
		memcpy(&request, message, sizeof(request));
		unsigned cursor = le32toh(request.cursor);
		if (request.reserved)
			return HYPER_IO_INVALID;
		/* Start each enumeration with an append-only hardware refresh.
		 * Native may retry while MMC/SCSI completes discovery. */
		if (!cursor && discover_disks(backend->blocks))
			return HYPER_IO_BACKEND_FAILURE;
		if (cursor > backend->blocks->disk_count)
			return HYPER_IO_INVALID;
		struct hyper_io_block_info result = {0};
		struct hyper_io_block_info *response = &result;
		*reply_size = 80;
		if (cursor == backend->blocks->disk_count)
			return HYPER_IO_OK;
		const struct physical_disk *disk =
		    &backend->blocks->disks[cursor];
		response->disk = htole64(cursor + 1);
		response->bytes = htole64(disk->bytes);
		response->logical_sector = htole32(disk->logical);
		response->physical_sector = htole32(disk->physical);
		response->flags =
		    htole32(disk->read_only ? HYPER_IO_BLOCK_READ_ONLY : 0);
		response->next_cursor = htole32(cursor + 1);
		memcpy((unsigned char *)reply + 48, &result.disk, 32);
		return HYPER_IO_OK;
	}
	if (operation == HYPER_IO_BLOCK_READ && size == 72) {
		struct hyper_io_block_read request;
		memcpy(&request, message, sizeof(request));
		uint64_t id = le64toh(request.disk),
			 offset = le64toh(request.offset);
		uint64_t length = le64toh(request.length),
			 memory_offset = le64toh(request.memory_offset);
		if (!id || id > backend->blocks->disk_count ||
		    backend->memory == MAP_FAILED)
			return HYPER_IO_INVALID;
		const struct physical_disk *disk =
		    &backend->blocks->disks[id - 1];
		if (!disk_current(disk))
			return HYPER_IO_BACKEND_FAILURE;
		if (!hyper_block_read_valid(disk->bytes, disk->logical, offset,
					    length, backend->region.length,
					    memory_offset) ||
		    offset > INT64_MAX || length > INT64_MAX - offset)
			return HYPER_IO_INVALID;
		unsigned char *destination =
		    (unsigned char *)backend->memory + memory_offset;
		for (uint64_t done = 0; done < length;) {
			ssize_t received = pread(disk->fd, destination + done,
						 length - done, offset + done);
			if (received < 0 && errno == EINTR)
				continue;
			if (received <= 0)
				return HYPER_IO_BACKEND_FAILURE;
			done += received;
		}
		return HYPER_IO_OK;
	}
	if (operation == HYPER_IO_BLOCK_BIND && size == 72) {
		struct hyper_io_block_bind request;
		memcpy(&request, message, sizeof(request));
		return bind_range(backend->blocks, &request);
	}
	return HYPER_IO_INVALID;
}
