/*
 * Copyright (c) 2011, 2012, 2013, 2014, 2015 Jonas 'Sortie' Termansen.
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
 * x64/interrupt.S
 * Transfers control to interrupt handlers when interrupts happen.
 */

.section .text

.global isr0
.type isr0, @function
isr0:
	pushq $0 # err_code
	pushq $0 # int_no
	jmp interrupt_handler_prepare
.global isr1
.type isr1, @function
isr1:
	pushq $0 # err_code
	pushq $1 # int_no
	jmp interrupt_handler_prepare
.global isr2
.type isr2, @function
isr2:
	pushq $0 # err_code
	pushq $2 # int_no
	jmp interrupt_handler_prepare
.global isr3
.type isr3, @function
isr3:
	pushq $0 # err_code
	pushq $3 # int_no
	jmp interrupt_handler_prepare
.global isr4
.type isr4, @function
isr4:
	pushq $0 # err_code
	pushq $4 # int_no
	jmp interrupt_handler_prepare
.global isr5
.type isr5, @function
isr5:
	pushq $0 # err_code
	pushq $5 # int_no
	jmp interrupt_handler_prepare
.global isr6
.type isr6, @function
isr6:
	pushq $0 # err_code
	pushq $6 # int_no
	jmp interrupt_handler_prepare
.global isr7
.type isr7, @function
isr7:
	pushq $0 # err_code
	pushq $7 # int_no
	jmp interrupt_handler_prepare
.global isr8
.type isr8, @function
isr8:
	# pushq $0 # err_code pushed by CPU
	pushq $8 # int_no
	jmp interrupt_handler_prepare
.global isr9
.type isr9, @function
isr9:
	pushq $0 # err_code
	pushq $9 # int_no
	jmp interrupt_handler_prepare
.global isr10
.type isr10, @function
isr10:
	# pushq $0 # err_code pushed by CPU
	pushq $10 # int_no
	jmp interrupt_handler_prepare
.global isr11
.type isr11, @function
isr11:
	# pushq $0 # err_code pushed by CPU
	pushq $11 # int_no
	jmp interrupt_handler_prepare
.global isr12
.type isr12, @function
isr12:
	# pushq $0 # err_code pushed by CPU
	pushq $12 # int_no
	jmp interrupt_handler_prepare
.global isr13
.type isr13, @function
isr13:
	# pushq $0 # err_code pushed by CPU
	pushq $13 # int_no
	jmp interrupt_handler_prepare
.global isr14
.type isr14, @function
isr14:
	# pushq $0 # err_code pushed by CPU
	pushq $14 # int_no
	jmp interrupt_handler_prepare
.global isr15
.type isr15, @function
isr15:
	pushq $0 # err_code
	pushq $15 # int_no
	jmp interrupt_handler_prepare
.global isr16
.type isr16, @function
isr16:
	pushq $0 # err_code
	pushq $16 # int_no
	jmp interrupt_handler_prepare
.global isr17
.type isr17, @function
isr17:
	# pushq $0 # err_code pushed by CPU
	pushq $17 # int_no
	jmp interrupt_handler_prepare
.global isr18
.type isr18, @function
isr18:
	pushq $0 # err_code
	pushq $18 # int_no
	jmp interrupt_handler_prepare
.global isr19
.type isr19, @function
isr19:
	pushq $0 # err_code
	pushq $19 # int_no
	jmp interrupt_handler_prepare
.global isr20
.type isr20, @function
isr20:
	pushq $0 # err_code
	pushq $20 # int_no
	jmp interrupt_handler_prepare
.global isr21
.type isr21, @function
isr21:
	pushq $0 # err_code
	pushq $21 # int_no
	jmp interrupt_handler_prepare
.global isr22
.type isr22, @function
isr22:
	pushq $0 # err_code
	pushq $22 # int_no
	jmp interrupt_handler_prepare
.global isr23
.type isr23, @function
isr23:
	pushq $0 # err_code
	pushq $23 # int_no
	jmp interrupt_handler_prepare
.global isr24
.type isr24, @function
isr24:
	pushq $0 # err_code
	pushq $24 # int_no
	jmp interrupt_handler_prepare
.global isr25
.type isr25, @function
isr25:
	pushq $0 # err_code
	pushq $25 # int_no
	jmp interrupt_handler_prepare
.global isr26
.type isr26, @function
isr26:
	pushq $0 # err_code
	pushq $26 # int_no
	jmp interrupt_handler_prepare
.global isr27
.type isr27, @function
isr27:
	pushq $0 # err_code
	pushq $27 # int_no
	jmp interrupt_handler_prepare
.global isr28
.type isr28, @function
isr28:
	pushq $0 # err_code
	pushq $28 # int_no
	jmp interrupt_handler_prepare
.global isr29
.type isr29, @function
isr29:
	pushq $0 # err_code
	pushq $29 # int_no
	jmp interrupt_handler_prepare
.global isr30
.type isr30, @function
isr30:
	# pushq $0 # err_code pushed by CPU
	pushq $30 # int_no
	jmp interrupt_handler_prepare
.global isr31
.type isr31, @function
isr31:
	pushq $0 # err_code
	pushq $31 # int_no
	jmp interrupt_handler_prepare
.global isr128
.type isr128, @function
isr128:
	pushq $0 # err_code
	pushq $128 # int_no
	jmp interrupt_handler_prepare
.global isr130
.type isr130, @function
isr130:
	pushq $0 # err_code
	pushq $130 # int_no
	jmp interrupt_handler_prepare
.global isr131
.type isr131, @function
isr131:
	pushq $0 # err_code
	pushq $131 # int_no
	jmp interrupt_handler_prepare
.global irq0
.type irq0, @function
irq0:
	pushq $0 # err_code
	pushq $32 # int_no
	jmp interrupt_handler_prepare
.global irq1
.type irq1, @function
irq1:
	pushq $0 # err_code
	pushq $33 # int_no
	jmp interrupt_handler_prepare
.global irq2
.type irq2, @function
irq2:
	pushq $0 # err_code
	pushq $34 # int_no
	jmp interrupt_handler_prepare
.global irq3
.type irq3, @function
irq3:
	pushq $0 # err_code
	pushq $35 # int_no
	jmp interrupt_handler_prepare
.global irq4
.type irq4, @function
irq4:
	pushq $0 # err_code
	pushq $36 # int_no
	jmp interrupt_handler_prepare
.global irq5
.type irq5, @function
irq5:
	pushq $0 # err_code
	pushq $37 # int_no
	jmp interrupt_handler_prepare
.global irq6
.type irq6, @function
irq6:
	pushq $0 # err_code
	pushq $38 # int_no
	jmp interrupt_handler_prepare
.global irq7
.type irq7, @function
irq7:
	pushq $0 # err_code
	pushq $39 # int_no
	jmp interrupt_handler_prepare
.global irq8
.type irq8, @function
irq8:
	pushq $0 # err_code
	pushq $40 # int_no
	jmp interrupt_handler_prepare
.global irq9
.type irq9, @function
irq9:
	pushq $0 # err_code
	pushq $41 # int_no
	jmp interrupt_handler_prepare
.global irq10
.type irq10, @function
irq10:
	pushq $0 # err_code
	pushq $42 # int_no
	jmp interrupt_handler_prepare
.global irq11
.type irq11, @function
irq11:
	pushq $0 # err_code
	pushq $43 # int_no
	jmp interrupt_handler_prepare
.global irq12
.type irq12, @function
irq12:
	pushq $0 # err_code
	pushq $44 # int_no
	jmp interrupt_handler_prepare
.global irq13
.type irq13, @function
irq13:
	pushq $0 # err_code
	pushq $45 # int_no
	jmp interrupt_handler_prepare
.global irq14
.type irq14, @function
irq14:
	pushq $0 # err_code
	pushq $46 # int_no
	jmp interrupt_handler_prepare
.global irq15
.type irq15, @function
irq15:
	pushq $0 # err_code
	pushq $47 # int_no
	jmp interrupt_handler_prepare
.global yield_cpu_handler
.type yield_cpu_handler, @function
yield_cpu_handler:
	pushq $0 # err_code
	pushq $129 # int_no
	jmp interrupt_handler_prepare
.global thread_exit_handler
.type thread_exit_handler, @function
thread_exit_handler:
	pushq $0 # err_code
	pushq $132 # int_no
	jmp interrupt_handler_prepare

interrupt_handler_prepare:
	cld
	movq $1, asm_is_cpu_interrupted

	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rax
	pushq %rcx
	pushq %rdx
	pushq %rbx
	pushq %rbp
	pushq %rsi
	pushq %rdi

	# Push the user-space data segment.
	movl %ds, %ebp
	pushq %rbp

	# Load the kernel data segment.
	movw $0x10, %bp
	movl %ebp, %ds
	movl %ebp, %es

	# Push CR2 in case of page faults
	movq %cr2, %rbp
	pushq %rbp

	# Push the current kernel errno value.
	movl errno, %ebp
	pushq %rbp

	# Push whether a signal is pending.
	movq asm_signal_is_pending, %rbp
	pushq %rbp

	# Now call the interrupt handler.
	movq %rsp, %rdi
	movq %rsp, %rbx
	andq $0xFFFFFFFFFFFFFFF0, %rsp
.global fake_interrupt
.type fake_interrupt, @function
fake_interrupt:
	call interrupt_handler
	movq %rbx, %rsp

load_interrupted_registers:
	# Restore whether signals are pending.
	popq %rbp
	movq %rbp, asm_signal_is_pending

	# Restore the previous kernel errno.
	popq %rbp
	movl %ebp, errno

	# Remove CR2 from the stack.
	addq $8, %rsp

	# Restore the user-space data segment.
	popq %rbp
	movl %ebp, %ds
	movl %ebp, %es

	popq %rdi
	popq %rsi
	popq %rbp
	popq %rbx
	popq %rdx
	popq %rcx
	popq %rax
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15

	# Remove int_no and err_code
	addq $16, %rsp

	movq $0, asm_is_cpu_interrupted

	# Return to where we came from.
	iretq
.size interrupt_handler_prepare, . - interrupt_handler_prepare

.global interrupt_handler_null
.type interrupt_handler_null, @function
interrupt_handler_null:
	iretq
.size interrupt_handler_null, . - interrupt_handler_null

.global load_registers
.type load_registers, @function
load_registers:
	# Let the register struct become our temporary stack
	movq %rdi, %rsp
	jmp load_interrupted_registers
.size load_registers, . - load_registers
