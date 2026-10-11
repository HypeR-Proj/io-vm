// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_BLOCK_POLICY_H
#define HYPER_BLOCK_POLICY_H
#include <stdint.h>

#define HYPER_BLOCK_CLIENTS 128U
#define HYPER_BLOCK_DISKS 128U
struct hyper_block_range {
	uint64_t disk, first, count;
	unsigned read_only;
};
/* Subtraction proves both range bounds and overflow freedom. Geometry and
 * authority are checked before a range can reach device mapper. */
static inline int hyper_block_range_valid(uint64_t bytes, unsigned sector,
					  unsigned disk_read_only,
					  const struct hyper_block_range *range)
{
	if (sector < 512 || sector > 65536 || (sector & (sector - 1)) ||
	    !bytes || bytes % sector || !range->disk || !range->count ||
	    range->read_only > 1 || (disk_read_only && !range->read_only))
		return 0;
	uint64_t sectors = bytes / sector;
	return range->first < sectors && range->count <= sectors - range->first;
}
static inline int hyper_block_ranges_conflict(const struct hyper_block_range *a,
					      const struct hyper_block_range *b)
{
	if (!a->disk || a->disk != b->disk || (a->read_only && b->read_only))
		return 0;
	/* Called only for already validated, non-wrapping ranges. */
	return a->first < b->first + b->count && b->first < a->first + a->count;
}
static inline int hyper_block_read_valid(uint64_t bytes, unsigned sector,
					 uint64_t offset, uint64_t length,
					 uint64_t memory_bytes,
					 uint64_t memory_offset)
{
	return sector && length && length <= (1U << 20) && !(offset % sector) &&
	       !(length % sector) && length <= bytes &&
	       offset <= bytes - length && length <= memory_bytes &&
	       memory_offset <= memory_bytes - length;
}
#endif
