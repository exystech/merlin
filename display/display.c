/*
 * Copyright (c) 2014, 2015, 2016, 2017, 2022 Jonas 'Sortie' Termansen.
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
 * display.c
 * Display server.
 */

#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arrow.inc"

#include "display.h"
#include "framebuffer.h"
#include "server.h"

uint32_t arrow_buffer[48 * 48];
struct framebuffer arrow_framebuffer = { 48, arrow_buffer, 48, 48 };

static void ready(void)
{
	const char* readyfd_env = getenv("READYFD");
	if ( !readyfd_env )
		return;
	int readyfd = atoi(readyfd_env);
	char c = '\n';
	write(readyfd, &c, 1);
	close(readyfd);
	unsetenv("READYFD");
}

static void compact_arguments(int* argc, char*** argv)
{
	for ( int i = 0; i < *argc; i++ )
	{
		while ( i < *argc && !(*argv)[i] )
		{
			for ( int n = i; n < *argc; n++ )
				(*argv)[n] = (*argv)[n+1];
			(*argc)--;
		}
	}
}

int main(int argc, char* argv[])
{
	for ( int i = 1; i < argc; i++ )
	{
		const char* arg = argv[i];
		if ( arg[0] != '-' || !arg[1] )
			continue;
		argv[i] = NULL;
		if ( !strcmp(arg, "--") )
			break;
		if ( arg[1] != '-' )
		{
			char c;
			while ( (c = *++arg) ) switch ( c )
			{
			default:
				errx(1, "unknown option -- '%c'", c);
			}
		}
		else
			errx(1, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	memcpy(arrow_buffer, arrow, sizeof(arrow));

	setlocale(LC_ALL, "");
	setvbuf(stdout, NULL, _IOLBF, 0);

	if ( getpgid(0) != getpid() )
		errx(1, "This program must be run in its own process group");

	struct display display;
	display_initialize(&display);

	struct server server;
	server_initialize(&server, &display);

	if ( setenv("DISPLAY_SOCKET", server.server_path, 1) < 0 )
		err(1, "setenv");

	ready();

	char* home_session = NULL;
	char** session_argv = NULL;
	if ( 1 < argc )
		session_argv = argv + 1;
	else
	{
		const char* home = getenv("HOME");
		if ( home && asprintf(&home_session, "%s/.displayrc", home) < 0 )
			err(1, "malloc");
		const char* session_path = NULL;
		if ( !access(home_session, F_OK) )
			session_path = home_session;
		else if ( !access("/etc/displayrc", F_OK) )
			session_path = "/etc/displayrc";
		else if ( !access("/etc/default/displayrc", F_OK) )
			session_path = "/etc/default/displayrc";
		if ( session_path )
			session_argv = (char**) (const char*[]) {session_path, NULL};
	}

	if ( session_argv )
	{
		pid_t pid = fork();
		if ( pid < 0 )
			warn("fork");
		else if ( pid == 0 )
		{
			execvp(session_argv[0], session_argv);
			warn("%s", session_argv[0]);
			_exit(127);
		}
	}

	free(home_session);

	server_mainloop(&server);

	return 0;
}
