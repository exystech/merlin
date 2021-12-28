/*
 * Copyright (c) 2021 Jonas 'Sortie' Termansen.
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
 * test-unix-socket-fd-cycle.c
 * Tests whether Unix socket file descriptor passing cycles are rejected.
 */

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <stdalign.h>
#include <stdio.h>
#include <unistd.h>

#include "test.h"

int main(void)
{
	int a_fds[2];
	test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, a_fds) == 0);
	int b_fds[2];
	test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, b_fds) == 0);

	struct msghdr mhdr;
	char buf[1] = { 0 };
	struct iovec iov;
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	alignas(struct cmsghdr) char cmsgdata[CMSG_SPACE(sizeof(int))];
	ssize_t amount;
	struct cmsghdr* cmsg;

	// Passing a Unix socket on itself isn't permitted.
	buf[0] = 'X';
	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = cmsgdata;
	mhdr.msg_controllen = sizeof(cmsgdata);
	cmsg = CMSG_FIRSTHDR(&mhdr);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	int* cdata = (int*) CMSG_DATA(cmsg);
	*cdata = a_fds[1];
	amount = sendmsg(a_fds[1], &mhdr, 0);
	test_assertx(amount < 0);
	test_assert(errno == EPERM);

	// Passing a Unix socket on its other end isn't permitted.
	*cdata = a_fds[0];
	amount = sendmsg(a_fds[1], &mhdr, 0);
	test_assertx(amount < 0);
	test_assert(errno == EPERM);

	// Passing a Unix socket (with no fds passed) on another Unix socket (which
	// itself isn't being passed) is allowed.
	*cdata = b_fds[1];
	amount = sendmsg(a_fds[1], &mhdr, 0);
	test_assert(0 <= amount);
	test_assertx(amount == 1);

	// A Unix socket (itself being passed) is not permitted to pass fds.
	// b_fds[1] is already being sent on a_fds[1] (to a_fds[0]).
	FILE* file;
	test_assert((file = tmpfile()));
	*cdata = fileno(file);
	amount = sendmsg(b_fds[1], &mhdr, 0);
	test_assertx(amount < 0);
	test_assert(errno == EPERM);
	fclose(file);

	// A Unix socket is not permitted to send a socket with fds being sent.
	// b_fds[1] is already being sent on a_fds[1] (to a_fds[0]).
	*cdata = a_fds[1];
	amount = sendmsg(b_fds[0], &mhdr, 0);
	test_assertx(amount < 0);
	test_assert(errno == EPERM);

	// Receive b_fds[1] being sent on a_fds[1] to a_fds[0].
	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = cmsgdata;
	mhdr.msg_controllen = sizeof(cmsgdata);
	amount = recvmsg(a_fds[0], &mhdr, 0);
	test_assert(0 <= amount);
	test_assertx(amount == 1);
	test_assertx(buf[0] == 'X');
	test_assertx(!(mhdr.msg_flags & MSG_CTRUNC));
	test_assertx(!mhdr.msg_flags);
	test_assertx(mhdr.msg_controllen);
	cmsg = CMSG_FIRSTHDR(&mhdr);
	test_assertx(cmsg);
	test_assertx(cmsg->cmsg_level == SOL_SOCKET);
	test_assertx(cmsg->cmsg_type == SCM_RIGHTS);
	test_assertx(cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
	cdata = (int*) CMSG_DATA(cmsg);
	int file_fd = *cdata;
	test_assertx(0 <= file_fd);
	struct stat gotten_st;
	test_assert(fstat(file_fd, &gotten_st) == 0);
	struct stat expected_st;
	test_assert(fstat(b_fds[1], &expected_st) == 0);
	test_assertx(gotten_st.st_ino == expected_st.st_ino);
	test_assertx(gotten_st.st_dev == expected_st.st_dev);
	test_assertx(!CMSG_NXTHDR(&mhdr, cmsg));
	close(file_fd);

	return 0;
}
