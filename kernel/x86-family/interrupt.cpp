/*
 * Copyright (c) 2011, 2012, 2013, 2014 Jonas 'Sortie' Termansen.
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
 * x86-family/interrupt.cpp
 * Interrupt support for i386 and x86_64 systems.
 */

#include <assert.h>
#include <errno.h>
#include <msr.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sortix/kernel/copy.h>
#include <sortix/kernel/cpu.h>
#include <sortix/kernel/descriptor.h>
#include <sortix/kernel/interrupt.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/scheduler.h>
#include <sortix/kernel/signal.h>
#include <sortix/kernel/syscall.h>
#include <sortix/kernel/thread.h>

#include "gdt.h"
#include "idt.h"
#include "pic.h"

extern "C" void isr0();
extern "C" void isr1();
extern "C" void isr2();
extern "C" void isr3();
extern "C" void isr4();
extern "C" void isr5();
extern "C" void isr6();
extern "C" void isr7();
extern "C" void isr8();
extern "C" void isr9();
extern "C" void isr10();
extern "C" void isr11();
extern "C" void isr12();
extern "C" void isr13();
extern "C" void isr14();
extern "C" void isr15();
extern "C" void isr16();
extern "C" void isr17();
extern "C" void isr18();
extern "C" void isr19();
extern "C" void isr20();
extern "C" void isr21();
extern "C" void isr22();
extern "C" void isr23();
extern "C" void isr24();
extern "C" void isr25();
extern "C" void isr26();
extern "C" void isr27();
extern "C" void isr28();
extern "C" void isr29();
extern "C" void isr30();
extern "C" void isr31();
extern "C" void isr128();
extern "C" void isr130();
extern "C" void isr131();
extern "C" void irq0();
extern "C" void irq1();
extern "C" void irq2();
extern "C" void irq3();
extern "C" void irq4();
extern "C" void irq5();
extern "C" void irq6();
extern "C" void irq7();
extern "C" void irq8();
extern "C" void irq9();
extern "C" void irq10();
extern "C" void irq11();
extern "C" void irq12();
extern "C" void irq13();
extern "C" void irq14();
extern "C" void irq15();
extern "C" void interrupt_handler_null();
extern "C" void syscall_handler();
extern "C" void yield_cpu_handler();
extern "C" void thread_exit_handler();
// Forward declare the raw handlers in kernel/x64/interrupt.S.
extern "C" void secure_call_handler();
extern "C" void secure_ret_handler();
extern "C" void secure_indirect_call_handler();
extern "C" void secure_indirect_jmp_handler();
extern "C" void secure_setjmp_handler();
extern "C" void secure_longjmp_handler();

namespace Sortix {
namespace Interrupt {

extern "C" { unsigned long asm_is_cpu_interrupted = 0; }

const size_t NUM_KNOWN_EXCEPTIONS = 20;
const char* exception_names[] =
{
	"Divide by zero",              /* 0, 0x0 */
	"Debug",                       /* 1, 0x1 */
	"Non maskable interrupt",      /* 2, 0x2 */
	"Breakpoint",                  /* 3, 0x3 */
	"Into detected overflow",      /* 4, 0x4 */
	"Out of bounds",               /* 5, 0x5 */
	"Invalid opcode",              /* 6, 0x6 */
	"No coprocessor",              /* 7, 0x7 */
	"Double fault",                /* 8, 0x8 */
	"Coprocessor segment overrun", /* 9, 0x9 */
	"Bad TSS",                     /* 10, 0xA */
	"Segment not present",         /* 11, 0xB */
	"Stack fault",                 /* 12, 0xC */
	"General protection fault",    /* 13, 0xD */
	"Page fault",                  /* 14, 0xE */
	"Unknown interrupt",           /* 15, 0xF */
	"Coprocessor fault",           /* 16, 0x10 */
	"Alignment check",             /* 17, 0x11 */
	"Machine check",               /* 18, 0x12 */
	"SIMD Floating-Point",         /* 19, 0x13 */
};

const unsigned int NUM_INTERRUPTS = 256U;

static struct IDT::idt_entry interrupt_table[NUM_INTERRUPTS];
static struct interrupt_handler* interrupt_handlers[NUM_INTERRUPTS];

static struct interrupt_handler Scheduler__InterruptYieldCPU_handler;
static struct interrupt_handler Signal__DispatchHandler_handler;
static struct interrupt_handler Signal__ReturnHandler_handler;
static struct interrupt_handler Scheduler__ThreadExitCPU_handler;
// Storage for the registration of the CFI interrupt handers.
static struct interrupt_handler CFI__secure_call_handler;
static struct interrupt_handler CFI__secure_ret_handler;
static struct interrupt_handler CFI__secure_indirect_call_handler;
static struct interrupt_handler CFI__secure_indirect_jmp_handler;
static struct interrupt_handler CFI__secure_setjmp_handler;
static struct interrupt_handler CFI__secure_longjmp_handler;

// Terminates the process abnormally with SIGABRT due to a CFI violation.
__attribute__((noreturn))
static void CFIViolation(const char* event)
{
	Process* process = CurrentProcess();
	process->ExitThroughSignal(SIGABRT);
	Log::PrintF("%s[%ji]: CFI violation: %s\n",
	            process->program_image_path, (intmax_t) process->pid, event);
	kthread_exit();
}

// The secure call system call. The destination address is in the rax register
// and the return address is the current instruction (as the int $0x90
// instruction has already been executed, rip will point to the next one). The
// return pointer is saved in the call stack array, and rip is changed to the
// destination address (rax). The stack is still decremented by 8, as the call
// instruction normally would, but the memory is unchanged (uninitialized) and
// unused (the called function would use secure ret). It's important to still
// decrement the stack by 8 because the System V ABI for x86_64 requires 16-byte
// stack alignment, which we must preserve, and the call instruction pushes 8
// bytes. The rax register is safe to clobber because it is used for the return
// value of the called function and there is no expectation that it would stay
// intact.
void SecureCallHandler(struct interrupt_context* intctx, void*)
{
	Thread* thread = CurrentThread();
	if ( thread->call_count == CFI_MAX_CALL_DEPTH )
		CFIViolation("secure call stack overflow");
	thread->calls[thread->call_count++] = intctx->rip;
	intctx->rip = intctx->rax;
	intctx->rsp -= 8;
}

// The secure return system call. There are no parameters. It simply pops the
// saved return pointer in the call stack, transfers execution to that location
// by setting rip, and then incrementing the stack by 8 as the ret instruction
// would do. If any setjmp objects go out of scope, they are cleaned up.
void SecureRetHandler(struct interrupt_context* intctx, void*)
{
	Thread* thread = CurrentThread();
	if ( thread->call_count == 0 )
		CFIViolation("secure call stack underflow");
	intctx->rip = thread->calls[--thread->call_count];
	intctx->rsp += 8;
	// jmp_buf registations may have gone out of scope.
	while ( thread->setjmp_count &&
            thread->call_count < thread->setjmps[thread->setjmp_count-1].call_level )
		thread->setjmp_count--;
}

// Control flow comparison function used for binary search of valid control
// flow in the control flow graph.
static int SearchControlFlow(const void* key_ptr, const void* cand_ptr)
{
	const struct control_flow* key = (const struct control_flow*) key_ptr;
	const struct control_flow* cand = (const struct control_flow*) cand_ptr;
	if ( cand->from < key->from )
		return 1;
	if ( cand->from > key->from )
		return -1;
	if ( cand->to < key->to )
		return 1;
	if ( cand->to > key->to )
		return -1;
	return 0;
}

// Verifies control flow is in the control flow graph.
static void CheckControlFlow(const struct control_flow* cf)
{
	Process* process = CurrentProcess();
	// If recording, don't enforce the control flow graph, but record instead.
	if ( process->cfi_dump )
	{
		// Don't record this control flow if already seen.
		for ( size_t i = 0; i < process->cfg_length; i++ )
		{
			if ( process->cfg[i].from == cf->from &&
				 process->cfg[i].to == cf->to )
				return;
		}
		// Double the control flow graph array in size if it is full.
		if ( process->cfg_length == process->cfg_allocated )
		{
			size_t new_allocated = process->cfg_allocated * 2;
			if ( new_allocated == 0 )
				new_allocated = 16;
			struct control_flow* new_cfg = new struct control_flow[new_allocated];
			if ( !new_cfg )
				CFIViolation("recording CFG: Out of memory");
			if ( process->cfg )
				memcpy(new_cfg, process->cfg, sizeof(struct control_flow) * process->cfg_length);
			delete[] process->cfg;
			process->cfg = new_cfg;
			process->cfg_allocated = new_allocated;
		}
		// Add the control flow to the (unsorted) control flow graph.
		process->cfg[process->cfg_length++] = *cf;
		ioctx_t ctx;
		SetupKernelIOCtx(&ctx);
		// Write the control flow to the trace file.
		if ( process->cfi_dump->write(&ctx, (const uint8_t*) cf, sizeof(*cf)) != sizeof(*cf) )
			CFIViolation("recording CFG: Trace file write failure");
		return;
	}

	// If there is no control flow graph loaded, don't enforce.
	if ( !process->cfg )
		return;

	// Use binary search to look up the control flow in the sorted control
	// flow graph.
	if ( bsearch(cf, process->cfg, process->cfg_length,
	             sizeof(struct control_flow), SearchControlFlow) )
		return;

	CFIViolation("control flow failure");
}

// Call a function, but only after verifying the control flow using the
// control flow graph. The destination is in the rax register.
void SecureIndirectCallHandler(struct interrupt_context* intctx, void* ctx)
{
	struct control_flow cf;
	cf.from = intctx->rip - 2 /* point to start of int 0x92, not the end */;
	cf.to = intctx->rax;
	CheckControlFlow(&cf);
	SecureCallHandler(intctx, ctx);
}

// Jump to code, but only after verifying the control flow. The destination
// is on the stack, after the red zone has skipped.
void SecureIndirectJmpHandler(struct interrupt_context* intctx, void*)
{
	uintptr_t dest;
	if ( !CopyFromUser(&dest, (uintptr_t*) intctx->rsp, sizeof(dest)) )
		CFIViolation("secure indirect jmp: Failed to read stack parameter");
	intctx->rsp += 8; // Unwind the parameter.
	intctx->rsp += 128; // Unwind the red zone.
	struct control_flow cf;
	cf.from = intctx->rip - 2 /* point to start of int 0x92, not the end */;
	cf.to = dest;
	CheckControlFlow(&cf);
	intctx->rip = dest;
}

// The secure setjmp handler. Registers a jmp_buf object such that it can be
// securely longjmp'd to later.
void SecureSetJmpHandler(struct interrupt_context* intctx, void*)
{
	Thread* thread = CurrentThread();
	// TODO: Check if overwriting existing setjmp.
	if ( thread->setjmp_count == 16 )
		CFIViolation("secure setjmp: jmp_buf registation stack overflow");
	if ( thread->call_count == 0 )
		CFIViolation("secure setjmp: secure stack underflow");
	struct secure_setjmp* setjmp = &thread->setjmps[thread->setjmp_count++];
	// Subtracted by one here, to remember the function that called setjmp, not
	// the libc setjmp function itself.
	setjmp->call_level = thread->call_count - 1;
	setjmp->rip = thread->calls[thread->call_count - 1];
	setjmp->jmpbuf_ptr = intctx->rdi;
}

// The secure longjmp handler.
void SecureLongJmpHandler(struct interrupt_context* intctx, void*)
{
	Thread* thread = CurrentThread();
	// Search for a maatching registation.
	for ( size_t i = 0; i < thread->setjmp_count; i++ )
	{
		struct secure_setjmp* setjmp = &thread->setjmps[i];
		if ( setjmp->jmpbuf_ptr != intctx->rdi )
			continue;
		jmp_buf buf;
		if ( !CopyFromUser(&buf, (const jmp_buf*) setjmp->jmpbuf_ptr, sizeof(buf)) )
			CFIViolation("secure longjmp: Failed to read jmp_buf");
		// Restore all the registers from the jmp_buf.
		intctx->rbx = buf[0];
		intctx->rsp = buf[1];
		intctx->rbp = buf[2];
		intctx->r12 = buf[3];
		intctx->r13 = buf[4];
		intctx->r14 = buf[5];
		intctx->r15 = buf[6];
		intctx->rip = setjmp->rip;
		intctx->rax = intctx->rsi;
		// Unwind the protected shadow stack.
		thread->call_count = setjmp->call_level;
		// jmp_buf registations may have gone out of scope.
		while ( thread->setjmp_count &&
		        thread->call_count < thread->setjmps[thread->setjmp_count-1].call_level )
			thread->setjmp_count--;
		return;
	}
	CFIViolation("secure longjmp: No such jmp_buf registation");
}

// Temporarily to see if this is the source of the assertion failure.
void DispatchHandlerWrap(struct interrupt_context* intctx, void* user)
{
	assert(Interrupt::IsEnabled());
	return Signal::DispatchHandler(intctx, user);
}

void RegisterHandler(unsigned int index, struct interrupt_handler* handler)
{
	assert(index < NUM_INTERRUPTS);
	bool was_enabled = SetEnabled(false);
	handler->prev = NULL;
	if ( (handler->next = interrupt_handlers[index]) )
		handler->next->prev = handler;
	interrupt_handlers[index] = handler;
	SetEnabled(was_enabled);
}

void UnregisterHandler(unsigned int index, struct interrupt_handler* handler)
{
	assert(index < NUM_INTERRUPTS);
	bool was_enabled = SetEnabled(false);
	if ( handler->prev )
		handler->prev->next = handler->next;
	else
		interrupt_handlers[index] = handler->next;
	if ( handler->next )
		handler->next->prev = handler->prev;
	SetEnabled(was_enabled);
}

static void RegisterRawHandler(unsigned int index,
                               void (*handler)(void),
                               bool userspace,
                               bool preemptive)
{
	assert(index < NUM_INTERRUPTS);
	addr_t handler_entry = (addr_t) handler;
	uint16_t selector = KCS;
	uint8_t rpl = userspace ? URPL : KRPL;
	uint8_t type = preemptive ? IDT::TYPE_TRAP : IDT::TYPE_INTERRUPT;
	uint8_t ist = 0;
	uint8_t flags = IDT::FLAG_PRESENT
	              | type << IDT::FLAG_TYPE_SHIFT
	              | rpl << IDT::FLAG_DPL_SHIFT;
	IDT::SetEntry(&interrupt_table[index], handler_entry, selector, flags, ist);
}

void Init()
{
	// Initialize the interrupt table entries to the null interrupt handler.
	memset(&interrupt_table, 0, sizeof(interrupt_table));
	for ( unsigned int i = 0; i < NUM_INTERRUPTS; i++ )
		RegisterRawHandler(i, interrupt_handler_null, false, false);

	// Remap the IRQ table on the PICs.
	PIC::ReprogramPIC();

	RegisterRawHandler(0, isr0, false, false);
	RegisterRawHandler(1, isr1, false, false);
	RegisterRawHandler(2, isr2, false, false);
	RegisterRawHandler(3, isr3, false, false);
	RegisterRawHandler(4, isr4, false, false);
	RegisterRawHandler(5, isr5, false, false);
	RegisterRawHandler(6, isr6, false, false);
	RegisterRawHandler(7, isr7, false, false);
	RegisterRawHandler(8, isr8, false, false);
	RegisterRawHandler(9, isr9, false, false);
	RegisterRawHandler(10, isr10, false, false);
	RegisterRawHandler(11, isr11, false, false);
	RegisterRawHandler(12, isr12, false, false);
	RegisterRawHandler(13, isr13, false, false);
	RegisterRawHandler(14, isr14, false, false);
	RegisterRawHandler(15, isr15, false, false);
	RegisterRawHandler(16, isr16, false, false);
	RegisterRawHandler(17, isr17, false, false);
	RegisterRawHandler(18, isr18, false, false);
	RegisterRawHandler(19, isr19, false, false);
	RegisterRawHandler(20, isr20, false, false);
	RegisterRawHandler(21, isr21, false, false);
	RegisterRawHandler(22, isr22, false, false);
	RegisterRawHandler(23, isr23, false, false);
	RegisterRawHandler(24, isr24, false, false);
	RegisterRawHandler(25, isr25, false, false);
	RegisterRawHandler(26, isr26, false, false);
	RegisterRawHandler(27, isr27, false, false);
	RegisterRawHandler(28, isr28, false, false);
	RegisterRawHandler(29, isr29, false, false);
	RegisterRawHandler(30, isr30, false, false);
	RegisterRawHandler(31, isr31, false, false);
	RegisterRawHandler(32, irq0, false, false);
	RegisterRawHandler(33, irq1, false, false);
	RegisterRawHandler(34, irq2, false, false);
	RegisterRawHandler(35, irq3, false, false);
	RegisterRawHandler(36, irq4, false, false);
	RegisterRawHandler(37, irq5, false, false);
	RegisterRawHandler(38, irq6, false, false);
	RegisterRawHandler(39, irq7, false, false);
	RegisterRawHandler(40, irq8, false, false);
	RegisterRawHandler(41, irq9, false, false);
	RegisterRawHandler(42, irq10, false, false);
	RegisterRawHandler(43, irq11, false, false);
	RegisterRawHandler(44, irq12, false, false);
	RegisterRawHandler(45, irq13, false, false);
	RegisterRawHandler(46, irq14, false, false);
	RegisterRawHandler(47, irq15, false, false);
	RegisterRawHandler(128, syscall_handler, true, true);
	RegisterRawHandler(129, yield_cpu_handler, true, false);
	RegisterRawHandler(130, isr130, true, true);
	RegisterRawHandler(131, isr131, true, true);
	RegisterRawHandler(132, thread_exit_handler, true, false);
	// Register the raw interrupt handlers for the 6 new CFI interrupts.
	RegisterRawHandler(0x90, secure_call_handler, true, true);
	RegisterRawHandler(0x91, secure_ret_handler, true, true);
	RegisterRawHandler(0x92, secure_indirect_call_handler, true, true);
	RegisterRawHandler(0x93, secure_indirect_jmp_handler, true, true);
	RegisterRawHandler(0x94, secure_setjmp_handler, true, true);
	RegisterRawHandler(0x95, secure_longjmp_handler, true, true);

	Scheduler__InterruptYieldCPU_handler.handler = Scheduler::InterruptYieldCPU;
	RegisterHandler(129, &Scheduler__InterruptYieldCPU_handler);
	Signal__DispatchHandler_handler.handler = DispatchHandlerWrap;
	RegisterHandler(130, &Signal__DispatchHandler_handler);
	Signal__ReturnHandler_handler.handler = Signal::ReturnHandler;
	RegisterHandler(131, &Signal__ReturnHandler_handler);
	Scheduler__ThreadExitCPU_handler.handler = Scheduler::ThreadExitCPU;
	RegisterHandler(132, &Scheduler__ThreadExitCPU_handler);
	CFI__secure_call_handler.handler = SecureCallHandler;
	// Register handles for each of the six new CFI interrupts.
	RegisterHandler(0x90, &CFI__secure_call_handler);
	CFI__secure_ret_handler.handler = SecureRetHandler;
	RegisterHandler(0x91, &CFI__secure_ret_handler);
	CFI__secure_indirect_call_handler.handler = SecureIndirectCallHandler;
	RegisterHandler(0x92, &CFI__secure_indirect_call_handler);
	CFI__secure_indirect_jmp_handler.handler = SecureIndirectJmpHandler;
	RegisterHandler(0x93, &CFI__secure_indirect_jmp_handler);
	CFI__secure_setjmp_handler.handler = SecureSetJmpHandler;
	RegisterHandler(0x94, &CFI__secure_setjmp_handler);
	CFI__secure_longjmp_handler.handler = SecureLongJmpHandler;
	RegisterHandler(0x95, &CFI__secure_longjmp_handler);

	IDT::Set(interrupt_table, NUM_INTERRUPTS);

	Interrupt::Enable();
}

const char* ExceptionName(const struct interrupt_context* intctx)
{
	if ( intctx->int_no < NUM_KNOWN_EXCEPTIONS )
		return exception_names[intctx->int_no];
	return "Unknown";
}

uintptr_t ExceptionLocation(const struct interrupt_context* intctx)
{
#if defined(__x86_64__)
	return intctx->rip;
#elif defined(__i386__)
	return intctx->eip;
#endif
}

__attribute__((noreturn))
void KernelCrashHandler(struct interrupt_context* intctx)
{
	Scheduler::SaveInterruptedContext(intctx, &CurrentThread()->registers);

	// Panic the kernel with a diagnostic message.
	PanicF("Unhandled CPU Exception id %zu `%s' at ip=0x%zx (cr2=0x%zx, "
	       "err_code=0x%zx)", intctx->int_no, ExceptionName(intctx),
	       ExceptionLocation(intctx), intctx->cr2, intctx->err_code);
}

void UserCrashHandler(struct interrupt_context* intctx)
{
	Scheduler::SaveInterruptedContext(intctx, &CurrentThread()->registers);

	// Execute this crash handler with preemption on.
	Interrupt::Enable();

	// TODO: Also send signals for other types of user-space crashes.
	if ( intctx->int_no == 14 /* Page fault */ )
	{
		struct sigaction* act = &CurrentProcess()->signal_actions[SIGSEGV];
		kthread_mutex_lock(&CurrentProcess()->signal_lock);
		bool handled = act->sa_handler != SIG_DFL && act->sa_handler != SIG_IGN;
		if ( handled )
			CurrentThread()->DeliverSignalUnlocked(SIGSEGV);
		kthread_mutex_unlock(&CurrentProcess()->signal_lock);
		if ( handled )
		{
			assert(Interrupt::IsEnabled());
			return Signal::DispatchHandler(intctx, NULL);
		}
	}

	// Issue a diagnostic message to the kernel log concerning the crash.
	Log::PrintF("The current process (pid %ji `%s') crashed and was terminated:\n",
	            (intmax_t) CurrentProcess()->pid, CurrentProcess()->program_image_path);
	Log::PrintF("%s exception at ip=0x%zx (cr2=0x%zx, err_code=0x%zx)\n",
	            ExceptionName(intctx), ExceptionLocation(intctx), intctx->cr2,
	            intctx->err_code);

	// Exit the process with the right error code.
	// TODO: Send a SIGINT, SIGBUS, or whatever instead.
	CurrentProcess()->ExitThroughSignal(SIGSEGV);

	// Deliver signals to this thread so it can exit correctly.
	assert(Interrupt::IsEnabled());
	Signal::DispatchHandler(intctx, NULL);
}

extern "C" void interrupt_handler(struct interrupt_context* intctx)
{
	unsigned int int_no = intctx->int_no;

	// IRQ 7 and 15 might be spurious and might need to be ignored.
	if ( int_no == IRQ7 && !(PIC::ReadISR() & (1 << 7)) )
		return;
	if ( int_no == IRQ15 && !(PIC::ReadISR() & (1 << 15)) )
	{
		PIC::SendMasterEOI();
		return;
	}

	bool is_in_kernel = (intctx->cs & 0x3) == KRPL;
	bool is_in_user = !is_in_kernel;
	bool is_crash = int_no < 32 && int_no != 7;

	// Invoke the appropriate interrupt handler.
	if ( is_crash && is_in_kernel )
		KernelCrashHandler(intctx);
	else if ( is_crash && is_in_user )
		UserCrashHandler(intctx);
	else
	{
		for ( struct interrupt_handler* iter = interrupt_handlers[int_no];
		      iter;
		      iter = iter->next )
			iter->handler(intctx, iter->context);
	}

	// Send an end of interrupt signal to the PICs if we got an IRQ.
	if ( IRQ0 <= int_no && int_no <= IRQ15 )
		PIC::SendEOI(int_no - IRQ0);
}

} // namespace Interrupt
} // namespace Sortix
