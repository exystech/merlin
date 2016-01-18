/*******************************************************************************

    Copyright(C) Jonas 'Sortie' Termansen 2016.

    This file is part of the Sortix C Library.

    The Sortix C Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    The Sortix C Library is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with the Sortix C Library. If not, see <http://www.gnu.org/licenses/>.

    sys/socket/socketpair.cpp
    Create a pair of connected sockets.

*******************************************************************************/

#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FLAGS_MASK (SOCK_NONBLOCK | SOCK_CLOEXEC | SOCK_CLOFORK)
#define TYPE_MASK (~FLAGS_MASK)
#define FLAGS(x) ((x) & FLAGS_MASK)
#define TYPE(x) ((x) & TYPE_MASK)

static char randchar(void)
{
	int index = arc4random_uniform(26 + 26 + 10);
	if ( index < 26 )
		return 'a' + index;
	if ( index < 26 + 26 )
		return 'A' + (index - 26);
	return '0' + (index - (26 + 26));
}

struct accept_thread
{
	int listen_fd;
	int fd;
	int flags;
	int errnum;
};

static void* accept_thread(void* arg_ptr)
{
	struct accept_thread* arg = (struct accept_thread*) arg_ptr;
	arg->fd = accept4(arg->listen_fd, NULL, NULL, arg->flags);
	arg->errnum = errno;
	return NULL;
}

static int socketpair_unix_stream(int flags, int fds[2])
{
	char templ[] = "/tmp/socketpair.XXXXXXXXXX";
	size_t templ_len = strlen(templ);
	while ( true )
	{
		for ( size_t i = 0; i < 10; i++ )
			templ[templ_len - i] = randchar();
		int listen_flags = SOCK_CLOEXEC | SOCK_CLOFORK;
		int listen_fd = socket(AF_UNIX, SOCK_STREAM | listen_flags, 0);
		if ( listen_fd < 0 )
			return -1;
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		memcpy(addr.sun_path, templ, templ_len + 1);
		if ( bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0 )
			return -1;
		if ( listen(listen_fd, 1) < 0 )
		{
			if ( errno == EADDRINUSE )
				continue;
			close(listen_fd);
			return -1;
		}
		int client_flags = flags & ~SOCK_NONBLOCK;
		int client_fd = socket(AF_UNIX, SOCK_STREAM | client_flags, 0);
		if ( client_fd < 0 )
		{
			int errnum = errno;
			close(listen_fd);
			unlink(templ);
			return errno = errnum, -1;
		}
		struct accept_thread arg;
		arg.listen_fd = listen_fd;
		arg.flags = flags;
		pthread_attr_t thread_attr;
		pthread_attr_init(&thread_attr);
		pthread_attr_setstacksize(&thread_attr, 8192);
		pthread_t thread;
		int ret = pthread_create(&thread, &thread_attr, accept_thread, &arg);
		pthread_attr_destroy(&thread_attr);
		if ( ret != 0 )
		{
			close(listen_fd);
			close(client_fd);
			unlink(templ);
			return errno = ret, -1;
		}
		ret = connect(client_fd, (struct sockaddr*) &addr, sizeof(addr));
		pthread_join(thread, NULL);
		if ( ret < 0 )
		{
			int errnum = errno;
			close(listen_fd);
			close(client_fd);
			unlink(templ);
			return errno = errnum, -1;
		}
		close(listen_fd);
		unlink(templ);
		if ( flags & SOCK_NONBLOCK )
			fcntl(client_fd, F_SETFL, flags);
		fds[0] = client_fd;
		fds[1] = arg.fd;
		return 0;
	}
}

extern "C" int socketpair(int family, int type, int protocol, int fds[2])
{
	if ( family == AF_UNIX )
	{
		if ( protocol != 0 )
			return errno = EPROTONOSUPPORT, -1;
		if ( TYPE(type) == SOCK_STREAM )
			return socketpair_unix_stream(FLAGS(type), fds);
		else
			return errno = EPROTOTYPE, -1;
	}
	return errno = EAFNOSUPPORT, -1;
}
