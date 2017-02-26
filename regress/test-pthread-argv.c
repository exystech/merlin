/*
 * Copyright (c) 2014 Jonas 'Sortie' Termansen.
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
 * test-pthread-argv.c
 * Tests if the argument vector and environment dies with the main thread.
 */

#include <pthread.h>
#include <string.h>

#include "test.h"

pthread_t main_thread;

char** global_argv;
char** global_envp;
size_t answer = 0;

void* thread_routine(void* ctx)
{
	(void) ctx;

	test_assertp(pthread_join(main_thread, NULL));

	size_t recount = 0;
	for ( int i = 0; global_argv[i]; i++ )
		recount += strlen(global_argv[i]);
	for ( int i = 0; global_envp[i]; i++ )
		recount += strlen(global_envp[i]);

	test_assertx(answer == recount);

	exit(0);
}

int main(int argc, char* argv[], char* envp[])
{
	(void) argc;

	for ( int i = 0; argv[i]; i++ )
		answer += strlen(argv[i]);
	for ( int i = 0; envp[i]; i++ )
		answer += strlen(envp[i]);

	global_argv = argv;
	global_envp = envp;

	main_thread = pthread_self();

	pthread_t thread;
	test_assertp(pthread_create(&thread, NULL, &thread_routine, NULL));

	pthread_exit(NULL);
}
