// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include "backend.h"
#include "block.h"
#include "hyper_block_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/dm-ioctl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

union dm_buffer {
	struct dm_ioctl alignment;
	unsigned char bytes[4096];
};
static struct dm_ioctl *dm_request(union dm_buffer *buffer, const char *name)
{
	memset(buffer, 0, sizeof(*buffer));
	struct dm_ioctl *request = &buffer->alignment;
	request->version[0] = DM_VERSION_MAJOR;
	request->version[1] = DM_VERSION_MINOR;
	request->version[2] = DM_VERSION_PATCHLEVEL;
	request->data_size = sizeof(*buffer);
	request->data_start = sizeof(*request);
	snprintf(request->name, sizeof(request->name), "%s", name);
	return request;
}
static int mapper_create(int control, const char *name, dev_t device,
			 unsigned logical,
			 const struct hyper_block_range *range)
{
	union dm_buffer buffer;
	struct dm_ioctl *request = dm_request(&buffer, name);
	if (ioctl(control, DM_DEV_CREATE, request))
		return -1;
	dev_t mapper_device = request->dev;
	request = dm_request(&buffer, name);
	request->target_count = 1;
	request->flags = range->read_only ? DM_READONLY_FLAG : 0;
	struct dm_target_spec *target =
	    (void *)(buffer.bytes + request->data_start);
	uint64_t multiplier = logical / 512;
	target->length = range->count * multiplier;
	strcpy(target->target_type, "linear");
	char *parameters = (char *)(target + 1);
	int length = snprintf(parameters, 128, "%u:%u %" PRIu64, major(device),
			      minor(device), range->first * multiplier);
	target->next = (sizeof(*target) + length + 1 + 7) & ~7U;
	if (ioctl(control, DM_TABLE_LOAD, request))
		goto failed;
	request = dm_request(&buffer, name);
	request->flags = range->read_only ? DM_READONLY_FLAG : 0;
	if (ioctl(control, DM_DEV_SUSPEND, request))
		goto failed;
	char path[96];
	snprintf(path, sizeof(path), "/dev/mapper/%s", name);
	if (mknod(path, S_IFBLK | 0600, mapper_device))
		goto failed;
	return 0;
failed:
	/* No publication occurred. Never unlink a node which failed mknod. */
	if (ioctl(control, DM_DEV_REMOVE, dm_request(&buffer, name)))
		perror("HypeR I/O: mapper rollback");
	return -1;
}
static int write_attribute(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	size_t size = strlen(value);
	ssize_t written;
	do {
		written = write(fd, value, size);
	} while (written < 0 && errno == EINTR);
	int result = written == (ssize_t)size ? 0 : -1;
	if (close(fd))
		result = -1;
	return result;
}
static int target_create(unsigned client, const char *mapper,
			 unsigned read_only)
{
	char core[160], target[160], group[176], lun[192], path[224],
	    value[160];
	snprintf(core, sizeof(core),
		 "/sys/kernel/config/target/core/iblock_0/%s", mapper);
	snprintf(target, sizeof(target),
		 "/sys/kernel/config/target/vhost/naa.5001405%09x", client);
	snprintf(group, sizeof(group), "%s/tpgt_1", target);
	snprintf(lun, sizeof(lun), "%s/lun/lun_0", group);
	unsigned made = 0;
	if (mkdir(core, 0700))
		return -1;
	made = 1;
	snprintf(path, sizeof(path), "%s/control", core);
	snprintf(value, sizeof(value), "udev_path=/dev/mapper/%s,readonly=%u\n",
		 mapper, read_only);
	if (write_attribute(path, value))
		goto failed;
	snprintf(path, sizeof(path), "%s/enable", core);
	if (write_attribute(path, "1\n") || mkdir(target, 0700))
		goto failed;
	made = 2;
	if (mkdir(group, 0700))
		goto failed;
	made = 3;
	if (mkdir(lun, 0700))
		goto failed;
	made = 4;
	snprintf(path, sizeof(path), "%s/nexus", group);
	if (write_attribute(path, "naa.5001405000000002\n"))
		goto failed;
	made = 5;
	/* This symlink is the only publication point. Data HELLO/ACTIVATE check
	 * it; nothing may observe the half-built LUN before all setup succeeds.
	 */
	snprintf(path, sizeof(path), "%s/disk", lun);
	if (symlink(core, path))
		goto failed;
	return 0;
failed:
	fprintf(stderr, "HypeR I/O: target setup failed (%s): %s\n", path,
		strerror(errno));
	if (made >= 5) {
		snprintf(path, sizeof(path), "%s/nexus", group);
		if (write_attribute(path, "NULL\n"))
			perror("HypeR I/O: nexus rollback");
	}
	if (made >= 4 && rmdir(lun))
		perror("HypeR I/O: LUN rollback");
	if (made >= 3 && rmdir(group))
		perror("HypeR I/O: target group rollback");
	if (made >= 2 && rmdir(target))
		perror("HypeR I/O: target rollback");
	if (made >= 1 && rmdir(core))
		perror("HypeR I/O: block object rollback");
	return -1;
}
int block_target_ready(const struct backend *backend)
{
	static const char format[] =
	    "/sys/kernel/config/target/vhost/%s/tpgt_1/lun/lun_0/disk";
	char path[sizeof(format) + sizeof(backend->target.vhost_wwpn)];
	if (!backend->target.vhost_wwpn[0])
		return 0;
	int length = snprintf(path, sizeof(path), format,
			      backend->target.vhost_wwpn);
	return length >= 0 && (size_t)length < sizeof(path) &&
	       !access(path, F_OK);
}
int block_target_publish(unsigned client, dev_t device, unsigned logical,
			 const struct hyper_block_range *range)
{
	char mapper[64];
	snprintf(mapper, sizeof(mapper), "hyper-client-%u", client);
	if ((mkdir("/dev/mapper", 0755) && errno != EEXIST) ||
	    (mkdir("/sys/kernel/config/target/core/iblock_0", 0700) &&
	     errno != EEXIST) ||
	    (mkdir("/sys/kernel/config/target/vhost", 0700) && errno != EEXIST))
		return -1;
	int control = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
	if (control < 0)
		return -1;
	int result = mapper_create(control, mapper, device, logical, range);
	if (!result && target_create(client, mapper, range->read_only)) {
		union dm_buffer buffer;
		if (ioctl(control, DM_DEV_REMOVE, dm_request(&buffer, mapper)))
			perror("HypeR I/O: binding rollback");
		else {
			char path[96];
			snprintf(path, sizeof(path), "/dev/mapper/%s", mapper);
			if (unlink(path))
				perror("HypeR I/O: device node rollback");
		}
		result = -1;
	}
	close(control);
	if (result)
		return -1;
	return 0;
}
