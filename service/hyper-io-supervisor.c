// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L
#include "hyper_clients.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop_signal(int signal)
{
	(void)signal;
	stopping = 1;
}
int main(int argc, char **argv)
{
	struct hyper_client clients[HYPER_BLOCK_CLIENTS];
	unsigned count;
	pid_t children[HYPER_BLOCK_CLIENTS] = {0};
	if (argc != 2)
		return 2;
	if (hyper_clients_read(argv[1], clients, &count))
		return 1;
	struct sigaction action = {.sa_handler = stop_signal};
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTERM, &action, NULL) ||
	    sigaction(SIGINT, &action, NULL))
		return 1;
	unsigned live = 0;
	int failed = 0;
	for (unsigned i = 0; i < count; ++i) {
		if (stopping)
			break;
		pid_t pid = fork();
		if (pid < 0) {
			failed = 1;
			stopping = 1;
			break;
		}
		if (!pid) {
			char memory[64];
			char queues[16], sg[16];
			char rx[16], tx[16];
			snprintf(queues, sizeof(queues), "%u", clients[i].request_queues);
			snprintf(sg, sizeof(sg), "%u", clients[i].sg_prealloc);
			snprintf(rx, sizeof(rx), "%u",
				 clients[i].net_resources.rx_backlog_packets);
			snprintf(tx, sizeof(tx), "%u",
				 clients[i].net_resources.tx_buffer_bytes);
			char control[64];
			char notification[64], net_notification[64], target[32];
			snprintf(memory, sizeof(memory), "/dev/hyper-memory-%u",
				 clients[i].id);
			snprintf(control, sizeof(control),
				 "/dev/hyper-io-control-%u", clients[i].id);
			snprintf(notification, sizeof(notification),
				 "/dev/hyper-io-notification-%u", clients[i].id);
			snprintf(net_notification, sizeof(net_notification),
				 "/dev/hyper-io-notification-%u-net", clients[i].id);
			if (!clients[i].id)
				snprintf(target, sizeof(target), "owner");
			else if (!access(notification, F_OK))
				snprintf(target, sizeof(target), "naa.5001405%09x", clients[i].id);
			else
				snprintf(target, sizeof(target), "-");
			execl("/usr/bin/hyper-io-service", "hyper-io-service",
			      memory, target, control, notification,
			      clients[i].network, clients[i].mac, net_notification,
			      queues, sg, rx, tx,
			      (char *)NULL);
			_exit(127);
		}
		children[i] = pid;
		++live;
	}
	int signalled = 0;
	while (live) {
		if (stopping && !signalled) {
			for (unsigned i = 0; i < count; ++i)
				if (children[i])
					kill(children[i], SIGTERM);
			signalled = 1;
		}
		int status;
		pid_t pid = waitpid(-1, &status, 0);
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
		for (unsigned i = 0; i < count; ++i)
			if (children[i] == pid) {
				children[i] = 0;
				--live;
				break;
			}
		if (!WIFEXITED(status) || WEXITSTATUS(status)) {
			failed = 1;
			stopping = 1;
		}
		/* A normally closed control endpoint retires only that client.
		 * The other services continue; RELEASE_MEMORY supports reuse
		 * without closing it. */
	}
	return failed ? 1 : 0;
}
