/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016 Jonas 'Sortie' Termansen.
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
 * sys/syscall.h
 * Functions and macros for declaring system call stubs.
 */

#ifndef INCLUDE_SYS_SYSCALL_H
#define INCLUDE_SYS_SYSCALL_H

#ifdef __is_sortix_libk
#error "This file is part of user-space and should not be built into libk"
#endif

#include <sys/cdefs.h>

#include <sortix/syscall.h>

/* Expand a macro and convert it to string. */
#define SYSCALL_STRINGIFY_EXPAND(foo) #foo

/* Implement the body of a function that selects the right system call and
   jumps into the generic implementation of system calls. */
#if defined(__i386__)

#define SYSCALL_FUNCTION_BODY(syscall_index) \
"	mov $" SYSCALL_STRINGIFY_EXPAND(syscall_index) ", %eax\n" \
"	jmp asm_syscall\n"

#elif defined(__x86_64__)

#define SYSCALL_FUNCTION_BODY(syscall_index) \
"	mov $" SYSCALL_STRINGIFY_EXPAND(syscall_index) ", %rax\n" \
"	jmp asm_syscall\n"

#else

#error "Provide an implementation for your platform."

#endif

/* Create a function that selects the right system call and jumps into the
   generic implementation of system calls. */
#define SYSCALL_FUNCTION(syscall_name, syscall_index) \
__attribute__((unused)) \
static void __verify_index_expression_##syscall_name(void) \
{ \
	(void) syscall_index; \
} \
__asm__("\n" \
".pushsection .text\n" \
".type " #syscall_name ", @function\n" \
#syscall_name ":\n" \
SYSCALL_FUNCTION_BODY(syscall_index) \
".size " #syscall_name ", . - " #syscall_name "\n" \
".popsection\n" \
);

/* Create a function that performs the system call by injecting the right
   instructions into the compiler assembly output. Then provide a declaration of
   the function that looks just like the caller wants it. */
#ifdef __cplusplus
#define DEFINE_SYSCALL(syscall_type, syscall_name, syscall_index, syscall_formals) \
SYSCALL_FUNCTION(syscall_name, syscall_index) \
extern "C" { syscall_type syscall_name syscall_formals; }
#else
#define DEFINE_SYSCALL(syscall_type, syscall_name, syscall_index, syscall_formals) \
SYSCALL_FUNCTION(syscall_name, syscall_index) \
syscall_type syscall_name syscall_formals;
#endif

/* System call accepting no parameters. */
#define DEFN_SYSCALL0(type, fn, num) \
DEFINE_SYSCALL(type, fn, num, (void))

/* System call accepting 1 parameter. */
#define DEFN_SYSCALL1(type, fn, num, t1) \
DEFINE_SYSCALL(type, fn, num, (t1 p1))

/* System call accepting 2 parameters. */
#define DEFN_SYSCALL2(type, fn, num, t1, t2) \
DEFINE_SYSCALL(type, fn, num, (t1 p1, t2 p2))

/* System call accepting 3 parameters. */
#define DEFN_SYSCALL3(type, fn, num, t1, t2, t3) \
DEFINE_SYSCALL(type, fn, num, (t1 p1, t2 p2, t3 p3))

/* System call accepting 4 parameters. */
#define DEFN_SYSCALL4(type, fn, num, t1, t2, t3, t4) \
DEFINE_SYSCALL(type, fn, num, (t1 p1, t2 p2, t3 p3, t4 p4))

/* System call accepting 5 parameters. */
#define DEFN_SYSCALL5(type, fn, num, t1, t2, t3, t4, t5) \
DEFINE_SYSCALL(type, fn, num, (t1 p1, t2 p2, t3 p3, t4 p4, t5 p5))

#endif
