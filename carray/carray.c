/*
 * Copyright (c) 2014, 2016 Jonas 'Sortie' Termansen.
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
 * carray.c
 * Convert a binary file to a C array.
 */

#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool get_option_variable(const char* option, const char** varptr,
                         const char* arg, int argc, char** argv, int* ip)
{
	size_t option_len = strlen(option);
	if ( strncmp(option, arg, option_len) != 0 )
		return false;
	if ( arg[option_len] == '=' )
	{
		*varptr = arg + option_len + 1;
		return true;
	}
	if ( arg[option_len] != '\0' )
		return false;
	if ( *ip + 1 == argc )
		errx(1, "expected operand after `%s'", option);
	*varptr = argv[++*ip];
	argv[*ip] = NULL;
	return true;
}

#define GET_OPTION_VARIABLE(str, varptr) \
        get_option_variable(str, varptr, arg, argc, argv, &i)

void get_short_option_variable(char c, const char** varptr,
                               const char* arg, int argc, char** argv, int* ip)
{
	if ( *(arg+1) )
		*varptr = arg + 1;
	else
	{
		if ( *ip + 1 == argc )
			errx(1, "option requires an argument -- '%c'", c);
		*varptr = argv[*ip + 1];
		argv[++(*ip)] = NULL;
	}
}

#define GET_SHORT_OPTION_VARIABLE(c, varptr) \
        get_short_option_variable(c, varptr, arg, argc, argv, &i)

int main(int argc, char* argv[])
{
	bool flag_const = false;
	bool flag_extern_c = false;
	bool flag_extern = false;
	bool flag_forward = false;
	bool flag_guard = false;
	bool flag_headers = false;
	bool flag_raw = false;
	bool flag_static = false;
	bool flag_volatile = false;

	const char* guard = NULL;
	const char* identifier = NULL;
	const char* includes = NULL;
	const char* output = NULL;
	const char* type = NULL;

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
			case 'c': flag_const = true; break;
			case 'e': flag_extern = true; break;
			case 'E': flag_extern_c = true; break;
			case 'f': flag_forward = true; break;
			case 'g': flag_guard = true; break;
			case 'G': GET_SHORT_OPTION_VARIABLE('G', &guard); arg = "G"; flag_guard = true; break;
			case 'H': flag_headers = true; break;
			// TODO: After releasing Sortix 1.1, change -i to --identifier
			//       rather than -H (--headers).
#if 0 // Future behavior:
			case 'i': GET_SHORT_OPTION_VARIABLE('i', &identifier); arg = "i"; break;
#else // Compatibility:
			case 'i': flag_headers = true; break;
#endif
			case 'o': GET_SHORT_OPTION_VARIABLE('o', &output); arg = "o"; break;
			case 'r': flag_raw = true; break;
			case 's': flag_static = true; break;
			case 't': GET_SHORT_OPTION_VARIABLE('t', &type); arg = "t"; break;
			case 'v': flag_volatile = true; break;
			default:
				errx(1, "unknown option -- '%c'", c);
			}
		}
		else if ( !strcmp(arg, "--const") )
			flag_const = true;
		else if ( !strcmp(arg, "--extern") )
			flag_extern = true;
		else if ( !strcmp(arg, "--extern-c") )
			flag_extern_c = true;
		else if ( !strcmp(arg, "--forward") )
			flag_forward = true;
		else if ( !strcmp(arg, "--use-guard") )
			flag_guard = true;
		else if ( !strcmp(arg, "--headers") )
			flag_headers = true;
		// TODO: After releasing Sortix 1.1, remove --include.
#if 1 // Compatibility:
		else if ( !strcmp(arg, "--include") )
			flag_headers = true;
#endif
		else if ( !strcmp(arg, "--raw") )
			flag_raw = true;
		else if ( !strcmp(arg, "--static") )
			flag_static = true;
		else if ( !strcmp(arg, "--volatile") )
			flag_volatile = true;
		else if ( !strcmp(arg, "--char") )
			type = "char";
		else if ( !strcmp(arg, "--signed-char") )
			type = "signed char";
		else if ( !strcmp(arg, "--unsigned-char") )
			type = "unsigned char";
		else if ( !strcmp(arg, "--int8_t") )
			type = "int8_t";
		else if ( !strcmp(arg, "--uint8_t") )
			type = "uint8_t";
		else if ( GET_OPTION_VARIABLE("--guard", &guard) )
			flag_guard = true;
		else if ( GET_OPTION_VARIABLE("--identifier", &identifier) ) { }
		else if ( GET_OPTION_VARIABLE("--includes", &includes) )
			flag_headers = true;
		else if ( GET_OPTION_VARIABLE("--output", &output) ) { }
		else if ( GET_OPTION_VARIABLE("--type", &type) ) { }
		else
			errx(1, "unknown option: %s", arg);
	}

	compact_arguments(&argc, &argv);

	if ( flag_extern && flag_static )
		errx(1, "the --extern and --static options are mutually incompatible");
	if ( flag_forward && flag_raw )
		errx(1, "the --forward and --raw options are mutually incompatible");

	if ( !type )
		type = "unsigned char";

	if ( !guard )
	{
		char* new_guard;
		if ( output )
			new_guard = strdup(output);
		else if ( 2 <= argc && strcmp(argv[1], "-") != 0 )
		{
			if ( asprintf(&new_guard, "%s_H", argv[1]) < 0 )
				err(1, "asprintf");
		}
		else
			new_guard = strdup("CARRAY_H");
		if ( !new_guard )
			err(1, "strdup");

		for ( size_t i = 0; new_guard[i]; i++ )
		{
			if ( 'A' <= new_guard[i] && new_guard[i] <= 'Z' )
				continue;
			else if ( 'a' <= new_guard[i] && new_guard[i] <= 'z' )
				new_guard[i] = 'A' + new_guard[i] - 'a';
			else if ( i != 0 && '0' <= new_guard[i] && new_guard[i] <= '9' )
				continue;
			else if ( new_guard[i] == '+' )
				new_guard[i] = 'X';
			else if ( i == 0 )
				new_guard[i] = 'X';
			else
				new_guard[i] = '_';
		}

		guard = new_guard;
	}

	if ( flag_headers && !includes )
	{
		if ( !strcmp(type, "int8_t") ||
		     !strcmp(type, "uint8_t") )
			includes = "#include <stdint.h>";
	}

	if ( !identifier )
	{
		char* new_identifier;
		if ( output )
			new_identifier = strdup(output);
		else if ( 2 <= argc && strcmp(argv[1], "-") != 0 )
			new_identifier = strdup(argv[1]);
		else
			new_identifier = strdup("carray");
		if ( !new_identifier )
			err(1, "strdup");

		for ( size_t i = 0; new_identifier[i]; i++ )
		{
			if ( i && new_identifier[i] == '.' && !strchr(new_identifier + i, '/') )
				new_identifier[i] = '\0';
			else if ( 'a' <= new_identifier[i] && new_identifier[i] <= 'z' )
				continue;
			else if ( 'A' <= new_identifier[i] && new_identifier[i] <= 'Z' )
				new_identifier[i] = 'a' + new_identifier[i] - 'A';
			else if ( i != 0 && '0' <= new_identifier[i] && new_identifier[i] <= '9' )
				continue;
			else if ( guard[i] == '+' )
				new_identifier[i] = 'x';
			else if ( i == 0 )
				new_identifier[i] = 'x';
			else
				new_identifier[i] = '_';
		}

		identifier = new_identifier;
	}

	if ( output && !freopen(output, "w", stdout) )
		err(1, "%s", output);

	if ( flag_guard && guard )
	{
		printf("#ifndef %s\n", guard);
		printf("#define %s\n", guard);
		printf("\n");
	}

	if ( flag_headers && includes )
	{
		printf("%s\n", includes);
		printf("\n");
	}

	if ( !flag_raw )
	{
		if ( flag_extern_c )
		{
			printf("#if defined(__cplusplus)\n");
			printf("extern \"C\" {\n");
			printf("#endif\n");
			printf("\n");
		}
		if ( flag_extern )
			printf("extern ");
		if ( flag_static )
			printf("static ");
		if ( flag_const )
			printf("const ");
		if ( flag_volatile )
			printf("volatile ");
		printf("%s %s[]", type, identifier);
		if ( flag_forward )
			printf(";\n");
		else
			printf(" = {\n");
	}

	if ( !flag_forward )
	{
		bool begun_row = false;
		unsigned int position = 0;

		for ( int i = 0; i < argc; i++ )
		{
			if ( i == 0 && 2 <= argc )
				continue;
			FILE* fp;
			const char* arg;
			if ( argc == 1 || !strcmp(argv[i], "-") )
			{
				arg = "<stdin>";
				fp = stdin;
			}
			else
			{
				arg = argv[i];
				fp = fopen(arg, "r");
			}
			if ( !fp )
				err(1, "%s", arg);
			int ic;
			while ( (ic = fgetc(fp)) != EOF )
			{
				printf("%c0x%02X,", position++ ? ' ' : '\t', ic);
				begun_row = true;
				if ( position == (80 - 8) / 6 )
				{
					printf("\n");
					position = 0;
					begun_row = false;
				}
			}
			if ( ferror(fp) )
				err(1, "fgetc: %s", arg);
			if ( fp != stdin )
				fclose(fp);
		}

		if ( begun_row )
			printf("\n");
	}

	if ( !flag_raw )
	{
		if ( !flag_forward )
			printf("};\n");
		if ( flag_extern_c )
		{
			printf("\n");
			printf("#if defined(__cplusplus)\n");
			printf("} /* extern \"C\" */\n");
			printf("#endif\n");
		}
	}

	if ( flag_guard && guard )
	{
		printf("\n");
		printf("#endif\n");
	}

	if ( ferror(stdout) || fflush(stdout) == EOF )
		err(1, "%s", output ? output : "stdout");

	return 0;
}
