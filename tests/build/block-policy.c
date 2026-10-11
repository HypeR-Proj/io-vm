// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#include "hyper_block_policy.h"
#include <assert.h>

int main(void)
{
	struct hyper_block_range a = {1, 4, 8, 0}, b = {1, 12, 4, 0};
	assert(hyper_block_range_valid(16 * 512, 512, 0, &a));
	assert(hyper_block_range_valid(16 * 4096, 4096, 0, &a));
	assert(!hyper_block_range_valid(16 * 512, 1000, 0, &a));
	assert(!hyper_block_range_valid(16 * 512 + 1, 512, 0, &a));
	assert(!hyper_block_range_valid(16 * 512, 512, 1, &a));
	assert(!hyper_block_ranges_conflict(&a, &b));
	b.first = 11;
	assert(hyper_block_ranges_conflict(&a, &b));
	b.disk = 2;
	assert(!hyper_block_ranges_conflict(&a, &b));
	b.disk = 1;
	a.read_only = b.read_only = 1;
	assert(!hyper_block_ranges_conflict(&a, &b));
	assert(hyper_block_range_valid(16 * 512, 512, 1, &a));
	a.first = UINT64_MAX;
	assert(!hyper_block_range_valid(UINT64_MAX - 511, 512, 0, &a));
	a.first = 0;
	a.count = UINT64_MAX;
	assert(!hyper_block_range_valid(UINT64_MAX - 511, 512, 0, &a));
	a.count = 0;
	assert(!hyper_block_range_valid(16 * 512, 512, 0, &a));
	assert(hyper_block_read_valid(4096, 512, 3584, 512, 4096, 3584));
	assert(!hyper_block_read_valid(4096, 512, 4096, 512, 4096, 0));
	assert(!hyper_block_read_valid(4096, 512, 0, 512, 4096, 4096));
	assert(!hyper_block_read_valid(4096, 512, 1, 512, 4096, 0));
	assert(!hyper_block_read_valid(4096, 512, 0, 0, 4096, 0));
	assert(!hyper_block_read_valid(UINT64_MAX, 512, UINT64_MAX - 511, 512,
				       4096, 0));
	assert(
	    !hyper_block_read_valid(1U << 22, 512, 0, 1U << 21, 1U << 22, 0));
	return 0;
}
