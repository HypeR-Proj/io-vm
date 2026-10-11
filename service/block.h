// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_BLOCK_H
#define HYPER_BLOCK_H
#include "hyper_io.h"
#include <stddef.h>
#include <sys/types.h>
struct backend;
struct block_owner;
struct hyper_block_range;
int block_owner_create(struct block_owner **, const char *);
void block_owner_destroy(struct block_owner *);
int block_target_ready(const struct backend *);
int block_target_publish(unsigned, dev_t, unsigned,
			 const struct hyper_block_range *);
unsigned block_dispatch(struct backend *, const unsigned char *, size_t, void *,
			size_t *);
#endif
