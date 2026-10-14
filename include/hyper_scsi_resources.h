// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_SCSI_RESOURCES_H
#define HYPER_SCSI_RESOURCES_H

#include <stdint.h>
#include <sys/ioctl.h>

/* Appliance-local vhost-scsi extension; fixed-width and pointer-free. */
struct hyper_scsi_resources {
	uint32_t request_queues, sg_prealloc, reserved[2];
};
#define HYPER_VHOST_SCSI_SET_RESOURCES _IOW(0xaf, 0x4f, struct hyper_scsi_resources)
#define HYPER_VHOST_SCSI_GET_RESOURCES _IOR(0xaf, 0x4f, struct hyper_scsi_resources)

#define HYPER_SCSI_REQUEST_QUEUES 4
#define HYPER_SCSI_SG_PREALLOC 128

static inline int hyper_scsi_resources_valid(unsigned queues, unsigned sg)
{
	return queues >= 1 && queues <= HYPER_SCSI_REQUEST_QUEUES &&
	       sg >= 2 && sg <= 2048;
}

#endif
