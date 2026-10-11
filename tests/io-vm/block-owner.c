// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
/* Run only with the disposable disk from block-owner.py. This exercises the
 * real owner dispatcher, Linux device mapper and LIO publication. */
#define _GNU_SOURCE
#include "../../service/backend.h"
#include "../../service/block.h"
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned char scratch[1U << 20];
static union {
	struct hyper_io_network_reply network;
	struct hyper_io_block_info block;
} reply;

static unsigned exchange(struct backend *backend, void *request, unsigned size)
{
	struct hyper_io_header *header = request;
	header->length = htole32(size);
	memset(&reply, 0, sizeof(reply));
	size_t reply_size = 48;
	return block_dispatch(backend, request, size, &reply, &reply_size);
}
int main(void)
{
	struct backend backend = {.memory = scratch,
				  .region.length = sizeof(scratch)};
	assert(!block_owner_create(&backend.blocks, "/etc/hyper-clients.conf"));
	/* devtmpfs can appear after the owner starts. A new enumeration must
	 * discover added disks without renumbering previously issued IDs. */
	assert(!rename("/dev/sda", "/dev/late-first"));
	assert(!rename("/dev/sdb", "/dev/late-second"));
	struct hyper_io_header hello = {.operation =
					    htole16(HYPER_IO_BLOCK_HELLO)};
	assert(exchange(&backend, &hello, sizeof(hello)) == HYPER_IO_OK);
	assert(le64toh(reply.network.common.features) == 7);
	struct hyper_io_block_enum query = {.header.operation =
						htole16(HYPER_IO_BLOCK_ENUM)};
	assert(exchange(&backend, &query, sizeof(query)) == HYPER_IO_OK);
	assert(!reply.block.disk);
	assert(!rename("/dev/late-first", "/dev/sda"));
	assert(exchange(&backend, &query, sizeof(query)) == HYPER_IO_OK);
	uint64_t disk = le64toh(reply.block.disk);
	assert(disk && le64toh(reply.block.bytes) == (16U << 20));
	assert(le32toh(reply.block.logical_sector) == 512);
	assert(!rename("/dev/late-second", "/dev/sdb"));
	assert(exchange(&backend, &query, sizeof(query)) == HYPER_IO_OK);
	assert(le64toh(reply.block.disk) == disk);
	query.cursor = reply.block.next_cursor;
	assert(exchange(&backend, &query, sizeof(query)) == HYPER_IO_OK);
	uint64_t second_disk = le64toh(reply.block.disk);
	assert(second_disk && second_disk != disk);
	assert(le64toh(reply.block.bytes) == (32U << 20));
	assert(le32toh(reply.block.logical_sector) == 4096);
	query.cursor = reply.block.next_cursor;
	assert(exchange(&backend, &query, sizeof(query)) == HYPER_IO_OK);
	assert(!reply.block.disk && !reply.block.bytes &&
	       !reply.block.next_cursor);
	query.cursor = htole32(128);
	assert(exchange(&backend, &query, sizeof(query)) == HYPER_IO_INVALID);
	struct hyper_io_block_read read = {.header.operation =
					       htole16(HYPER_IO_BLOCK_READ),
					   .disk = htole64(disk),
					   .length = htole64(512)};
	assert(exchange(&backend, &read, sizeof(read)) == HYPER_IO_OK);
	assert(!memcmp(scratch, "owner-disk", 10));
	read.memory_offset = htole64(sizeof(scratch));
	assert(exchange(&backend, &read, sizeof(read)) == HYPER_IO_INVALID);
	read.memory_offset = 0;
	read.offset = htole64(UINT64_MAX - 511);
	assert(exchange(&backend, &read, sizeof(read)) == HYPER_IO_INVALID);
	struct hyper_io_block_bind binding = {.header.operation =
						  htole16(HYPER_IO_BLOCK_BIND),
					      .client = htole32(1),
					      .disk = htole64(disk),
					      .first_lba = htole64(64),
					      .sectors = htole64(128)};
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_OK);
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_OK);
	strcpy(backend.target.vhost_wwpn, "naa.5001405000000001");
	assert(block_target_ready(&backend));
	binding.sectors = htole64(129);
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_BUSY);
	binding.client = htole32(2);
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_BUSY);
	binding.first_lba = htole64(192);
	binding.sectors = htole64(64);
	binding.flags = htole32(HYPER_IO_BLOCK_READ_ONLY);
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_OK);
	binding.client = htole32(3);
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_OK);
	binding.client = htole32(4);
	binding.disk = htole64(second_disk);
	binding.first_lba = htole64(4);
	binding.sectors = htole64(8);
	assert(exchange(&backend, &binding, sizeof(binding)) == HYPER_IO_OK);
	int fd = open("/dev/mapper/hyper-client-1", O_RDONLY);
	assert(fd >= 0);
	uint64_t bytes;
	assert(!ioctl(fd, BLKGETSIZE64, &bytes) && bytes == 128 * 512);
	assert(pread(fd, scratch, 512, 0) == 512);
	assert(!memcmp(scratch, "range-one", 9));
	assert(pread(fd, scratch, 512, bytes) == 0);
	close(fd);
	fd = open("/dev/mapper/hyper-client-2", O_RDONLY);
	assert(fd >= 0);
	unsigned ro = 0;
	assert(!ioctl(fd, BLKROGET, &ro) && ro);
	assert(pread(fd, scratch, 512, 0) == 512);
	assert(!memcmp(scratch, "range-two", 9));
	close(fd);
	fd = open("/dev/mapper/hyper-client-2", O_WRONLY);
	if (fd >= 0) {
		assert(pwrite(fd, scratch, 512, 0) < 0);
		close(fd);
	}
	fd = open("/dev/mapper/hyper-client-4", O_RDONLY);
	assert(fd >= 0);
	assert(!ioctl(fd, BLKGETSIZE64, &bytes) && bytes == 8 * 4096);
	assert(pread(fd, scratch, 4096, 0) == 4096);
	assert(!memcmp(scratch, "second-disk-range", 17));
	close(fd);
	struct backend guest = {0};
	assert(exchange(&guest, &binding, sizeof(binding)) ==
	       HYPER_IO_UNSUPPORTED);
	block_owner_destroy(backend.blocks);
	puts("HypeR block owner: enumeration, reads, range isolation, "
	     "read-only and immutable bindings PASS");
	return 0;
}
