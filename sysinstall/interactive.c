/*
 * Copyright (c) 2015, 2016, 2017, 2023 Jonas 'Sortie' Termansen.
 * Copyright (c) 2023 Juhani 'nortti' Krekelä.
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
 * interactive.c
 * Interactive utility functions.
 */

#include <sys/display.h>
#include <sys/ioctl.h>
#include <sys/termmode.h>
#include <sys/types.h>

#include <ctype.h>
#include <display.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <wchar.h>

#include <display.h>

#include "autoconf.h"
#include "execute.h"
#include "interactive.h"

#define REQUEST_DISPLAYS_ID 0
#define REQUEST_DISPLAY_MODE_ID 1

static uint32_t displays_count;
static bool displays_count_received;

static struct dispmsg_crtc_mode display_mode;
static int request_display_mode_error;
static bool display_mode_received;

void shlvl(void)
{
	long shlvl = 0;
	if ( getenv("SHLVL") && (shlvl = atol(getenv("SHLVL"))) < 0 )
		shlvl = 0;
	if ( shlvl < LONG_MAX )
		shlvl++;
	char shlvl_string[sizeof(long) * 3];
	snprintf(shlvl_string, sizeof(shlvl_string), "%li", shlvl);
	setenv("SHLVL", shlvl_string, 1);
}

void text(const char* str)
{
	fflush(stdout);
	struct winsize ws;
	if ( tcgetwinsize(1, &ws) < 0 )
		err(2, "tcgetwinsize");
	size_t columns = ws.ws_col;
	size_t column = 0;
	struct wincurpos wcp;
	if ( tcgetwincurpos(1, &wcp) == 0 )
		column = wcp.wcp_col;
	bool blank = false;
	while ( str[0] )
	{
		if ( str[0] == '\n' )
		{
			putchar('\n');
			blank = false;
			column = 0;
			str++;
			continue;
		}
		else if ( isblank((unsigned char) str[0]) )
		{
			blank = true;
			str++;
			continue;
		}
		size_t word_length = 0;
		size_t word_columns = 0;
		mbstate_t ps = { 0 };
		while ( str[word_length] &&
		        str[word_length] != '\n' &&
		        !isblank((unsigned char) str[word_length]) )
		{
			wchar_t wc;
			size_t amount = mbrtowc(&wc, str + word_length, SIZE_MAX, &ps);
			if ( amount == (size_t) -2 )
				 break;
			if ( amount == (size_t) -1 )
			{
				memset(&ps, 0, sizeof(ps));
				amount = 1;
			}
			if ( amount == (size_t) 0 )
				break;
			word_length += amount;
			int width = wcwidth(wc);
			if ( width < 0 )
				continue;
			word_columns += width;
		}
		if ( (column && blank ? 1 : 0) + word_columns <= columns - column )
		{
			if ( column && blank )
			{
				putchar(' ');
				column++;
			}
			blank = false;
			fwrite(str, 1, word_length, stdout);
			column += word_columns;
			if ( column == columns )
				column = 0;
		}
		else
		{
			if ( column != 0 && column != columns )
				putchar('\n');
			column = 0;
			blank = false;
			fwrite(str, 1, word_length, stdout);
			column += word_columns;
			column %= columns;
		}
		str += word_length;
	}
	fflush(stdout);
}

void textf(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	char* str;
	int len = vasprintf(&str, format, ap);
	va_end(ap);
	if ( len < 0 )
	{
		vprintf(format, ap);
		return;
	}
	text(str);
	free(str);
}

void prompt(char* buffer,
            size_t buffer_size,
            const char* autoconf_name,
            const char* question,
            const char* answer)
{
	promptx(buffer, buffer_size, autoconf_name, question, answer, false);
}

void promptx(char* buffer,
             size_t buffer_size,
             const char* autoconf_name,
             const char* question,
             const char* answer,
             bool catch_if_shell)
{
	char* autoconf_answer = autoconf_eval(autoconf_name);
	while ( true )
	{
		printf("\e[1m");
		fflush(stdout);
		text(question);
		if ( answer )
			printf(" [%s] ", answer);
		else
			printf(" ");
		fflush(stdout);
		const char* accept_defaults = autoconf_get("accept_defaults");
		const char* automatic_answer = NULL;
		if ( autoconf_answer )
		{
			automatic_answer = autoconf_answer;
			if ( !automatic_answer[0] )
				automatic_answer = answer;
		}
		else if ( accept_defaults && !strcasecmp(accept_defaults, "yes") )
			automatic_answer = answer;
		if ( automatic_answer )
		{
			printf("\e[93m");
			printf("%s\n", automatic_answer);
			printf("\e[m");
			fflush(stdout);
			strlcpy(buffer, automatic_answer, buffer_size);
			free(autoconf_answer);
			return;
		}
		fgets(buffer, buffer_size, stdin);
		printf("\e[22m");
		fflush(stdout);
		size_t buffer_length = strlen(buffer);
		if ( buffer_length && buffer[buffer_length-1] == '\n' )
			buffer[--buffer_length] = '\0';
		while ( buffer_length && buffer[buffer_length-1] == ' ' )
			buffer[--buffer_length] = '\0';
		if ( !strcmp(buffer, "") )
		{
			if ( !answer )
				continue;
			strlcpy(buffer, answer, buffer_size);
		}
		if ( !strcmp(buffer, "!") )
		{
			printf("Type 'exit' to return to the %s.\n", prompt_man_page);
			fflush(stdout);
			execute((const char*[]) { "sh", NULL }, "f");
			if ( catch_if_shell )
				break;
			continue;
		}
		if ( !strcmp(buffer, "!man") )
		{
			execute((const char*[]) { "man", prompt_man_section,
			                          prompt_man_page, NULL }, "f");
			continue;
		}
		break;
	}
	free(autoconf_answer);
}

void password(char* buffer,
              size_t buffer_size,
              const char* question)
{
	unsigned int termmode;
	gettermmode(0, &termmode);
	settermmode(0, termmode & ~TERMMODE_ECHO);
	printf("\e[1m");
	fflush(stdout);
	text(question);
	printf(" ");
	fflush(stdout);
	fflush(stdin);
	// TODO: This may leave a copy of the password in the stdio buffer.
	fgets(buffer, buffer_size, stdin);
	fflush(stdin);
	printf("\e[22m\n");
	size_t buffer_length = strlen(buffer);
	if ( buffer_length && buffer[buffer_length-1] == '\n' )
		buffer[--buffer_length] = '\0';
	settermmode(0, termmode);
}

static bool has_program(const char* program)
{
	return execute((const char*[]) { "which", "--", program, NULL }, "q") == 0;
}

bool missing_program(const char* program)
{
	if ( has_program(program) )
		return false;
	warnx("%s: Program is absent", program);
	return true;
}

static void on_displays(void* ctx, uint32_t id, uint32_t displays)
{
	(void) ctx;
	if ( id != REQUEST_DISPLAYS_ID )
		return;
	displays_count = displays;
	displays_count_received = true;
}

static void on_display_mode(void* ctx, uint32_t id,
                            struct dispmsg_crtc_mode mode)
{
	(void) ctx;
	if ( id != REQUEST_DISPLAY_MODE_ID )
		return;
	display_mode = mode;
	request_display_mode_error = 0;
	display_mode_received = true;
}

static void on_ack(void* ctx, uint32_t id, int32_t error)
{
	(void) ctx;
	if ( id != REQUEST_DISPLAY_MODE_ID )
		return;
	if ( error )
	{
		request_display_mode_error = error;
		display_mode_received = true;
	}
}

bool get_video_mode(struct dispmsg_crtc_mode* mode)
{
	if ( getenv("DISPLAY_SOCKET") )
	{
		struct display_connection* connection = display_connect_default();
		if ( !connection )
			return false;
		struct display_event_handlers handlers = {0};
		handlers.displays_handler = on_displays;
		handlers.display_mode_handler = on_display_mode;
		handlers.ack_handler = on_ack;
		display_request_displays(connection, REQUEST_DISPLAYS_ID);
		displays_count_received = false;
		while ( !displays_count_received )
			display_wait_event(connection, &handlers);
		if ( displays_count < 1 )
		{
			display_disconnect(connection);
			return false;
		}
		// TODO: Multimonitor support.
		display_request_display_mode(connection, REQUEST_DISPLAY_MODE_ID, 0);
		display_mode_received = false;
		while ( !display_mode_received )
			display_wait_event(connection, &handlers);
		display_disconnect(connection);
		if ( request_display_mode_error )
			return false;
		*mode = display_mode;
		return true;
	}

	struct tiocgdisplay display;
	struct tiocgdisplays gdisplays;
	memset(&gdisplays, 0, sizeof(gdisplays));
	gdisplays.count = 1;
	gdisplays.displays = &display;
	struct dispmsg_get_driver_name dgdn = { 0 };
	if ( ioctl(1, TIOCGDISPLAYS, &gdisplays) < 0 || gdisplays.count == 0 )
		return false;
	dgdn.device = display.device;
	dgdn.msgid = DISPMSG_GET_DRIVER_NAME;
	dgdn.device = display.device;
	dgdn.driver_index = 0;
	dgdn.name.byte_size = 0;
	dgdn.name.str = NULL;
	if ( dispmsg_issue(&dgdn, sizeof(dgdn)) < 0 && errno == ENODEV )
		return false;
	struct dispmsg_get_crtc_mode get_mode;
	memset(&get_mode, 0, sizeof(get_mode));
	get_mode.msgid = DISPMSG_GET_CRTC_MODE;
	get_mode.device = display.device;
	get_mode.connector = display.connector;
	// TODO: Still allow setting the video mode if none was already set.
	if ( dispmsg_issue(&get_mode, sizeof(get_mode)) < 0 )
		return false;
	*mode = get_mode.mode;
	return true;
}

void gui_shutdown(int code)
{
	if ( getenv("DISPLAY_SOCKET") )
	{
		struct display_connection* connection = display_connect_default();
		if ( connection )
			display_shutdown(connection, code);
		else
			warn("display_connect_default");
	}
}
