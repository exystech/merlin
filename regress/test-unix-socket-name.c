/*
 * Copyright (c) 2017 Jonas 'Sortie' Termansen.
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
 * test-unix-socket-name.c
 * Tests whether unix sockets return the right names.
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include "test.h"

static pid_t main_pid;
static char tmpdir[] = "/tmp/test-unix-socket-name.XXXXXX";
static char sockpath[(sizeof(tmpdir) - 1) + 1 + 6 + 1];
static bool made_tmpdir = false;
static bool made_socket = false;

void exit_handler(void)
{
	if ( getpid() != main_pid )
		return;
	if ( made_socket )
		unlink(sockpath);
	if ( made_tmpdir )
		rmdir(tmpdir);
}

int main(void)
{
	main_pid = getpid();
	test_assert(atexit(exit_handler) == 0);
	test_assert(mkdtemp(tmpdir));
	made_tmpdir = true;

	int server;
	test_assert(0 <= (server = socket(AF_UNIX, SOCK_STREAM, 0)));

	test_assert(snprintf(sockpath, sizeof(sockpath), "%s/socket", tmpdir) <
	            (int) sizeof(sockpath));
	socklen_t sockaddr_actual_len =
		offsetof(struct sockaddr_un, sun_path) + strlen(sockpath) + 1;
	struct sockaddr_un sockaddr;
	test_assert(sockaddr_actual_len <= sizeof(sockaddr));
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sun_family = AF_UNIX;
	strlcpy(sockaddr.sun_path, sockpath, sizeof(sockaddr.sun_path));
	socklen_t sockaddr_len = sizeof(sockaddr);

	test_assert(bind(server, (struct sockaddr*) &sockaddr, sockaddr_len) == 0);
	test_assert(listen(server, 1) == 0 );
	made_socket = true;

	struct sockaddr_un addr;
	socklen_t addr_len;

	memset(&addr, 0, sizeof(addr));
	addr_len  = sizeof(addr);
	test_assert(getsockname(server, (struct sockaddr*) &addr,
	                        &addr_len) == 0);
	test_assert(sockaddr_actual_len <= addr_len);
	test_assert(addr_len <= sizeof(addr));
	test_assert(addr.sun_family == AF_UNIX);
	test_assert(strcmp(sockpath, addr.sun_path) == 0);

	test_assert(getpeername(server, (struct sockaddr*) &addr,
	                        &addr_len) == -1);
	test_assert(errno == ENOTCONN);

	memset(&addr, 0, sizeof(addr));
	addr_len = sizeof(addr);

	int client;
	pid_t pid;
	test_assert(0 <= (pid = fork()));
	if ( pid == 0 )
	{
		test_assert(0 <= (client = socket(AF_UNIX, SOCK_STREAM, 0)));
		test_assert(connect(client, (struct sockaddr*) &sockaddr,
		                    sockaddr_len) == 0);

#if defined(__sortix__) // Linux returns an empty struct sockaddr_un.
		memset(&addr, 0, sizeof(addr));
		addr_len  = sizeof(addr);
		test_assert(getsockname(client, (struct sockaddr*) &addr,
		                        &addr_len) == 0);
		test_assert(sockaddr_actual_len <= addr_len);
		test_assert(addr_len <= sizeof(addr));
		test_assert(addr.sun_family == AF_UNIX);
		test_assert(strcmp(sockpath, addr.sun_path) == 0);
#endif

		memset(&addr, 0, sizeof(addr));
		addr_len = sizeof(addr);
		test_assert(getpeername(client, (struct sockaddr*) &addr,
		                        &addr_len) == 0);
		test_assert(sockaddr_actual_len <= addr_len);
		test_assert(addr_len <= sizeof(addr));
		test_assert(addr.sun_family == AF_UNIX);
		test_assert(strcmp(sockpath, addr.sun_path) == 0);

		_exit(0);
	}

	memset(&addr, 0, sizeof(addr));
	addr_len  = sizeof(addr);
	test_assert(0 <= (client = accept(server, (struct sockaddr*) &addr,
		                              &addr_len)));
	test_assert(sockaddr_actual_len <= addr_len);
	test_assert(addr_len <= sizeof(addr));
	test_assert(addr.sun_family == AF_UNIX);
	test_assert(strcmp(sockpath, addr.sun_path) == 0);

	memset(&addr, 0, sizeof(addr));
	addr_len  = sizeof(addr);
	test_assert(getsockname(client, (struct sockaddr*) &addr,
	                        &addr_len) == 0);
	test_assert(sockaddr_actual_len <= addr_len);
	test_assert(addr_len <= sizeof(addr));
	test_assert(addr.sun_family == AF_UNIX);
	test_assert(strcmp(sockpath, addr.sun_path) == 0);

#if defined(__sortix__) // Linux returns an empty struct sockaddr_un.
	memset(&addr, 0, sizeof(addr));
	addr_len = sizeof(addr);
	test_assert(getpeername(client, (struct sockaddr*) &addr,
	                        &addr_len) == 0);
	test_assert(sockaddr_actual_len <= addr_len);
	test_assert(addr_len <= sizeof(addr));
	test_assert(addr.sun_family == AF_UNIX);
	test_assert(strcmp(sockpath, addr.sun_path) == 0);
#endif

	int status = 0;
	waitpid(pid, &status, 0);
	if ( !WIFEXITED(status) || WEXITSTATUS(status) != 0 )
		return 1;

	return 0;
}
