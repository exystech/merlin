/*
 * Copyright (c) 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * expr.c
 * Evaluate expressions.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TODO: Support the `expr + foo' GNU syntax where foo is treated as a string
//       literal, but this requires disambiguating stuff like this:
//         `expr foo : + foo'
//         `expr + 5 + + 2 +'
// TODO: Support the other GNU function extensions documented in help().

static bool integer_of_string(const char* str, intmax_t* out)
{
	if ( !isdigit((unsigned char) str[0]) )
		return false;
	char* endptr;
	errno = 0;
	intmax_t result = strtoimax((char*) str, &endptr, 10);
	if ( (result == INTMAX_MIN || result == INTMAX_MAX) && errno == ERANGE )
		return false;
	if ( *endptr )
		return false;
	return *out = result, true;
}

static char* strdup_or_die(const char* str)
{
	char* result = strdup(str);
	if ( !str )
		err(2, "strdup");
	return result;
}

static char* strndup_or_die(const char* str, size_t n)
{
	char* result = strndup(str, n);
	if ( !str )
		err(2, "strndup");
	return result;
}

static char* print_intmax_or_die(intmax_t value)
{
	char value_string[sizeof(intmax_t) * 3];
	snprintf(value_string, sizeof(value_string), "%ji", value);
	return strdup_or_die(value_string);
}

static void syntax_error(void)
{
	errx(2, "syntax error");
}

static void non_integer_argument(void)
{
	errx(2, "non-integer argument");
}

static void division_by_zero(void)
{
	errx(2, "division by zero");
}

static void overflow(const char* op)
{
	errx(2, "%s overflow", op);
}

static char* interpret(char** tokens, size_t num_tokens);

static char* interpret_left_associative(char** tokens,
                                        size_t num_tokens,
                                        const char* operator_name,
                                        char* (*next)(char**, size_t, const void*),
                                        const void* next_context,
                                        char* (*function)(const char*, const char*))
{
	size_t depth = 0;
	for ( size_t n = num_tokens; n != 0; n-- )
	{
		size_t i = n - 1;
		if ( !strcmp(tokens[i], ")") )
		{
			depth++;
			continue;
		}
		if ( !strcmp(tokens[i], "(") )
		{
			if ( depth == 0 )
				syntax_error();
			depth--;
			continue;
		}
		if ( depth != 0 )
			continue;
		if ( strcmp(tokens[i], operator_name) != 0 )
			continue;
		if ( i == 0 )
			syntax_error();
		if ( i + 1 == num_tokens )
			syntax_error();
		char** left_tokens = tokens;
		size_t num_left_tokens = i;
		char** right_tokens = tokens + i + 1;
		size_t num_right_tokens = num_tokens - (i + 1);
		char* left_value =
			interpret_left_associative(left_tokens, num_left_tokens,
			                           operator_name, next, next_context,
			                           function);
		char* right_value = next(right_tokens, num_right_tokens, next_context);
		char* value = function(left_value, right_value);
		free(left_value);
		free(right_value);
		return value;
	}

	if ( 0 < depth )
		syntax_error();

	return next(tokens, num_tokens, next_context);
}

static char* bool_to_boolean_value(bool b)
{
	return strdup_or_die(b ? "1" : "0");
}

static char* interpret_literal(char** tokens,
                               size_t num_tokens,
                               const void* ctx)
{
	(void) ctx;
	if ( num_tokens != 1 )
		syntax_error();
	return strdup_or_die(tokens[0]);
}

static char* interpret_parentheses(char** tokens,
                                   size_t num_tokens,
                                   const void* ctx)
{
	if ( 2 <= num_tokens &&
	     strcmp(tokens[0], "(") == 0 &&
	     strcmp(tokens[num_tokens-1], ")") == 0 )
		return interpret(tokens + 1, num_tokens - 2);
	return interpret_literal(tokens, num_tokens, ctx);
}

static char* evaluate_and(const char* a, const char* b)
{
	if ( strcmp(a, "") != 0 && strcmp(a, "0") != 0 &&
	     strcmp(b, "") != 0 && strcmp(b, "0") != 0 )
		return strdup_or_die(a);
	return strdup_or_die("0");
}

static char* evaluate_or(const char* a, const char* b)
{
	if ( strcmp(a, "") != 0 && strcmp(a, "0") != 0 )
		return strdup_or_die(a);
	if ( strcmp(b, "") != 0 && strcmp(b, "0") != 0 )
		return strdup_or_die(b);
	return strdup_or_die("0");
}

static int compare_values(const char* a, const char* b)
{
	intmax_t a_int, b_int;
	if ( integer_of_string(a, &a_int) &&
	     integer_of_string(b, &b_int) )
	{
		if ( a_int < b_int )
			return -1;
		if ( a_int > b_int )
			return 1;
		return 0;
	}
	return strcoll(a, b);
}

static char* evaluate_eq(const char* a, const char* b)
{
	return bool_to_boolean_value(compare_values(a, b) == 0);
}

static char* evaluate_gt(const char* a, const char* b)
{
	return bool_to_boolean_value(0 < compare_values(a, b));
}

static char* evaluate_ge(const char* a, const char* b)
{
	return bool_to_boolean_value(0 <= compare_values(a, b));
}

static char* evaluate_lt(const char* a, const char* b)
{
	return bool_to_boolean_value(compare_values(a, b) < 0);
}

static char* evaluate_le(const char* a, const char* b)
{
	return bool_to_boolean_value(compare_values(a, b) <= 0);
}

static char* evaluate_neq(const char* a, const char* b)
{
	return bool_to_boolean_value(compare_values(a, b) != 0);
}

static char* evaluate_integer_function(const char* a, const char* b,
                                       intmax_t (*function)(intmax_t, intmax_t))
{
	intmax_t a_int;
	intmax_t b_int;
	if ( !integer_of_string(a, &a_int) ||
	     !integer_of_string(b, &b_int) )
		non_integer_argument();
	return print_intmax_or_die(function(a_int, b_int));
}

static intmax_t integer_add(intmax_t a, intmax_t b)
{
	intmax_t result = 0;
	if ( __builtin_add_overflow(a, b, &result) )
		overflow("addition");
	return result;
}

static char* evaluate_add(const char* a, const char* b)
{
	return evaluate_integer_function(a, b, integer_add);
}

static intmax_t integer_sub(intmax_t a, intmax_t b)
{
	intmax_t result = 0;
	if ( __builtin_sub_overflow(a, b, &result) )
		overflow("subtraction");
	return result;
}

static char* evaluate_sub(const char* a, const char* b)
{
	return evaluate_integer_function(a, b, integer_sub);
}

static intmax_t integer_mul(intmax_t a, intmax_t b)
{
	intmax_t result = 0;
	if ( __builtin_mul_overflow(a, b, &result) )
		overflow("multiplication");
	return result;
}

static char* evaluate_mul(const char* a, const char* b)
{
	return evaluate_integer_function(a, b, integer_mul);
}

static intmax_t integer_div(intmax_t a, intmax_t b)
{
	if ( b == 0 )
		division_by_zero();
	if ( a == INTMAX_MIN && b == -1 )
		overflow("division");
	return a / b;
}

static char* evaluate_div(const char* a, const char* b)
{
	return evaluate_integer_function(a, b, integer_div);
}

// TODO: Is this fully well-defined?
static intmax_t integer_mod(intmax_t a, intmax_t b)
{
	if ( b == 0 )
		division_by_zero();
	if ( a == INTMAX_MIN && b == -1 )
		overflow("division");
	return a % b;
}

static char* evaluate_mod(const char* a, const char* b)
{
	return evaluate_integer_function(a, b, integer_mod);
}

static char* evaluate_match(const char* a, const char* b)
{
	regex_t regex;
	int status = regcomp(&regex, b, 0);
	if ( status != 0 )
	{
		char errbuf[256];
		const char* errmsg = errbuf;
		char* erralloc = NULL;
		size_t errbuf_needed;
		if ( sizeof(errbuf) < (errbuf_needed = regerror(status, &regex, errbuf,
		                                                sizeof(errbuf))) )
		{
			if ( (erralloc = (char*) malloc(errbuf_needed)) )
			{
				errmsg = erralloc;
				regerror(status, &regex, erralloc, errbuf_needed);
			}
		}
		errx(2, "compiling regular expression: %s", errmsg);
		free(erralloc);
	}

	char* result;

	regmatch_t rm[2];
	if ( regexec(&regex, a, 2, rm, 0) == 0 && rm[0].rm_so == 0 )
	{
		if ( 0 <= rm[1].rm_so )
			result = strndup_or_die(a + rm[1].rm_so, rm[1].rm_eo - rm[1].rm_so);
		else
			result = print_intmax_or_die(rm[0].rm_eo);
	}
	else
	{
		if ( 0 < regex.re_nsub )
			result = strdup_or_die("");
		else
			result = strdup_or_die("0");
	}

	regfree(&regex);

	return result;
}

struct binary_operator
{
	const char* operator_name;
	char* (*function)(const char*, const char*);
};

struct binary_operator binary_operators[] =
{
	{ "|", evaluate_or },
	{ "&", evaluate_and },
	{ "=", evaluate_eq },
	{ ">", evaluate_gt },
	{ ">=", evaluate_ge },
	{ "<", evaluate_lt },
	{ "<=", evaluate_le },
	{ "!=", evaluate_neq },
	{ "+", evaluate_add },
	{ "-", evaluate_sub },
	{ "*", evaluate_mul },
	{ "/", evaluate_div },
	{ "%", evaluate_mod },
	{ ":", evaluate_match },
};

static char* interpret_binary_operator(char** tokens,
                                       size_t num_tokens,
                                       const void* context)
{
	size_t index = *(const size_t*) context;
	size_t next_index = index + 1;

	char* (*next)(char**, size_t, const void*);
	const void* next_context;

	if ( next_index == sizeof(binary_operators) / sizeof(binary_operators[0]) )
	{
		next = interpret_parentheses;
		next_context = NULL;
	}
	else
	{
		next = interpret_binary_operator;
		next_context = &next_index;
	}

	struct binary_operator* binop = &binary_operators[index];
	return interpret_left_associative(tokens, num_tokens, binop->operator_name,
	                                  next, next_context, binop->function);
}

static char* interpret(char** tokens, size_t num_tokens)
{
	if ( !num_tokens )
		syntax_error();
	size_t operator_index = 0;
	return interpret_binary_operator(tokens, num_tokens, &operator_index);
}

static void help(FILE* fp, const char* argv0)
{
	fprintf(fp, "Usage: %s EXPRESSION\n", argv0);
	fprintf(fp, "  or:  %s OPTION\n", argv0);
	fprintf(fp, "\n");
	fprintf(fp, "      --help     display this help and exit\n");
	fprintf(fp, "      --version  output version information and exit\n");
	fprintf(fp, "\n");
	fprintf(fp, "Print the value of EXPRESSION to standard output.  A blank line below\n");
	fprintf(fp, "separates increasing precedence groups.  EXPRESSION may be:\n");
	fprintf(fp, "\n");
	fprintf(fp, "  ARG1 | ARG2       ARG1 if it is neither null nor 0, otherwise ARG2\n");
	fprintf(fp, "\n");
	fprintf(fp, "  ARG1 & ARG2       ARG1 if neither argument is null or 0, otherwise 0\n");
	fprintf(fp, "\n");
	fprintf(fp, "  ARG1 < ARG2       ARG1 is less than ARG2\n");
	fprintf(fp, "  ARG1 <= ARG2      ARG1 is less than or equal to ARG2\n");
	fprintf(fp, "  ARG1 = ARG2       ARG1 is equal to ARG2\n");
	fprintf(fp, "  ARG1 != ARG2      ARG1 is unequal to ARG2\n");
	fprintf(fp, "  ARG1 >= ARG2      ARG1 is greater than or equal to ARG2\n");
	fprintf(fp, "  ARG1 > ARG2       ARG1 is greater than ARG2\n");
	fprintf(fp, "\n");
	fprintf(fp, "  ARG1 + ARG2       arithmetic sum of ARG1 and ARG2\n");
	fprintf(fp, "  ARG1 - ARG2       arithmetic difference of ARG1 and ARG2\n");
	fprintf(fp, "\n");
	fprintf(fp, "  ARG1 * ARG2       arithmetic product of ARG1 and ARG2\n");
	fprintf(fp, "  ARG1 / ARG2       arithmetic quotient of ARG1 divided by ARG2\n");
	fprintf(fp, "  ARG1 %% ARG2       arithmetic remainder of ARG1 divided by ARG2\n");
	fprintf(fp, "\n");
	fprintf(fp, "  STRING : REGEXP   anchored pattern match of REGEXP in STRING\n");
	fprintf(fp, "\n");
#if 0
	fprintf(fp, "  match STRING REGEXP        same as STRING : REGEXP\n");
	fprintf(fp, "  substr STRING POS LENGTH   substring of STRING, POS counted from 1\n");
	fprintf(fp, "  index STRING CHARS         index in STRING where any CHARS is found, or 0\n");
	fprintf(fp, "  length STRING              length of STRING\n");
	fprintf(fp, "  + TOKEN                    interpret TOKEN as a string, even if it is a\n");
	fprintf(fp, "                               keyword like `match' or an operator like `/'\n");
#endif
	fprintf(fp, "\n");
	fprintf(fp, "  ( EXPRESSION )             value of EXPRESSION\n");
	fprintf(fp, "\n");
	fprintf(fp, "Beware that many operators need to be escaped or quoted for shells.\n");
	fprintf(fp, "Comparisons are arithmetic if both ARGs are numbers, else lexicographical.\n");
	fprintf(fp, "Pattern matches return the string matched between \\<( and \\) or null; if\n");
	fprintf(fp, "\\( and \\) are not used, they return the number of characters matched or 0.\n");
	fprintf(fp, "\n");
	fprintf(fp, "Exit status is 0 if EXPRESSION is neither null nor 0, 1 if EXPRESSION is null\n");
	fprintf(fp, "or 0, 2 if EXPRESSION is syntactically invalid, and 3 if an error occurred.\n");
}

static void version(FILE* fp, const char* argv0)
{
	fprintf(fp, "%s (Sortix) %s\n", argv0, VERSIONSTR);
}

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "");

	if ( argc == 2 && !strcmp(argv[1], "--help") )
		help(stdout, argv[0]), exit(0);
	if ( argc == 2 && !strcmp(argv[1], "--version") )
		version(stdout, argv[0]), exit(0);

	char* value = interpret(argv + 1, argc - 1);
	printf("%s\n", value);
	bool success = strcmp(value, "") != 0 && strcmp(value, "0") != 0;
	free(value);

	return success ? 0 : 1;
}
