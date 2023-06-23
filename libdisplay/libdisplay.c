/*
 * Copyright (c) 2014, 2015, 2016, 2017, 2023 Jonas 'Sortie' Termansen.
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
 * libdisplay.c
 * Display client library.
 */

#include <sys/socket.h>
#include <sys/un.h>

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <ioleast.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <display.h>

#include "display-protocol.h"

int display_spawn(int argc, char** argv)
{
	int length = 2 + 1;
	if ( __builtin_add_overflow(length, argc, &length) )
		return errno = EOVERFLOW, -1;
	char** new_argv = reallocarray(NULL, length, sizeof(char*));
	if ( !new_argv )
		return -1;
	new_argv[0] = (char*) "display";
	// TODO: Start the compositor in a special close-after-program-exists mode?
	//       And maybe go fullscreen / maximized by default?
	new_argv[1] = (char*) "--";
	for ( int i = 0; i < argc; i++ )
		new_argv[2 + i] = argv[i];
	new_argv[2 + argc] = NULL;
	execvp(new_argv[0], new_argv);
	free(new_argv);
	return -1;
}

static int open_local_client_socket(const char* path, int flags)
{
	size_t path_length = strlen(path);
	size_t addr_size = offsetof(struct sockaddr_un, sun_path) + path_length + 1;
	struct sockaddr_un* sockaddr = malloc(addr_size);
	if ( !sockaddr )
		return -1;
	sockaddr->sun_family = AF_LOCAL;
	strcpy(sockaddr->sun_path, path);
	int fd = socket(AF_LOCAL, SOCK_STREAM | flags, 0);
	if ( fd < 0 )
		return free(sockaddr), -1;
	if ( connect(fd, (const struct sockaddr*) sockaddr, addr_size) < 0 )
		return close(fd), free(sockaddr), -1;
	free(sockaddr);
	return fd;
}

struct display_connection
{
	int fd;
	struct display_packet_header header;
	size_t header_got;
	uint8_t* payload;
	size_t payload_got;
};

struct display_connection* display_connect(const char* socket_path)
{
	struct display_connection* connection =
		calloc(1, sizeof(struct display_connection));
	if ( !connection )
		return NULL;
	if ( (connection->fd = open_local_client_socket(socket_path, 0)) < 0 )
		return free(connection), (struct display_connection*) NULL;
	size_t send_buffer_size = 2 * 1024 * 1024;
	setsockopt(connection->fd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size,
	           sizeof(send_buffer_size));
	return connection;
}

struct display_connection* display_connect_default(void)
{
	return display_connect(getenv("DISPLAY_SOCKET") ?
	                       getenv("DISPLAY_SOCKET") :
	                       "/var/run/display");
}

void display_disconnect(struct display_connection* connection)
{
	free(connection->payload);
	close(connection->fd);
	free(connection);
}

int display_connection_fd(struct display_connection* connection)
{
	return connection->fd;
}

static void send_message(struct display_connection* connection,
                         uint32_t id,
                         const void* message,
                         size_t message_size,
                         const void* auxiliary,
                         size_t auxiliary_size)
{
	struct display_packet_header header;
	header.id = id;
	header.size = message_size + auxiliary_size;
	writeall(connection->fd, &header, sizeof(header));
	writeall(connection->fd, message, message_size);
	writeall(connection->fd, auxiliary, auxiliary_size);
}

static void send_message_no_aux(struct display_connection* connection,
                                uint32_t id,
                                const void* message,
                                size_t message_size)
{
	send_message(connection, id, message, message_size, 0, 0);
}

void display_shutdown(struct display_connection* connection, uint32_t code)
{
	struct display_shutdown msg;
	msg.code = code;
	send_message_no_aux(connection, DISPLAY_SHUTDOWN, &msg, sizeof(msg));
}

void display_create_window(struct display_connection* connection,
                           uint32_t window_id)
{
	struct display_create_window msg;
	msg.window_id = window_id;
	send_message_no_aux(connection, DISPLAY_CREATE_WINDOW, &msg, sizeof(msg));
}

void display_destroy_window(struct display_connection* connection,
                            uint32_t window_id)
{
	struct display_destroy_window msg;
	msg.window_id = window_id;
	send_message_no_aux(connection, DISPLAY_DESTROY_WINDOW, &msg, sizeof(msg));
}

void display_resize_window(struct display_connection* connection,
                           uint32_t window_id,
                           uint32_t width,
                           uint32_t height)
{
	struct display_resize_window msg;
	msg.window_id = window_id;
	msg.width = width;
	msg.height = height;
	send_message_no_aux(connection, DISPLAY_RESIZE_WINDOW, &msg, sizeof(msg));
}

void display_render_window(struct display_connection* connection,
                           uint32_t window_id,
                           uint32_t left,
                           uint32_t top,
                           uint32_t width,
                           uint32_t height,
                           uint32_t* data)
{
	struct display_render_window msg;
	msg.window_id = window_id;
	msg.left = left;
	msg.top = top;
	msg.width = width;
	msg.height = height;
	send_message(connection, DISPLAY_RENDER_WINDOW, &msg, sizeof(msg),
	             data, sizeof(uint32_t) * width * height);
}

void display_title_window(struct display_connection* connection,
                          uint32_t window_id,
                          const char* title)
{
	struct display_title_window msg;
	msg.window_id = window_id;
	send_message(connection, DISPLAY_TITLE_WINDOW, &msg, sizeof(msg), title,
	             strlen(title));
}

void display_show_window(struct display_connection* connection,
                         uint32_t window_id)
{
	struct display_show_window msg;
	msg.window_id = window_id;
	send_message_no_aux(connection, DISPLAY_SHOW_WINDOW, &msg, sizeof(msg));
}

void display_hide_window(struct display_connection* connection,
                         uint32_t window_id)
{
	struct display_hide_window msg;
	msg.window_id = window_id;
	send_message_no_aux(connection, DISPLAY_HIDE_WINDOW, &msg, sizeof(msg));
}

void display_chkblayout(struct display_connection* connection,
                        uint32_t id,
                        void* data,
                        uint32_t kblayout_bytes)
{
	struct display_chkblayout msg;
	msg.id = id;
	send_message(connection, DISPLAY_CHKBLAYOUT, &msg, sizeof(msg),
	             data, kblayout_bytes);
}

void display_request_displays(struct display_connection* connection,
                              uint32_t id)
{
	struct display_request_displays msg;
	msg.id = id;
	send_message_no_aux(connection, DISPLAY_REQUEST_DISPLAYS, &msg,
	                    sizeof(msg));
}

void display_request_display_modes(struct display_connection* connection,
                                   uint32_t id,
                                   uint32_t display_id)
{
	struct display_request_display_modes msg;
	msg.id = id;
	msg.display_id = display_id;
	send_message_no_aux(connection, DISPLAY_REQUEST_DISPLAY_MODES, &msg,
	                    sizeof(msg));
}

void display_request_display_mode(struct display_connection* connection,
                                  uint32_t id,
                                  uint32_t display_id)
{
	struct display_request_display_mode msg;
	msg.id = id;
	msg.display_id = display_id;
	send_message_no_aux(connection, DISPLAY_REQUEST_DISPLAY_MODE, &msg,
	                    sizeof(msg));
}

void display_set_display_mode(struct display_connection* connection,
                              uint32_t id,
                              uint32_t display_id,
                              struct dispmsg_crtc_mode mode)
{
	struct display_set_display_mode msg;
	msg.id = id;
	msg.display_id = display_id;
	msg.mode = mode;
	send_message_no_aux(connection, DISPLAY_SET_DISPLAY_MODE, &msg,
	                    sizeof(msg));
}

static bool display_read_event(struct display_connection* connection)
{
	while ( connection->header_got < sizeof(connection->header) )
	{
		errno = 0;
		uint8_t* data = (uint8_t*) &connection->header + connection->header_got;
		size_t left = sizeof(connection->header) - connection->header_got;
		ssize_t amount = read(connection->fd, data, left);
		if ( amount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
			break;
		if ( amount <= 0 )
			return false;
		connection->header_got += amount;
	}
	if ( connection->header_got == sizeof(connection->header) &&
	     !connection->payload )
	{
		connection->payload = malloc(connection->header.size);
		if ( !connection->payload )
			return false;
		connection->payload_got = 0;
	}
	while ( connection->header_got == sizeof(connection->header) &&
	        connection->payload &&
	        connection->payload_got < connection->header.size )
	{
		errno = 0;
		uint8_t* data = connection->payload + connection->payload_got;
		size_t left = connection->header.size - connection->payload_got;
		ssize_t amount = read(connection->fd, data, left);
		if ( amount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
			break;
		if ( amount <= 0 )
			return false;
		connection->payload_got += amount;
	}

	return true;
}

static int display_dispatch_event(struct display_connection* connection,
                                  struct display_event_handlers* handlers)
{
	if ( connection->header_got == sizeof(connection->header) &&
	     connection->payload &&
	     connection->payload_got == connection->header.size )
	{
		void* payload = connection->payload;

		if ( connection->header.id == EVENT_DISCONNECT &&
		     connection->header.size == sizeof(struct event_disconnect) )
		{
			struct event_disconnect* event = payload;
			(void) event;
			if ( handlers->disconnect_handler )
				handlers->disconnect_handler(handlers->context);
			else
				exit(0);
		}

		if ( connection->header.id == EVENT_QUIT &&
		     connection->header.size == sizeof(struct event_quit) )
		{
			struct event_quit* event = payload;
			if ( handlers->quit_handler )
				handlers->quit_handler(handlers->context, event->window_id);
			else
				exit(0);
		}

		if ( connection->header.id == EVENT_RESIZE &&
		     connection->header.size == sizeof(struct event_resize) )
		{
			struct event_resize* event = payload;
			if ( handlers->resize_handler )
				handlers->resize_handler(handlers->context, event->window_id,
				                         event->width, event->height);
		}

		if ( connection->header.id == EVENT_KEYBOARD &&
		     connection->header.size == sizeof(struct event_keyboard) )
		{
			struct event_keyboard* event = payload;
			if ( handlers->keyboard_handler )
				handlers->keyboard_handler(handlers->context, event->window_id,
				                           event->codepoint);
		}

		if ( connection->header.id == EVENT_ACK &&
		     connection->header.size == sizeof(struct event_ack) )
		{
			struct event_ack* event = payload;
			if ( handlers->ack_handler )
				handlers->ack_handler(handlers->context, event->id,
				                      event->error);
		}

		if ( connection->header.id == EVENT_DISPLAYS &&
		     connection->header.size == sizeof(struct event_displays) )
		{
			struct event_displays* event = payload;
			if ( handlers->displays_handler )
				handlers->displays_handler(handlers->context, event->id,
				                           event->displays);
		}

		if ( connection->header.id == EVENT_DISPLAY_MODES &&
		     connection->header.size >= sizeof(struct event_display_modes) )
		{
			size_t aux_size = connection->header.size -
			                  sizeof(struct event_display_modes);
			void* aux = (char*) payload + sizeof(struct event_display_modes);
			struct event_display_modes* event = payload;
			if ( handlers->display_modes_handler )
				handlers->display_modes_handler(handlers->context, event->id,
				                                event->modes_count,
				                                aux, aux_size);
		}

		if ( connection->header.id == EVENT_DISPLAY_MODE &&
		     connection->header.size == sizeof(struct event_display_mode) )
		{
			struct event_display_mode* event = payload;
			if ( handlers->display_mode_handler )
				handlers->display_mode_handler(handlers->context, event->id,
				                               event->mode);
		}

		connection->header_got = 0;
		free(connection->payload);
		connection->payload = NULL;
		connection->payload_got = 0;

		return 0;
	}

	return -1;
}

static int display_event_read_hangup(struct display_event_handlers* handlers)
{
	if ( handlers->disconnect_handler )
		handlers->disconnect_handler(handlers->context);
	else
		exit(1);
	return -1;
}

int display_poll_event(struct display_connection* connection,
                       struct display_event_handlers* handlers)
{
	fcntl(connection->fd, F_SETFL, fcntl(connection->fd, F_GETFL) | O_NONBLOCK);
	bool read_success = display_read_event(connection);
	fcntl(connection->fd, F_SETFL, fcntl(connection->fd, F_GETFL) & ~O_NONBLOCK);
	if ( !read_success )
		return display_event_read_hangup(handlers);
	return display_dispatch_event(connection, handlers);
}

int display_wait_event(struct display_connection* connection,
                       struct display_event_handlers* handlers)
{
	if ( !display_read_event(connection) )
		return display_event_read_hangup(handlers);
	return display_dispatch_event(connection, handlers);
}
