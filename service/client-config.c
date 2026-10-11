// SPDX-FileCopyrightText: 2026 roolrz
// SPDX-License-Identifier: Apache-2.0
#include "hyper_clients.h"
#include "hyper_network.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int network_name(const char *name)
{
	if (*name < 'a' || *name > 'z')
		return 0;
	for (; *name; ++name)
		if (!(*name >= 'a' && *name <= 'z') &&
		    !isdigit((unsigned char)*name) && *name != '-')
			return 0;
	return 1;
}
int hyper_clients_read(const char *path,
		       struct hyper_client clients[HYPER_BLOCK_CLIENTS],
		       unsigned *count)
{
	char line[160];
	unsigned n = 0, owner = 0;
	FILE *file = fopen(path, "r");
	if (!file)
		return -1;
	if (!fgets(line, sizeof(line), file) ||
	    strcmp(line, "hyper.clients.v3\n"))
		goto invalid;
	while (fgets(line, sizeof(line), file)) {
		char id[8], extra;
		if (n == HYPER_BLOCK_CLIENTS || !strchr(line, '\n') ||
		    sscanf(line, "%7s %31s %17s %c", id, clients[n].network,
			   clients[n].mac, &extra) != 3)
			goto invalid;
		for (const char *p = id; *p; ++p)
			if (!isdigit((unsigned char)*p))
				goto invalid;
		unsigned long number = strtoul(id, NULL, 10);
		if (number >= HYPER_BLOCK_CLIENTS)
			goto invalid;
		clients[n].id = number;
		int network = strcmp(clients[n].network, "-") != 0;
		unsigned char mac[6];
		if ((network && (!number || !network_name(clients[n].network) ||
				 hyper_mac_parse(clients[n].mac, mac))) ||
		    (!network && strcmp(clients[n].mac, "-")))
			goto invalid;
		for (unsigned i = 0; i < n; ++i)
			if (clients[i].id == number ||
			    (network &&
			     !strcmp(clients[i].mac, clients[n].mac)))
				goto invalid;
		owner += number == 0;
		++n;
	}
	if (ferror(file) || owner != 1)
		goto invalid;
	fclose(file);
	*count = n;
	return 0;
invalid:
	fclose(file);
	errno = EINVAL;
	return -1;
}
