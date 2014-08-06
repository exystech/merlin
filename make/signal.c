/*
 * Copyright (c) 2014, 2015 Jonas 'Sortie' Termansen.
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
 * signal.c
 * Signal handling.
 */

#include <sys/types.h>

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>

#include "signal.h"

// TODO: Make some of these variables static and remove from the header?
sigset_t cleanup_signals;
bool cleanup_desired = false;
bool cleanup_in_progress = false;
pid_t cleanup_forward_pid = 0;
int* cleanup_exit_code_ptr;

sig_atomic_t cleanup_signal_arrived_flag = 0;

void die_through_signal(int signum)
{
	struct sigaction sa;
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	sigaction(signum, &sa, NULL);
	sigset_t signum_set;
	sigemptyset(&signum_set);
	sigaddset(&signum_set, signum);
	sigprocmask(SIG_UNBLOCK, &signum_set, NULL);
	raise(signum);
}

void on_cleanup_signal(int signum)
{
	if ( cleanup_in_progress )
		return;
	if ( !cleanup_desired )
		return die_through_signal(signum);
	if ( cleanup_signal_arrived_flag == 0 )
		cleanup_signal_arrived_flag = signum;
	// The waitpid call assigns the exit code of the child process to this
	// variable on return. We don't want to send a signal to a process that has
	// been waited for, since the pid could have been reused by another process.
	if ( *cleanup_exit_code_ptr != INT_MIN )
		return;
	kill(cleanup_forward_pid, signum);
}

void setup_cleanup_signals()
{
	sigemptyset(&cleanup_signals);
	sigaddset(&cleanup_signals, SIGHUP);
	sigaddset(&cleanup_signals, SIGTERM);
	sigaddset(&cleanup_signals, SIGINT);
	sigaddset(&cleanup_signals, SIGQUIT);

	cleanup_desired = false;
	cleanup_in_progress = false;
	cleanup_forward_pid = 0;

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_cleanup_signal;
	memcpy(&sa.sa_mask, &cleanup_signals, sizeof(sigset_t));
	sa.sa_flags = SA_RESTART;

	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
}
