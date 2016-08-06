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
 * test-unix-socket-shutdown.c
 * Tests whether unix sockets shut down correctly.
 */

#include <sys/socket.h>
#include <sys/wait.h>

#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include "test.h"

static void test(bool test_read,
                 bool parent_shutdown,
                 bool child_shutdown,
                 bool sigpipe)
{
	int fds[2];
	test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	pid_t child_pid;
	test_assert(0 <= (child_pid = fork()));
	if ( child_pid == 0 )
	{
		if ( !test_read && !sigpipe )
			signal(SIGPIPE, SIG_IGN);
		if ( child_shutdown )
		{
			if ( test_read )
				test_assert(shutdown(fds[0], SHUT_RD) == 0);
			else
				test_assert(shutdown(fds[0], SHUT_WR) == 0);
		}
		close(fds[1]);
		char c;
		if ( test_read )
		{
			ssize_t amount = read(fds[0], &c, 1);
			if ( amount < 0 )
				test_error(errno, "read");
			test_assert(amount == 0);
			_exit(0);
		}
		else
		{
			while ( true )
			{
				c = 'X';
				ssize_t amount = write(fds[0], &c, 1);
				if ( !sigpipe && amount == -1 && errno == EPIPE )
					_exit(0);
				if ( amount != 1 )
					test_error(errno, "write");
			}
		}
	}
	close(fds[0]);
	if ( parent_shutdown )
	{
		if ( test_read )
			test_assert(shutdown(fds[1], SHUT_WR) == 0);
		else
			test_assert(shutdown(fds[1], SHUT_RD) == 0);
	}
	else if ( !parent_shutdown && !child_shutdown )
		close(fds[1]);
	int status;
	test_assert(waitpid(child_pid, &status, 0) == child_pid);
	if ( test_read || !sigpipe )
		test_assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	else
		test_assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGPIPE);
	if ( parent_shutdown || child_shutdown )
		close(fds[1]);
}

int main(void)
{
	test(false, false, false, false);
	test(true, false, false, false);
	test(true, false, false, true);
	test(false, true, false, false);
	test(true, true, false, false);
	test(true, true, false, true);
	test(false, false, true, false);
	test(true, false, true, false);
	test(true, false, true, true);
	test(false, true, true, false);
	test(true, true, true, false);
	test(true, true, true, true);
	return 0;
}
