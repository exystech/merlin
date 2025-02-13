/*
 * Copyright (c) 2013 Jonas 'Sortie' Termansen.
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
 * stdlib/system.c
 * Execute a shell command.
 */

#include <sys/wait.h>

#include <stdlib.h>
#include <unistd.h>

int system(const char* command)
{
	const int ret_error = command ? -1 : 0;
	const int exit_error = command ? 127 : 0;
	if ( !command )
		command = "exit 1";
	// TODO: Block SIGHCHLD, SIGINT, anda SIGQUIT while waiting.
	pid_t childpid = fork();
	if ( childpid < 0 )
		return ret_error;
	if ( childpid )
	{
		int status;
		if ( waitpid(childpid, &status, 0) < 0 )
			return ret_error;
		return status;
	}
	execlp("sh", "sh", "-c", command, (const char*) NULL);
	_exit(exit_error);
}
