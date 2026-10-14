// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_CLIENTS_H
#define HYPER_CLIENTS_H
#include "hyper_block_policy.h"
struct hyper_client {
	unsigned id;
	unsigned request_queues, sg_prealloc;
	char network[32], mac[18];
};
/* Transport endpoints, network policy and per-client SCSI resources. Storage ranges
 * arrive exclusively from the Native owner after its partition discovery. */
int hyper_clients_read(const char *, struct hyper_client[HYPER_BLOCK_CLIENTS],
		       unsigned *);
#endif
