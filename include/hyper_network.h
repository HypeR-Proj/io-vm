// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#ifndef HYPER_NETWORK_H
#define HYPER_NETWORK_H
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define HYPER_MAX_NETWORKS 8
/* TAP's ether_setup overrides tun_setup's initial queue length. */
#define HYPER_NET_RX_BACKLOG_PACKETS 1000
#define HYPER_NET_RX_BACKLOG_MAX 65536
#define HYPER_NET_TX_BUFFER_MIN 65536
struct hyper_network_resources {
	unsigned rx_backlog_packets;
	/* Zero preserves Linux's unlimited sndbuf and its batching fast paths. */
	unsigned tx_buffer_bytes;
};
static inline int hyper_network_resource_number(const char *text,
					       unsigned *value)
{
	unsigned result = 0;
	if (!*text)
		return -1;
	for (; *text; ++text) {
		if (*text < '0' || *text > '9')
			return -1;
		unsigned digit = (unsigned)(*text - '0');
		if (result > ((unsigned)INT_MAX - digit) / 10)
			return -1;
		result = result * 10 + digit;
	}
	*value = result;
	return 0;
}
static inline int hyper_network_resources_parse(
	const char *rx, const char *tx, struct hyper_network_resources *resources)
{
	struct hyper_network_resources result;
	if (hyper_network_resource_number(rx, &result.rx_backlog_packets) ||
	    hyper_network_resource_number(tx, &result.tx_buffer_bytes) ||
	    !result.rx_backlog_packets ||
	    result.rx_backlog_packets > HYPER_NET_RX_BACKLOG_MAX ||
	    (result.tx_buffer_bytes &&
	     result.tx_buffer_bytes < HYPER_NET_TX_BUFFER_MIN))
		return -1;
	*resources = result;
	return 0;
}
struct hyper_network {
	char name[32], bridge[16], uplink[16];
};
int hyper_networks_read(const char *, struct hyper_network[HYPER_MAX_NETWORKS],
			unsigned *);
static inline int hyper_mac_parse(const char *text, unsigned char mac[6])
{
	if (strlen(text) != 17)
		return -1;
	for (unsigned i = 0; i < 6; ++i) {
		unsigned value = 0;
		for (unsigned j = 0; j < 2; ++j) {
			char c = text[3 * i + j];
			if (c >= '0' && c <= '9')
				value = value * 16 + (unsigned)(c - '0');
			else if (c >= 'a' && c <= 'f')
				value = value * 16 + (unsigned)(c - 'a' + 10);
			else
				return -1;
		}
		mac[i] = value;
		if (i != 5 && text[3 * i + 2] != ':')
			return -1;
	}
	return (mac[0] & 3) == 2 ? 0 : -1;
}
#endif
