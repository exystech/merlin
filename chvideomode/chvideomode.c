/*
 * Copyright (c) 2012, 2013, 2014, 2015, 2016, 2023 Jonas 'Sortie' Termansen.
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
 * chvideomode.c
 * Menu for changing the screen resolution.
 */

#include <sys/display.h>
#include <sys/ioctl.h>
#include <sys/keycodes.h>
#include <sys/termmode.h>
#include <sys/wait.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static bool set_current_mode(const struct tiocgdisplay* display,
                             struct dispmsg_crtc_mode mode)
{
	struct dispmsg_set_crtc_mode msg;
	msg.msgid = DISPMSG_SET_CRTC_MODE;
	msg.device = display->device;
	msg.connector = display->connector;
	msg.mode = mode;
	return dispmsg_issue(&msg, sizeof(msg)) == 0;
}

static struct dispmsg_crtc_mode*
get_available_modes(const struct tiocgdisplay* display,
                    size_t* num_modes_ptr)
{
	struct dispmsg_get_crtc_modes msg;
	msg.msgid = DISPMSG_GET_CRTC_MODES;
	msg.device = display->device;
	msg.connector = display->connector;
	size_t guess = 1;
	while ( true )
	{
		struct dispmsg_crtc_mode* ret = (struct dispmsg_crtc_mode*)
			malloc(sizeof(struct dispmsg_crtc_mode) * guess);
		if ( !ret )
			return NULL;
		msg.modes_length = guess;
		msg.modes = ret;
		if ( dispmsg_issue(&msg, sizeof(msg)) == 0 )
		{
			*num_modes_ptr = guess;
			return ret;
		}
		free(ret);
		if ( errno == ERANGE && guess < msg.modes_length )
		{
			guess = msg.modes_length;
			continue;
		}
		return NULL;
	}
}

struct filter
{
	bool include_all;
	bool include_supported;
	bool include_unsupported;
	bool include_text;
	bool include_graphics;
	size_t minbpp;
	size_t maxbpp;
	size_t minxres;
	size_t maxxres;
	size_t minyres;
	size_t maxyres;
	size_t minxchars;
	size_t maxxchars;
	size_t minychars;
	size_t maxychars;
};

static bool mode_passes_filter(struct dispmsg_crtc_mode mode,
                               struct filter* filter)
{
	if ( filter->include_all )
		return true;
	size_t width = mode.view_xres;
	size_t height = mode.view_yres;
	size_t bpp = mode.fb_format;
	bool supported = (mode.control & DISPMSG_CONTROL_VALID) ||
	                 (mode.control & DISPMSG_CONTROL_OTHER_RESOLUTIONS);
	bool unsupported = !supported;
	bool text = mode.control & DISPMSG_CONTROL_VGA;
	bool graphics = !text;
	if ( mode.control & DISPMSG_CONTROL_OTHER_RESOLUTIONS )
		return true;
	if ( unsupported && !filter->include_unsupported )
		return false;
	if ( supported && !filter->include_supported )
		return false;
	if ( text && !filter->include_text )
		return false;
	if ( graphics && !filter->include_graphics )
		return false;
	if ( graphics && (bpp < filter->minbpp || filter->maxbpp < bpp) )
		return false;
	if ( graphics && (width < filter->minxres || filter->maxxres < width) )
		return false;
	if ( graphics && (height < filter->minyres || filter->maxyres < height) )
		return false;
	// TODO: Support filtering text modes according to columns/rows.
	return true;
}

static void filter_modes(struct dispmsg_crtc_mode* modes,
                         size_t* num_modes_ptr,
                         struct filter* filter)
{
	size_t in_num = *num_modes_ptr;
	size_t out_num = 0;
	for ( size_t i = 0; i < in_num; i++ )
	{
		if ( mode_passes_filter(modes[i], filter) )
			modes[out_num++] = modes[i];
	}
	*num_modes_ptr = out_num;
}

static bool get_mode(struct dispmsg_crtc_mode* modes,
                     size_t num_modes,
                     unsigned int xres,
                     unsigned int yres,
                     unsigned int bpp,
                     struct dispmsg_crtc_mode* mode)
{
	bool found = false;
	bool found_other = false;
	size_t index;
	size_t other_index = 0;
	for ( size_t i = 0; i < num_modes; i++ )
	{
		if ( modes[i].view_xres == xres &&
			 modes[i].view_yres == yres &&
			 modes[i].fb_format == bpp )
		{
			index = i;
			found = true;
			break;
		}
		if ( modes[i].control & DISPMSG_CONTROL_OTHER_RESOLUTIONS )
		{
			found_other = true;
			other_index = i;
		}
	}
	if ( !found )
	{
		if ( found_other )
			index = other_index;
		else
			// Not in the list of pre-set resolutions and setting a custom
			// resolution is not supported.
			return false;
	}

	*mode = modes[index];
	if ( mode->control & DISPMSG_CONTROL_OTHER_RESOLUTIONS )
	{
		mode->fb_format = bpp;
		mode->view_xres = xres;
		mode->view_yres = yres;
		mode->control &= ~DISPMSG_CONTROL_OTHER_RESOLUTIONS;
		mode->control |= DISPMSG_CONTROL_VALID;
	}

	return true;
}

static bool select_mode(struct dispmsg_crtc_mode* modes,
                        size_t num_modes,
                        int mode_set_error,
                        struct dispmsg_crtc_mode* mode)
{
	int num_modes_display_length = 1;
	for ( size_t i = num_modes; 10 <= i; i /= 10 )
		num_modes_display_length++;

	size_t selection;
	bool decided;
	bool first_render;
	struct wincurpos render_at;
	selection = 0;
	decided = false;
	first_render = true;
	memset(&render_at, 0, sizeof(render_at));
	while ( !decided )
	{
		fflush(stdout);

		struct winsize ws;
		if ( tcgetwinsize(1, &ws) != 0 )
		{
			ws.ws_col = 80;
			ws.ws_row = 25;
		}

		struct wincurpos wcp;
		if ( tcgetwincurpos(1, &wcp) != 0 )
		{
			wcp.wcp_col = 1;
			wcp.wcp_row = 1;
		}

		size_t off = 1; // The "Please select ..." line at the top.
		if ( mode_set_error )
			off++;

		size_t entries_per_page = ws.ws_row - off;
		size_t page = selection / entries_per_page;
		size_t from = page * entries_per_page;
		size_t how_many_available = num_modes - from;
		size_t how_many = entries_per_page;
		if ( how_many_available < how_many )
			how_many = how_many_available;
		size_t lines_on_screen = off + how_many;

		if ( first_render )
		{
			while ( wcp.wcp_row &&
			        ws.ws_row - (wcp.wcp_row + 1) < lines_on_screen )
			{
				printf("\e[S");
				printf("\e[%juH", 1 + (uintmax_t) wcp.wcp_row);
				wcp.wcp_row--;
				wcp.wcp_col = 1;
			}
			render_at = wcp;
			first_render = false;
		}

		printf("\e[m");
		printf("\e[%juH", 1 + (uintmax_t) render_at.wcp_row);
		printf("\e[2K");

		if ( mode_set_error )
			printf("Error: Could not set desired mode: %s\n",
			       strerror(mode_set_error));
		printf("Please select one of these video modes or press ESC to "
		       "abort.\n");

		for ( size_t i = 0; i < how_many; i++ )
		{
			size_t index = from + i;
			size_t screenline = off + index - from;
			const char* color = index == selection ? "\e[31m" : "\e[m";
			printf("\e[%zuH", 1 + render_at.wcp_row + screenline);
			printf("%s", color);
			printf("\e[2K");
			printf(" [%-*zu] ", num_modes_display_length, index);
			if ( modes[index].control & DISPMSG_CONTROL_VALID )
				printf("%ux%ux%u",
				       modes[index].view_xres,
				       modes[index].view_yres,
				       modes[index].fb_format);
			else if ( modes[index].control & DISPMSG_CONTROL_OTHER_RESOLUTIONS )
				printf("(enter a custom resolution)");
			else
				printf("(unknown video device feature)");
			printf("\e[m");
		}

		printf("\e[J");
		fflush(stdout);

		unsigned int oldtermmode;
		if ( gettermmode(0, &oldtermmode) < 0 )
			err(1, "gettermmode");

		if ( settermmode(0, TERMMODE_KBKEY | TERMMODE_UNICODE |
		                    TERMMODE_SIGNAL) < 0 )
			err(1, "settermmode");

		bool redraw = false;
		while ( !redraw && !decided )
		{
			uint32_t codepoint;
			ssize_t numbytes = read(0, &codepoint, sizeof(codepoint));
			if ( numbytes < 0 )
				err(1, "read");

			int kbkey = KBKEY_DECODE(codepoint);
			if ( kbkey )
			{
				switch ( kbkey )
				{
				case KBKEY_ESC:
					if ( settermmode(0, oldtermmode) < 0 )
						err(1, "settermmode");
					printf("\n");
					return false;
					break;
				case KBKEY_UP:
					if ( selection )
						selection--;
					else
						selection = num_modes -1;
					redraw = true;
					break;
				case KBKEY_DOWN:
					if ( selection + 1 == num_modes )
						selection = 0;
					else
						selection++;
					redraw = true;
					break;
				case KBKEY_ENTER:
					if ( settermmode(0, oldtermmode) < 0 )
						err(1, "settermmode");
					fgetc(stdin);
					printf("\n");
					decided = true;
					break;
				}
			}
			else
			{
				if ( L'0' <= codepoint && codepoint <= '9' )
				{
					uint32_t requested = codepoint - '0';
					if ( requested < num_modes )
					{
						selection = requested;
						redraw = true;
					}
				}
			}
		}

		if ( settermmode(0, oldtermmode) < 0 )
			err(1, "settermmode");
	}

	*mode = modes[selection];
	if ( mode->control & DISPMSG_CONTROL_OTHER_RESOLUTIONS )
	{
		uintmax_t req_bpp, req_width, req_height;
		while ( true )
		{
			printf("Enter video mode [WIDTHxHEIGHTxBPP]: ");
			fflush(stdout);
			if ( scanf("%jux%jux%ju", &req_width, &req_height, &req_bpp) != 3 )
			{
				fgetc(stdin);
				fflush(stdin);
				continue;
			}
			fgetc(stdin);
			break;
		}
		mode->fb_format = req_bpp;
		mode->view_xres = req_width;
		mode->view_yres = req_height;
		mode->control &= ~DISPMSG_CONTROL_OTHER_RESOLUTIONS;
		mode->control |= DISPMSG_CONTROL_VALID;
	}

	return true;
}

static size_t parse_size_t(const char* str)
{
	char* endptr;
	errno = 0;
	uintmax_t parsed = strtoumax(str, &endptr, 10);
	if ( !*str || *endptr )
		errx(1, "Invalid integer argument: %s", str);
	if ( errno == ERANGE || (size_t) parsed != parsed )
		errx(1, "Integer argument too large: %s", str);
	return (size_t) parsed;
}

static bool parse_bool(const char* str)
{
	if ( !strcmp(str, "0") || !strcmp(str, "false") )
		return false;
	if ( !strcmp(str, "1") || !strcmp(str, "true") )
		return true;
	errx(1, "Invalid boolean argument: %s", str);
}

enum longopt
{
	OPT_SHOW_ALL = 128,
	OPT_SHOW_SUPPORTED,
	OPT_SHOW_UNSUPPORTED,
	OPT_SHOW_TEXT,
	OPT_SHOW_GRAPHICS,
	OPT_BPP,
	OPT_MIN_BPP,
	OPT_MAX_BPP,
	OPT_WIDTH,
	OPT_MIN_WIDTH,
	OPT_MAX_WIDTH,
	OPT_HEIGHT,
	OPT_MIN_HEIGHT,
	OPT_MAX_HEIGHT,
};

int main(int argc, char* argv[])
{
	struct filter filter;

	filter.include_all = false;
	filter.include_supported = true;
	filter.include_unsupported = false;
	filter.include_text = true;
	filter.include_graphics = true;
	// TODO: HACK: The kernel log printing requires either text mode or 32-bit
	// graphics. For now, just filter away anything but 32-bit graphics.
	filter.minbpp = 32;
	filter.maxbpp = 32;
	filter.minxres = 0;
	filter.maxxres = SIZE_MAX;
	filter.minyres = 0;
	filter.maxyres = SIZE_MAX;
	filter.minxchars = 0;
	filter.maxxchars = SIZE_MAX;
	filter.minychars = 0;
	filter.maxychars = SIZE_MAX;

	const struct option longopts[] =
	{
		{"show-all", required_argument, NULL, OPT_SHOW_ALL},
		{"show-supported", required_argument, NULL, OPT_SHOW_SUPPORTED},
		{"show-unsupported", required_argument, NULL, OPT_SHOW_UNSUPPORTED},
		{"show-text", required_argument, NULL, OPT_SHOW_TEXT},
		{"show-graphics", required_argument, NULL, OPT_SHOW_GRAPHICS},
		{"bpp", required_argument, NULL, OPT_BPP},
		{"min-bpp", required_argument, NULL, OPT_MIN_BPP},
		{"max-bpp", required_argument, NULL, OPT_MAX_BPP},
		{"width", required_argument, NULL, OPT_WIDTH},
		{"min-width", required_argument, NULL, OPT_MIN_WIDTH},
		{"max-width", required_argument, NULL, OPT_MAX_WIDTH},
		{"height", required_argument, NULL, OPT_HEIGHT},
		{"min-height", required_argument, NULL, OPT_MIN_HEIGHT},
		{"max-height", required_argument, NULL, OPT_MAX_HEIGHT},
		{0, 0, 0, 0}
	};
	const char* opts = "";
	int opt;
	while ( (opt = getopt_long(argc, argv, opts, longopts, NULL)) != -1 )
	{
		switch (opt)
		{
		case OPT_SHOW_ALL: filter.include_all = parse_bool(optarg); break;
		case OPT_SHOW_SUPPORTED:
			filter.include_supported = parse_bool(optarg); break;
		case OPT_SHOW_UNSUPPORTED:
			filter.include_unsupported = parse_bool(optarg); break;
		case OPT_SHOW_TEXT: filter.include_text = parse_bool(optarg); break;
		case OPT_SHOW_GRAPHICS:
			filter.include_graphics = parse_bool(optarg); break;
		case OPT_BPP:
			filter.minbpp = filter.maxbpp = parse_size_t(optarg); break;
		case OPT_MIN_BPP: filter.minbpp = parse_size_t(optarg); break;
		case OPT_MAX_BPP: filter.maxbpp = parse_size_t(optarg); break;
		case OPT_WIDTH:
			filter.minxres = filter.maxxres = parse_size_t(optarg); break;
		case OPT_MIN_WIDTH: filter.minxres = parse_size_t(optarg); break;
		case OPT_MAX_WIDTH: filter.maxxres = parse_size_t(optarg); break;
		case OPT_HEIGHT:
			filter.minyres = filter.maxyres = parse_size_t(optarg); break;
		case OPT_MIN_HEIGHT: filter.minyres = parse_size_t(optarg); break;
		case OPT_MAX_HEIGHT: filter.maxyres = parse_size_t(optarg); break;
		default: return 1;
		}
	}

	struct tiocgdisplay display;
	struct tiocgdisplays gdisplays;
	memset(&gdisplays, 0, sizeof(gdisplays));
	gdisplays.count = 1;
	gdisplays.displays = &display;
	if ( ioctl(1, TIOCGDISPLAYS, &gdisplays) < 0 || gdisplays.count == 0 )
	{
		fprintf(stderr, "No video devices are associated with this terminal.\n");
		exit(13);
	}

	size_t num_modes = 0;
	struct dispmsg_crtc_mode* modes = get_available_modes(&display, &num_modes);
	if ( !modes )
		err(1, "Unable to detect available video modes");

	if ( !num_modes )
	{
		fprintf(stderr, "No video modes are currently available.\n");
		fprintf(stderr, "Try make sure a device driver exists and is "
		                "activated.\n");
		exit(11);
	}

	filter_modes(modes, &num_modes, &filter);
	if ( !num_modes )
	{
		fprintf(stderr, "No video mode remains after filtering away unwanted "
		                "modes.\n");
		fprintf(stderr, "Try make sure the desired device driver is loaded and "
		                "is configured correctly.\n");
		exit(12);
	}

	if ( 1 < argc - optind )
		errx(1, "Unexpected extra operand");
	else if ( argc - optind == 1 )
	{
		unsigned int xres, yres, bpp;
		if ( sscanf(argv[optind], "%ux%ux%u", &xres, &yres, &bpp) != 3 )
			errx(1, "Invalid video mode: %s", argv[optind]);

		struct dispmsg_crtc_mode mode;
		if ( !get_mode(modes, num_modes, xres, yres, bpp, &mode) )
			errx(1, "No such available resolution: %s", argv[optind]);

		if ( !set_current_mode(&display, mode) )
			err(1, "Failed to set video mode %jux%jux%ju",
			    (uintmax_t) mode.view_xres,
			    (uintmax_t) mode.view_yres,
			    (uintmax_t) mode.fb_format);
	}
	else
	{
		int mode_set_error = 0;
		bool mode_set = false;
		while ( !mode_set )
		{
			struct dispmsg_crtc_mode mode;
			if ( !select_mode(modes, num_modes, mode_set_error, &mode) )
				exit(10);

			if ( !(mode_set = set_current_mode(&display, mode)) )
			{
				mode_set_error = errno;
				warn("Failed to set video mode %jux%jux%ju",
				     (uintmax_t) mode.view_xres,
				     (uintmax_t) mode.view_yres,
				     (uintmax_t) mode.fb_format);
			}
		}
	}

	return 0;
}
