// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include "../../service/random.h"
#include <asm/hwcap.h>
#include <assert.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <unistd.h>

int main(void)
{
	/* This fixture deliberately supplies RNDR with QEMU's max CPU. Production
	 * does not require RNDR: Linux can also seed from assigned hardware RNGs. */
	if (!(getauxval(AT_HWCAP2) & HWCAP2_RNG)) {
		fputs("Random fixture unsupported: QEMU CPU must expose RNDR\n", stderr);
		return 1;
	}
	struct hyper_io_random request = {.bytes = htole32(HYPER_IO_RANDOM_MAX)};
	struct hyper_io_random_reply reply = {0};
	unsigned status = HYPER_IO_NOT_READY;
	size_t size = 48;
	for (unsigned attempt = 0; attempt < 100; ++attempt) {
		status = random_request((void *)&request, sizeof(request), &reply, &size);
		if (status != HYPER_IO_NOT_READY)
			break;
		usleep(10000);
	}
	assert(status == HYPER_IO_OK && size == 240);
	assert(le32toh(reply.bytes) == HYPER_IO_RANDOM_MAX);
	explicit_bzero(&reply, sizeof(reply));
	request.bytes = htole32(1);
	assert(random_request((void *)&request, sizeof(request), &reply, &size) == HYPER_IO_OK);
	assert(size == 49 && le32toh(reply.bytes) == 1);
	explicit_bzero(&reply, sizeof(reply));
	puts("Initialized Linux random: RNDR source and bounded requests PASS");
	return 0;
}
