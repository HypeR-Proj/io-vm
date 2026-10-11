// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_CLIENTS_H
#define HYPER_CLIENTS_H
#include "hyper_block_policy.h"
struct hyper_client {
	unsigned id;
	char network[32], mac[18];
};
/* This describes transport endpoints and network policy only. Storage ranges
 * arrive exclusively from the Native owner after its partition discovery. */
int hyper_clients_read(const char *, struct hyper_client[HYPER_BLOCK_CLIENTS],
		       unsigned *);
#endif
