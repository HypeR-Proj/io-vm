// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_RANDOM_H
#define HYPER_RANDOM_H
#include "hyper_io.h"
#include <endian.h>
#include <errno.h>
#include <string.h>
#include <sys/random.h>

/* No fallback to uninitialized entropy, timestamps or a Native PRNG. The
 * initialized Linux random subsystem owns generation and source selection. */
static inline unsigned random_request(const unsigned char *message, size_t size,
				      struct hyper_io_random_reply *reply,
				      size_t *reply_size)
{
	struct hyper_io_random request;
	*reply_size = 48;
	reply->bytes = 0;
	explicit_bzero(reply->data, sizeof(reply->data));
	if (size != sizeof(request))
		return HYPER_IO_INVALID;
	memcpy(&request, message, sizeof(request));
	unsigned bytes = le32toh(request.bytes);
	if (!bytes || bytes > HYPER_IO_RANDOM_MAX || request.reserved)
		return HYPER_IO_INVALID;
	ssize_t result = getrandom(reply->data, bytes, GRND_NONBLOCK);
	if (result != (ssize_t)bytes) {
		unsigned status = result < 0 && errno == EAGAIN ? HYPER_IO_NOT_READY :
			result < 0 && errno == ENOSYS ? HYPER_IO_UNSUPPORTED :
			HYPER_IO_BACKEND_FAILURE;
		explicit_bzero(reply->data, sizeof(reply->data));
		return status;
	}
	reply->bytes = htole32(bytes);
	*reply_size = 48 + bytes;
	return HYPER_IO_OK;
}
#endif
