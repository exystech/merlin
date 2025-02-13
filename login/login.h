/*
 * Copyright (c) 2014, 2015, 2023 Jonas 'Sortie' Termansen.
 * Copyright (c) 2023 dzwdz.
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
 * login.h
 * Authenticates users.
 */

#ifndef LOGIN_H
#define LOGIN_H

struct check
{
	sigset_t oldset;
	struct termios tio;
	pid_t pid;
	int pipe;
	union
	{
		int errnum;
		unsigned char errnum_bytes[sizeof(int)];
	};
	unsigned int errnum_done;
	bool pipe_nonblock;
};

enum special_action
{
	SPECIAL_ACTION_NONE,
	SPECIAL_ACTION_POWEROFF,
	SPECIAL_ACTION_REBOOT,
	SPECIAL_ACTION_HALT,
	SPECIAL_ACTION_REINIT,
};

bool login(const char* username, const char* session);
bool check_real(const char* username, const char* password);
bool check_begin(struct check* chk,
                 const char* username,
                 const char* password,
                 bool restrict_termmode);
bool check_end(struct check* chk, bool* result, bool try);
bool check(const char* username, const char* password);
int graphical(void);
int textual(void);

bool parse_username(const char* input,
                    char** out_username,
                    char** out_session,
                    enum special_action* action);
void handle_special(enum special_action action);

#endif
