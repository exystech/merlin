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
 * test-pthread-basic.c
 * Tests whether basic pthread support works.
 */

#include <sys/wait.h>

#include <signal.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

int main(void)
{
	int fds[2];
	pipe(fds);
	pid_t pid = fork();
	test_assert(0 <= pid);
	if ( pid == 0 )
	{
		close(fds[0]);
		char c = 'X';
		test_assert(write(fds[1], &c, 1) == 1);
		while (1)
			sleep(1000);
	}
	close(fds[1]);
	char c;
	test_assert(read(fds[0], &c, 1) == 1);
	test_assert(c == 'X');
	kill(pid, SIGKILL);
	int status;
	waitpid(pid, &status, 0);
	return 0;
}
