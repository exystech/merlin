/* $NetBSD: lrint.S,v 1.2 2004/10/13 15:18:32 drochner Exp $ */

/*
 * Written by Matthias Drochner <drochner@NetBSD.org>.
 * Public domain.
 */

#include <machine/asm.h>

ENTRY(lrint)
#ifdef __i386__
	pushl   %ebp
	movl    %esp,%ebp
	subl    $4,%esp
	fldl	8(%ebp)
	fistpl	(%esp)
	movl	(%esp),%eax
	leave
	int $0x91
#else
	cvtsd2siq %xmm0,%rax
	int $0x91
#endif
