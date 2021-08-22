/*
 * Copyright (c) 2021 Juhani 'nortti' Krekelä.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * dnsconfig.c
 * Configure kernel DNS resolver list.
 */

#include <sys/dnsconfig.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[])
{
	bool append = false;
	bool delete = false;
	bool set = false;

	int opt;
	while ( (opt = getopt(argc, argv, "ads")) != -1 )
	{
		switch ( opt )
		{
		case 'a': append = true; break;
		case 'd': delete = true; break;
		case 's': set = true; break;
		}
	}

	if ( append + delete + set > 1 )
		errx(1, "-a, -d, -s are mutually exclusive");

	// If no mode is provided but there are arguments, default to set.
	if ( !append && !delete && !set && optind < argc )
		set = true;

	struct dnsconfig dnsconfig = {0};
	if ( !set && getdnsconfig(&dnsconfig) < 0 )
		err(1, "getdnsconfig");

	if ( append || delete || set )
	{
		for ( int i = optind; i < argc; i++ )
		{
			struct dnsconfig_server server = {0};
			if ( inet_pton(AF_INET, argv[i], &server.addr.in) )
			{
				server.family = AF_INET;
				server.addrsize = sizeof(server.addr.in);
			}
			else if ( inet_pton(AF_INET6, argv[i], &server.addr.in6) )
			{
				server.family = AF_INET6;
				server.addrsize = sizeof(server.addr.in6);
			}
			else
				errx(1, "Invalid address: %s", argv[i]);

			if ( !delete )
			{
				size_t index = dnsconfig.servers_count++;
				if ( dnsconfig.servers_count > DNSCONFIG_MAX_SERVERS )
					errx(1, "Too many DNS resolvers (%zu max)",
					     (size_t) DNSCONFIG_MAX_SERVERS);
				dnsconfig.servers[index] = server;
			}
			else
			{
				// Search for a matching entry.
				bool found = false;
				for ( size_t j = 0; j < dnsconfig.servers_count; j++ )
				{
					if ( dnsconfig.servers[j].family != server.family ||
					     memcmp(&dnsconfig.servers[j].addr, &server.addr,
					            server.addrsize) )
						continue;

					for ( size_t k = j + 1; k < dnsconfig.servers_count; k++ )
						dnsconfig.servers[k - 1] = dnsconfig.servers[k];
					dnsconfig.servers_count--;
					memset(&dnsconfig.servers[dnsconfig.servers_count], 0,
					       sizeof(struct dnsconfig_server));
					found = true;
					break;
				}

				if ( !found )
					errx(1, "Resolver not in the list: %s", argv[i]);
			}

		}

		if ( setdnsconfig(&dnsconfig) < 0 )
			err(1, "setdnsconfig");
	}
	else
	{
		for ( size_t i = 0; i < dnsconfig.servers_count; i++ )
		{
			char address[INET6_ADDRSTRLEN];
			if ( !inet_ntop(dnsconfig.servers[i].family,
			                &dnsconfig.servers[i].addr,
			                address, sizeof(address)) )
				err(1, "inet_ntop");
			if ( printf("%s\n", address) < 0 )
				err(1, "stdout");
		}
		if ( fflush(stdout) )
			err(1, "stdout");
	}

	return 0;
}
