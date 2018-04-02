/*
 * Copyright (c) 2016, 2017, 2018 Jonas 'Sortie' Termansen.
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
 * rw.c
 * Blockwise input/output.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef OFF_MAX
#define OFF_MAX ((off_t) ((UINTMAX_C(1) << (sizeof(off_t) * 8 - 1)) - 1))
#endif

static uintmax_t parse_quantity(const char* string,
                                blksize_t input_blksize,
                                blksize_t output_blksize)
{
	const char* end;
	if ( *string < '0' || '9' < *string )
		errx(1, "invalid quantity: %s", string);
	errno = 0;
	uintmax_t value = strtoumax(string, (char**) &end, 0);
	if ( value == UINTMAX_MAX && errno == ERANGE )
		errx(1, "argument overflow: %s", string);
	if ( *end )
	{
		while ( isspace((unsigned char) *end) )
			end++;
		uintmax_t magnitude = 1;
		const char* unit = end;
		unsigned char magc = tolower((unsigned char) *end);
		switch ( magc )
		{
		case '\0': errx(1, "trailing whitespace in quantity: %s", string);
		case 'b': magnitude = 1; break;
		case 'k': magnitude = UINTMAX_C(1024) << (0 * 10); break;
		case 'm': magnitude = UINTMAX_C(1024) << (1 * 10); break;
		case 'g': magnitude = UINTMAX_C(1024) << (2 * 10); break;
		case 't': magnitude = UINTMAX_C(1024) << (3 * 10); break;
		case 'p': magnitude = UINTMAX_C(1024) << (4 * 10); break;
		case 'e': magnitude = UINTMAX_C(1024) << (5 * 10); break;
		case 'r': magnitude = input_blksize; break;
		case 'w': magnitude = output_blksize; break;
		case 'x':
			if ( input_blksize != output_blksize )
				errx(1, "input block size is not output block size: %s",
				     string);
			magnitude = input_blksize;
			break;
		default: errx(1, "unsupported unit: %s", unit);
		}
		end++;
		if ( (tolower(magc) != 'b' &&
		      tolower(magc) != 'r' &&
		      tolower(magc) != 'w' &&
		      tolower(magc) != 'x') &&
		      strcasecmp(end, "iB") == 0 )
			end += 2;
		if ( *end != '\0' )
			errx(1, "unsupported unit: %s", unit);
		if ( magnitude != 0 && UINTMAX_MAX / magnitude < value )
			errx(1, "argument overflow: %s", string);
		value *= magnitude;
	}
	return value;
}

static size_t parse_size_t(const char* string,
                           blksize_t input_blksize,
                           blksize_t output_blksize)
{
	uintmax_t result = parse_quantity(string, input_blksize, output_blksize);
	if ( result != (size_t) result || SSIZE_MAX < result )
		errx(1, "argument overflow: %s", string);
	return (size_t) result;
}

static off_t parse_off_t(const char* string,
                         blksize_t input_blksize,
                         blksize_t output_blksize)
{
	uintmax_t result = parse_quantity(string, input_blksize, output_blksize);
	if ( result != (uintmax_t) (off_t) result )
		errx(1, "argument overflow: %s", string);
	return (off_t) result;
}

static off_t parse_offset(const char* string,
                          blksize_t input_blksize,
                          blksize_t output_blksize,
                          off_t size)
{
	if ( string[0] == '-' )
	{
		off_t result = parse_off_t(string + 1, input_blksize, output_blksize);
		if ( size < result )
			errx(1, "value smaller than file size: %s", string);
		return size - result;
	}
	else if ( string[0] == '+' )
	{
		off_t result = parse_off_t(string + 1, input_blksize, output_blksize);
		if ( OFF_MAX - size < result )
			errx(1, "argument overflow: %s", string);
		return size + result;
	}
	return parse_off_t(string, input_blksize, output_blksize);
}

static time_t parse_time_t(const char* string)
{
	const char* end;
	if ( *string < '0' || '9' < *string )
		errx(1, "invalid duration: %s", string);
	errno = 0;
	uintmax_t value = strtoumax(string, (char**) &end, 0);
	if ( value == UINTMAX_MAX && errno == ERANGE )
		errx(1, "argument overflow: %s", string);
	if ( *end )
		errx(1, "invalid duration: %s", string);
	if ( value != (uintmax_t) (time_t) value )
		errx(1, "argument overflow: %s", string);
	return (time_t) value;
}

static time_t timediff(struct timespec now, struct timespec then)
{
	time_t result = now.tv_sec - then.tv_sec;
	if ( now.tv_nsec < then.tv_nsec )
		result--;
	return result;
}

static int percent_done(off_t done, off_t total)
{
	if ( total < 0 || total < done )
		return -1;
	// Avoid overflow when multiplying by 100 by reducing the problem.
	if ( OFF_MAX / 65536 <= done )
	{
		done /= 65536;
		total /= 65536;
	}
	if ( total == 0 )
		return 100;
	return (done * 100) / total;
}

static void format_bytes_amount(char* buf,
                                size_t len,
                                uintmax_t value,
                                bool human_readable)
{
	if ( !human_readable )
	{
		snprintf(buf, len, "%ju B", value);
		return;
	}
	uintmax_t value_fraction = 0;
	uintmax_t exponent = 1024;
	char suffixes[] = { 'B', 'K', 'M', 'G', 'T', 'P', 'E' };
	size_t num_suffixes = sizeof(suffixes) / sizeof(suffixes[0]);
	size_t suffix_index = 0;
	while ( exponent <= value && suffix_index + 1 < num_suffixes)
	{
		value_fraction = value % exponent;
		value /= exponent;
		suffix_index++;
	}
	char suffix_str[] =
		{ suffixes[suffix_index], 0 < suffix_index ? 'i' : '\0', 'B', '\0' };
	char fraction_char = '0' + (value_fraction / (1024 / 10 + 1)) % 10;
	snprintf(buf, len, "%ju.%c %s", value, fraction_char, suffix_str);
}

static void format_time_amount(char* buf,
                               size_t len,
                               uintmax_t value,
                               bool human_readable)
{
	if ( !human_readable || value < 60 )
		snprintf(buf, len, "%ju s", value);
	else if ( value < 60 * 60 )
	{
		int seconds = value % 60;
		int fraction = (seconds * 10) / 60;
		uintmax_t minutes = value / 60;
		snprintf(buf, len, "%ju.%i m", minutes, fraction);
	}
	else if ( value < 24 * 60 * 60 )
	{
		int minutes = (value / 60) % 60;
		int fraction = (minutes * 10) / 60;
		uintmax_t hours = value / (60 * 60);
		snprintf(buf, len, "%ju.%i h", hours, fraction);
	}
	else
	{
		int minutes = (value / 60) % (24 * 60);
		int fraction = (minutes * 10) / (24 * 60);
		uintmax_t days = value / (24 * 60 * 60);
		snprintf(buf, len, "%ju.%i d", days, fraction);
	}
}

static volatile sig_atomic_t signaled = 0;
static volatile sig_atomic_t interrupted = 0;

static void on_signal(int signum)
{
	signaled = 1;
	if ( signum == SIGINT )
		interrupted = 1;
}

static void progress(struct timespec start,
                     off_t done,
                     off_t total,
                     bool human_readable,
                     struct timespec* last_statistic,
                     time_t interval)
{
	// Write statistics if signaled or if an interval has been set with -p.
	bool handling_signal = signaled || interrupted;
	struct timespec now;
	if ( !handling_signal )
	{
		if ( interval < 0 )
			return;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ( 0 < interval )
		{
			time_t since_last = timediff(now, *last_statistic);
			if ( since_last <= 0 )
				return;
			last_statistic->tv_sec += interval;
		}
	}
	// Avoid system calls being interrupted when writing statistics, but ensure
	// that we die by SIGINT if it happens for the second twice.
	sigset_t sigset, oldsigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	if ( !interrupted )
		sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, &oldsigset);
	if ( handling_signal )
		clock_gettime(CLOCK_MONOTONIC, &now);
	time_t duration = timediff(now, start);
	int percent = percent_done(done, total);
	off_t speed = -1;
	if ( 0 < duration )
		speed = done / duration;
	time_t countdown = -1;
	if ( 0 < speed && 0 <= total && done <= total )
	{
		off_t countdown_off = (total - done) / speed;
		if ( (time_t) countdown_off == countdown_off )
			countdown = countdown_off;
	}
	char duration_str[3 * sizeof(duration) + 2];
	format_time_amount(duration_str, sizeof(duration_str), duration,
	                   human_readable);
	char done_str[3 * sizeof(done) + 2];
	format_bytes_amount(done_str, sizeof(done_str), done, human_readable);
	char total_str[3 * sizeof(total) + 2] = "? B";
	if ( 0 <= total )
		format_bytes_amount(total_str, sizeof(total_str), total,
		                    human_readable);
	char percent_str[5] = "?%";
	if ( 0 <= percent )
		snprintf(percent_str, sizeof(percent_str), "%i%%", percent);
	char speed_str[3 * sizeof(speed) + 2] = "? B";
	if ( 0 <= speed )
		format_bytes_amount(speed_str, sizeof(speed_str), speed,
		                    human_readable);
	char countdown_str[3 * sizeof(countdown) + 2] = "? s";
	if ( 0 <= countdown )
		format_time_amount(countdown_str, sizeof(countdown_str), countdown,
		                   human_readable);
	fprintf(stderr, "%s %s / %s %s %s/s %s\n",
		duration_str,
		done_str,
		total_str,
	    percent_str,
		speed_str,
		countdown_str);
	if ( interrupted )
		raise(SIGINT);
	if ( handling_signal )
		signaled = 0;
	sigprocmask(SIG_SETMASK, &oldsigset, NULL);
}

int main(int argc, char *argv[])
{
	// SIGUSR1 is deadly by default until a handler is installed, let users
	// avoid the race condition by letting them block it before loading this
	// program and then it's unblocked after a handler is installed. Allow
	// disabling SIGUSR1 handling by setting the handler to ignore before
	// loading this program.
	struct sigaction sa;
	sigaction(SIGUSR1, NULL, &sa);
	bool handle_sigusr1 = sa.sa_handler != SIG_IGN;
	if ( handle_sigusr1 )
	{
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_signal;
		sa.sa_flags = 0; // Don't restart system calls.
		sigaction(SIGUSR1, &sa, NULL);
		sigset_t usr1_set;
		sigemptyset(&usr1_set);
		sigaddset(&usr1_set, SIGUSR1);
		sigset_t old_sigset;
		sigprocmask(SIG_UNBLOCK, &usr1_set, &old_sigset);
	}

	bool append = false;
	bool force = false;
	bool human_readable = false;
	bool no_create = false;
	bool pad = false;
	bool sync = false;
	bool truncate = false;
	bool verbose = false;
	const char* count_str = NULL;
	const char* input_path = NULL;
	const char* output_path = NULL;
	const char* input_blksize_str = NULL;
	const char* output_blksize_str = NULL;
	const char* input_offset_str = NULL;
	const char* output_offset_str = NULL;
	const char* progress_str = NULL;

	int opt;
	while ( (opt = getopt(argc, argv, "ab:c:fhI:i:O:o:Pp:r:stvw:x")) != -1 )
	{
		switch ( opt )
		{
		case 'a': append = true; break;
		case 'b': input_blksize_str = output_blksize_str = optarg; break;
		case 'c': count_str = optarg; break;
		case 'f': force = true; break;
		case 'h': human_readable = true; break;
		case 'I': input_offset_str = optarg; break;
		case 'i': input_path = optarg; break;
		case 'O': output_offset_str = optarg; break;
		case 'o': output_path = optarg; break;
		case 'P': pad = true; break;
		case 'p': progress_str = optarg; verbose = true; break;
		case 'r': input_blksize_str = optarg; break;
		case 's': sync = true; break;
		case 't': truncate = true; break;
		case 'v': verbose = true; break;
		case 'w': output_blksize_str = optarg; break;
		case 'x': no_create = true; break;
		default: return 1;
		}
	}

	if ( optind < argc )
		errx(1, "unexpected extra operand");

	if ( append && truncate )
		errx(1, "the -a and -t options are mutually incompatible");

	int input_fd = 0;
	if ( input_path )
	{
		input_fd = open(input_path, O_RDONLY);
		if ( input_fd < 0 )
			err(1, "%s", input_path);
	}
	else
		input_path = "<stdin>";

	int output_fd = 1;
	if ( output_path )
	{
		int flags = O_WRONLY | O_CREAT;
		if ( append )
			flags |= O_APPEND;
		if ( no_create )
			flags |= O_EXCL;
		output_fd = open(output_path, flags, 0666);
		if ( output_fd < 0 )
			err(1, "%s", output_path);
	}
	else
	{
		if ( append )
			errx(1, "the -a option requires -o");
		output_path = "<stdout>";
	}

	struct stat input_st;
	if ( fstat(input_fd, &input_st) < 0 )
		err(1, "stat: %s", input_path);

#if !defined(__sortix__)
	if ( S_ISBLK(input_st.st_mode) && input_st.st_size == 0 )
	{
		if ( (input_st.st_size = lseek(input_fd, 0, SEEK_END)) < 0 )
			err(1, "%s: lseek", input_path);
		lseek(input_fd, 0, SEEK_SET);
	}
#endif

	struct stat output_st;
	if ( fstat(output_fd, &output_st) < 0 )
		err(1, "stat: %s", output_path);

#if !defined(__sortix__)
	if ( S_ISBLK(output_st.st_mode) && output_st.st_size == 0 )
	{
		if ( (output_st.st_size = lseek(output_fd, 0, SEEK_END)) < 0 )
			err(1, "%s: lseek", output_path);
		lseek(output_fd, 0, SEEK_SET);
	}
#endif

	size_t input_blksize = input_st.st_blksize;
	if ( input_blksize_str )
		input_blksize = parse_size_t(input_blksize_str,
		                             input_st.st_blksize,
		                             output_st.st_blksize);
	if ( input_blksize == 0 )
		errx(1, "the input block size cannot be zero");

	size_t output_blksize = output_st.st_blksize;
	if ( output_blksize_str )
		output_blksize = parse_size_t(output_blksize_str,
		                              input_st.st_blksize,
		                              output_st.st_blksize);
	if ( output_blksize == 0 )
		errx(1, "the output block size cannot be zero");

	off_t input_offset = 0;
	if ( input_offset_str )
		input_offset = parse_offset(input_offset_str,
		                           input_blksize,
		                           output_blksize,
		                           input_st.st_size);

	off_t output_offset = 0;
	if ( output_offset_str )
		output_offset = parse_offset(output_offset_str,
		                             input_blksize,
		                             output_blksize,
		                             output_st.st_size);
	if ( append )
	{
		if ( output_offset != 0 )
			errx(1, "-O cannot be set to a non-zero value if -a is set");
		output_offset = output_st.st_size;
	}

	off_t count = -1; // No limit.
	if ( count_str )
	{
		off_t left = input_offset <= input_st.st_size ?
		             input_st.st_size - input_offset : 0;
		count = parse_offset(count_str, input_blksize, output_blksize, left);
	}

	time_t interval = -1; // No interval.
	if ( progress_str )
		interval = parse_time_t(progress_str);

	// Input and output are done only with aligned reads/writes, unless not
	// possible. The buffer works in two modes depending on the parameters:
	//
	// 1) If
	//
	//    * The input and output block sizes are a multiple of each other, and
	//    * the input offset and output offsets are equal modulo the block
	//      sizes;
	//
	//    then the buffer size is the largest of the input block size and the
	//    output block size, and it will always be possible to fill the buffer
	//    of that size with input and write it out.
	//
	// 2) Otherwise, the buffer size is the input block size plus the output
	//    block size, working as a ring buffer. This buffer will ensure
	//    efficient forward progress can be made even with worst case block
	//    sizes and offsets.
	bool use_largest_blksize = (input_blksize > output_blksize ?
	                            input_blksize % output_blksize == 0 :
	                            output_blksize % input_blksize == 0) &&
	                           input_offset % input_blksize ==
	                           output_offset % output_blksize;
	size_t buffer_size = use_largest_blksize ?
	                     input_blksize > output_blksize ?
	                     input_blksize :
	                     output_blksize :
	                     input_blksize + output_blksize;

	// Allocate a page aligned buffer.
	unsigned char* buffer = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE,
	                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ( buffer == MAP_FAILED )
		err(1, "allocating %zu byte buffer", buffer_size);

	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	struct timespec last_statistic = start;

	if ( verbose )
	{
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_signal;
		sa.sa_flags = SA_RESETHAND; // Second SIGINT is deadly.
		sigaction(SIGINT, &sa, NULL);
	}

	// Whether an end of file condition has been met, kept track of it in a
	// variable to handle devices like terminals that don't have sticky EOF
	// conditions (where the next read will also fail with an EOF condition).
	bool input_eof = false;

	// Estimate of how much will be written to the output for statistics. This
	// is set to -1 if not known or if the guess turns out to be wrong.
	off_t estimated_total_out;
	if ( S_ISREG(input_st.st_mode) || S_ISBLK(input_st.st_mode) )
	{
		off_t remaining = input_offset <= input_st.st_size ?
		                  input_st.st_size - input_offset : 0;
		estimated_total_out =
			count == -1 || remaining < count ? remaining : count;
	}
	else
		estimated_total_out = count;

	// Skip past the initial input offset. If the input isn't seekable, read and
	// discard that many bytes from the input. Fail hard even if -f as there is
	// no way to recover.
	if ( input_offset != 0 && lseek(input_fd, input_offset, SEEK_SET) < 0 )
	{
		if ( errno != ESPIPE )
			err(1, "%s: lseek", input_path);
		off_t offset = 0;
		while ( !input_eof && offset < input_offset )
		{
			size_t amount = input_blksize;
			if ( (uintmax_t) (input_offset - offset) < (uintmax_t) amount )
				amount = input_offset - offset;
			size_t so_far = 0;
			while ( so_far < amount )
			{
				progress(start, 0, estimated_total_out, human_readable,
				         &last_statistic, interval);
				ssize_t done = read(input_fd, buffer + so_far, amount - so_far);
				if ( done < 0 && errno == EINTR )
					done = 0;
				else if ( done < 0 )
					err(1, "%s: offset %ji", input_path, (intmax_t) offset);
				else if ( done == 0 )
				{
					input_eof = true;
					estimated_total_out = 0;
					break;
				}
				so_far += done;
				offset += done;
			}
		}
	}
	// The size of the next block to read, set such that after a block of this
	// size has been read, all subsequent reads will be aligned.
	size_t next_input_blksize = input_blksize - (input_offset % input_blksize);

	// Skip past the initial output offset. If the output isn't seekable, write
	// that many NUL bytes to the output. Fail hard even if -f as there is no
	// way to recover. If in append mode, -O is required to be zero and
	// output_offset is already set to the size of the output.
	if ( !append &&
	     output_offset != 0 &&
	     lseek(output_fd, output_offset, SEEK_SET) < 0 )
	{
		if ( errno != ESPIPE )
			err(1, "%s: lseek", output_path);
		memset(buffer, 0, output_blksize);
		off_t offset = 0;
		while ( offset < output_offset )
		{
			size_t amount = output_blksize;
			if ( (uintmax_t) (output_offset - offset) < (uintmax_t) amount )
				amount = output_offset - offset;
			size_t so_far = 0;
			while ( so_far < amount )
			{
				progress(start, 0, estimated_total_out, human_readable,
				         &last_statistic, interval);
				ssize_t done =
					write(output_fd, buffer + so_far, amount - so_far);
				if ( done < 0 && errno == EINTR )
					done = 0;
				else if ( done < 0 )
					err(1, "%s: offset %ji", output_path, (intmax_t) offset);
				so_far += done;
				offset += done;
			}
		}
	}
	// The size of the next block to write, set such that after a block of this
	// size has been written, all subsequent writes will be aligned.
	size_t next_output_blksize =
		output_blksize - (output_offset % output_blksize);

	// The total amount of bytes that has been read.
	off_t total_in = 0;

	// The total amount of bytes that has been written.
	off_t total_out = 0;

	// The offset in the ring buffer where data begins.
	size_t buffer_offset = 0;

	// The amount of data bytes in the ring buffer.
	size_t buffer_used = 0;

	// IO vector for efficient IO in case the ring buffer data wraps.
	struct iovec iov[2];
	memset(iov, 0, sizeof(iov));

	// The main loop. If an output block can't be written, read another input
	// block. If an output block can be written, write it.
	int exit_status = 0;
	do
	{
		// Read another input block, unless enough data has already been read,
		// or an end of file condition has been encountered.
		if ( !input_eof && count != -1 && count <= total_in )
		{
			input_eof = true;
			estimated_total_out = total_in;
		}
		else if ( !input_eof && buffer_used < next_output_blksize )
		{
			size_t left = next_input_blksize;
			next_input_blksize = input_blksize;
			if ( count != -1 &&
			     (uintmax_t) (count - total_in) < (uintmax_t) left )
				left = count - total_in;
			while ( left )
			{
				progress(start, total_out, estimated_total_out, human_readable,
				         &last_statistic, interval);
				assert(left <= buffer_size - buffer_used);
				size_t buffer_end = buffer_offset + buffer_used;
				if ( buffer_size < buffer_end )
					buffer_end -= buffer_size;
				size_t sequential = buffer_size - buffer_end;
				ssize_t done;
				if ( left <= sequential )
					done = read(input_fd, buffer + buffer_end, left);
				else
				{
					iov[0].iov_base = buffer + buffer_end;
					iov[0].iov_len = sequential;
					iov[1].iov_base = buffer;
					iov[1].iov_len = left - sequential;
					done = readv(input_fd, iov, 2);
				}
				if ( done < 0 && errno == EINTR )
					;
				else if ( done < 0 && !force )
					err(1, "%s: offset %ji", input_path,
					    (intmax_t) input_offset);
				else if ( done == 0 )
				{
					input_eof = true;
					estimated_total_out = total_in;
					break;
				}
				else
				{
					if ( done < 0 && force )
					{
						warn("%s: offset %ji", input_path,
						    (intmax_t) input_offset);
						// Skip until the next input block, or native input block
						// (whichever comes first).
						size_t until_next_native_block =
							input_st.st_blksize -
							(input_offset % input_st.st_blksize);
						size_t skip = left < until_next_native_block ?
						              left : until_next_native_block;
						// But don't skip past the end of the input.
						off_t possible = input_offset <= input_st.st_size ?
						                 input_st.st_size - input_offset : 0;
						if ( (uintmax_t) possible < (uintmax_t) skip )
							skip = possible;
						if ( lseek(input_fd, left, SEEK_CUR) < 0 )
							err(1, "%s: lseek", input_path);
						// Check if we reached the end of the file.
						if ( skip == 0 )
						{
							input_eof = true;
							estimated_total_out = total_in;
							break;
						}
						if ( skip <= sequential )
							memset(buffer + buffer_end, 0, skip);
						else
						{
							memset(buffer + buffer_end, 0, sequential);
							memset(buffer, 0, skip - sequential);
						}
						done = skip;
						exit_status = 1;
					}
					if ( OFF_MAX - input_offset < done )
					{
						errno = EOVERFLOW;
						err(1, "%s: offset", input_path);
					}
					left -= done;
					input_offset += done;
					buffer_used += done;
					total_in += done;
					// The estimate is wrong if too much has been read.
					if ( estimated_total_out < total_in )
						estimated_total_out = -1;
				}
			}
		}
		// If requested, pad the final block with NUL bytes until the next
		// output-block-size boundrary in the output.
		if ( pad && (input_eof && 0 < buffer_used) &&
		     buffer_used < next_output_blksize )
		{
			size_t left = next_output_blksize - buffer_used;
			size_t buffer_end = buffer_offset + buffer_used;
			if ( buffer_size < buffer_end )
				buffer_end -= buffer_size;
			size_t sequential = buffer_size - buffer_end;
			if ( left <= sequential )
				memset(buffer + buffer_end, 0, left);
			else
			{
				memset(buffer + buffer_end, 0, sequential);
				memset(buffer, 0, left - sequential);
			}
			buffer_used = next_output_blksize;
			estimated_total_out = total_out + buffer_used;
			pad = false;
		}
		// If the end of the input has been reached or a full output block can
		// written out, write out an output block.
		if ( (input_eof && 0 < buffer_used) ||
		     next_output_blksize <= buffer_used )
		{
			size_t left = next_output_blksize < buffer_used ?
			              next_output_blksize : buffer_used;
			next_output_blksize = output_blksize;
			while ( left )
			{
				progress(start, total_out, estimated_total_out, human_readable,
				         &last_statistic, interval);
				size_t sequential = buffer_size - buffer_offset;
				ssize_t done;
				if ( left <= sequential )
					done = write(output_fd, buffer + buffer_offset, left);
				else
				{
					iov[0].iov_base = buffer + buffer_offset;
					iov[0].iov_len = sequential;
					iov[1].iov_base = buffer;
					iov[1].iov_len = left - sequential;
					done = writev(output_fd, iov, 2);
				}
				if ( done < 0 && errno == EINTR )
					;
				else if ( done < 0 && (!force || append) )
					err(1, "%s: offset %ji", output_path,
				        (intmax_t) output_offset);
				else
				{
					// -f doesn't make sense in append mode as the error can't
					// be skipped past.
					if ( done < 0 && force && !append )
					{
						warn("%s: offset %ji", output_path,
						    (intmax_t) output_offset);
						// Skip until the next output block or native output
						// block (whichever comes first).
						size_t until_next_native_block =
							output_st.st_blksize -
							(output_offset % output_st.st_blksize);
						size_t skip = left < until_next_native_block ?
						              left : until_next_native_block;
						if ( lseek(output_fd, skip, SEEK_CUR) < 0 )
							err(1, "%s: lseek", output_path);
						done = skip;
						exit_status = 1;
					}
					if ( OFF_MAX - output_offset < done )
					{
						errno = EOVERFLOW;
						err(1, "%s: offset", output_path);
					}
					left -= done;
					buffer_offset += done;
					if ( buffer_size <= buffer_offset )
						buffer_offset -= buffer_size;
					buffer_used -= done;
					if ( buffer_used == 0 )
						buffer_offset = 0;
					output_offset += done;
					total_out += done;
					// The estimate is wrong if too much has been written.
					if ( estimated_total_out < total_out )
						estimated_total_out = -1;
				}
			}
		}
	} while ( !(input_eof && buffer_used == 0) );

	munmap(buffer, buffer_size);

	if ( truncate && ftruncate(output_fd, output_offset) < 0 )
		err(1, "truncate: %s", output_path);
	if ( sync && fsync(output_fd) < 0 )
		err(1, "sync: %s", output_path);
	if ( close(input_fd) < 0 )
		err(1, "close: %s", input_path);
	if ( close(output_fd) < 0 )
		err(1, "close: %s", output_path);

	if ( verbose || interrupted || signaled )
	{
		signaled = 1;
		progress(start, total_out, total_out, human_readable, &last_statistic,
		         interval);
	}

	return exit_status;
}
