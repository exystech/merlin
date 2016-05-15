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
 * signal.cpp
 * Asynchronous user-space thread interruption.
 */

#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>

#include <sortix/sigaction.h>
#include <sortix/signal.h>
#include <sortix/sigset.h>
#include <sortix/stack.h>
#include <sortix/ucontext.h>

#include <sortix/kernel/copy.h>
#include <sortix/kernel/interrupt.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/ptable.h>
#include <sortix/kernel/signal.h>
#include <sortix/kernel/syscall.h>
#include <sortix/kernel/thread.h>

namespace Sortix {

sigset_t default_ignored_signals;
sigset_t default_stop_signals;
sigset_t unblockable_signals;

// A per-cpu value whether a signal is pending in the running task.
extern "C" { volatile unsigned long asm_signal_is_pending = 0; }

static
void UpdatePendingSignals(Thread* thread) // thread->process->signal_lock held
{
	struct sigaction* signal_actions = thread->process->signal_actions;

	// Determine which signals wouldn't be ignored if received.
	sigset_t handled_signals;
	sigemptyset(&handled_signals);
	for ( int i = 1; i < SIG_MAX_NUM; i++ )
	{
		if ( signal_actions[i].sa_handler == SIG_IGN )
			continue;
		if ( signal_actions[i].sa_handler == SIG_DFL &&
		     sigismember(&default_ignored_signals, i) )
			continue;
		// TODO: A process that is a member of an orphaned process group shall
		//       not be allowed to stop in response to the SIGTSTP, SIGTTIN, or
		//       SIGTTOU signals. In cases where delivery of one of these
		//       signals would stop such a process, the signal shall be
		//       discarded.
		if ( /* is member of an orphaned process group */ false &&
		     signal_actions[i].sa_handler == SIG_DFL &&
		     sigismember(&default_stop_signals, i) )
			continue;
		sigaddset(&handled_signals, i);
	}

	// TODO: Handle that signals can be pending process-wide!

	// Discard all requested signals that would be ignored if delivered.
	sigandset(&thread->signal_pending, &thread->signal_pending, &handled_signals);

	// Determine which signals are not blocked.
	sigset_t permitted_signals;
	signotset(&permitted_signals, &thread->signal_mask);
	sigorset(&permitted_signals, &permitted_signals, &unblockable_signals);

	// Determine which signals can currently be delivered to this thread.
	sigset_t deliverable_signals;
	sigandset(&deliverable_signals, &permitted_signals, &thread->signal_pending);

	// Determine whether any signals can be delivered.
	unsigned long is_pending = !sigisemptyset(&deliverable_signals) ? 1 : 0;
	if ( thread->force_no_signals )
		is_pending = 0;

	// Store whether a signal is pending in the virtual register.
	if ( thread == CurrentThread() )
		asm_signal_is_pending = is_pending;
	else
		thread->registers.signal_pending = is_pending;
}

void Thread::DoUpdatePendingSignal()
{
	ScopedLock lock(&process->signal_lock);
	UpdatePendingSignals(this);
}

int sys_sigaction(int signum,
                  const struct sigaction* user_newact,
                  struct sigaction* user_oldact)
{
	if ( signum < 0 || signum == 0 /* null signal */ || SIG_MAX_NUM <= signum )
		return errno = EINVAL;

	Process* process = CurrentProcess();
	ScopedLock lock(&process->signal_lock);

	struct sigaction* kact = &process->signal_actions[signum];

	// Let the caller know the previous action.
	if ( user_oldact )
	{
		if ( !CopyToUser(user_oldact, kact, sizeof(struct sigaction)) )
			return -1;
	}

	// Retrieve and validate the new signal action.
	if ( user_newact )
	{
		struct sigaction newact;
		if ( !CopyFromUser(&newact, user_newact, sizeof(struct sigaction)) )
			return -1;

		if ( newact.sa_flags & ~__SA_SUPPORTED_FLAGS )
			return errno = EINVAL, -1;

		if ( newact.sa_handler == SIG_ERR )
			return errno = EINVAL, -1;

		memcpy(kact, &newact, sizeof(struct sigaction));

		// Signals may become discarded because of the new handler.
		ScopedLock threads_lock(&process->threadlock);
		for ( Thread* t = process->firstthread; t; t = t->nextsibling )
			UpdatePendingSignals(t);
	}

	return 0;
}

int sys_sigaltstack(const stack_t* user_newstack, stack_t* user_oldstack)
{
	Thread* thread = CurrentThread();

	if ( user_oldstack )
	{
		if ( !CopyToUser(user_oldstack, &thread->signal_stack, sizeof(stack_t)) )
			return -1;
	}

	if ( user_newstack )
	{
		stack_t newstack;
		if ( !CopyFromUser(&newstack, user_newstack, sizeof(stack_t)) )
			return -1;

		if ( newstack.ss_flags & ~__SS_SUPPORTED_FLAGS )
			return errno = EINVAL, -1;

		memcpy(&thread->signal_stack, &newstack, sizeof(stack_t));
	}

	return 0;
}

int sys_sigpending(sigset_t* set)
{
	Process* process = CurrentProcess();
	Thread* thread = CurrentThread();

	ScopedLock lock(&process->signal_lock);

	// TODO: What about process-wide signals?

	return CopyToUser(set, &thread->signal_pending, sizeof(sigset_t)) ? 0 : -1;
}

int sys_sigprocmask(int how, const sigset_t* user_set, sigset_t* user_oldset)
{
	Process* process = CurrentProcess();
	Thread* thread = CurrentThread();

	// TODO: Signal masks are a per-thread property, perhaps this should be
	//       locked in another manner?
	ScopedLock lock(&process->signal_lock);

	// Let the caller know the previous signal mask.
	if ( user_oldset )
	{
		if ( !CopyToUser(user_oldset, &thread->signal_mask, sizeof(sigset_t)) )
			return -1;
	}

	// Update the current signal mask according to how.
	if ( user_set )
	{
		sigset_t set;
		if ( !CopyFromUser(&set, user_set, sizeof(sigset_t)) )
			return -1;

		switch ( how )
		{
		case SIG_BLOCK:
			sigorset(&thread->signal_mask, &thread->signal_mask, &set);
			break;
		case SIG_UNBLOCK:
			signotset(&set, &set);
			sigandset(&thread->signal_mask, &thread->signal_mask, &set);
			break;
		case SIG_SETMASK:
			memcpy(&thread->signal_mask, &set, sizeof(sigset_t));
			break;
		default:
			return errno = EINVAL, -1;
		};

		UpdatePendingSignals(thread);
	}

	return 0;
}

int sys_sigsuspend(const sigset_t* set)
{
	Process* process = CurrentProcess();
	Thread* thread = CurrentThread();

	sigset_t old_signal_mask; sigemptyset(&old_signal_mask);
	sigset_t new_signal_mask;

	ScopedLock lock(&process->signal_lock);

	// Only accept signals from the user-provided set if given.
	if ( set )
	{
		if ( !CopyFromUser(&new_signal_mask, set, sizeof(sigset_t)) )
			return -1;
		memcpy(&old_signal_mask, &thread->signal_mask, sizeof(sigset_t));
		memcpy(&thread->signal_mask, &new_signal_mask, sizeof(sigset_t));
		UpdatePendingSignals(thread);
	}

	// Wait for a signal to happen or otherwise never halt.
	kthread_cond_t never_triggered = KTHREAD_COND_INITIALIZER;
	while ( !Signal::IsPending() )
		kthread_cond_wait_signal(&never_triggered, &process->signal_lock);

	// Restore the previous signal mask if the user gave its own set to wait on.
	if ( set )
	{
		memcpy(&thread->signal_mask, &old_signal_mask, sizeof(sigset_t));
		UpdatePendingSignals(thread);
	}

	// The system call never halts or it halts because a signal interrupted it.
	return errno = EINTR, -1;
}

int sys_kill(pid_t pid, int signum)
{
	// Protect the kernel process.
	if ( !pid )
		return errno = EPERM, -1;

	// TODO: Implement that pid == -1 means all processes!
	bool process_group = pid < 0 ? (pid = -pid, true) : false;

	// TODO: Race condition: The process could be deleted while we use it.
	Process* process = CurrentProcess()->GetPTable()->Get(pid);
	if ( !process )
		return errno = ESRCH, -1;

	// TODO: Protect init?
	// TODO: Check for permission.
	// TODO: Check for zombies.

	if ( process_group )
	{
		if ( !process->DeliverGroupSignal(signum) && errno != ESIGPENDING )
			return -1;
		return errno = 0, 0;
	}

	if ( !process->DeliverSignal(signum) && errno != ESIGPENDING )
		return -1;
	return errno = 0, 0;
}

bool Process::DeliverGroupSignal(int signum)
{
	ScopedLock lock(&groupparentlock);
	if ( !groupfirst )
		return errno = ESRCH, false;
	for ( Process* iter = groupfirst; iter; iter = iter->groupnext )
	{
		int saved_errno = errno;
		if ( !iter->DeliverSignal(signum) && errno != ESIGPENDING )
		{
			// This is not currently an error condition.
		}
		errno = saved_errno;
	}
	return true;
}

bool Process::DeliverSignal(int signum)
{
	ScopedLock lock(&threadlock);

	if ( !firstthread )
		return errno = EINIT, false;

	// Broadcast particular signals to all the threads in the process.
	if ( signum == SIGCONT || signum == SIGSTOP || signum == SIGKILL )
	{
		int saved_errno = errno;
		for ( Thread* t = firstthread; t; t = t->nextsibling )
		{
			if ( !t->DeliverSignal(signum) && errno != ESIGPENDING )
			{
				// This is not currently an error condition.
			}
		}
		errno = saved_errno;
		return true;
	}

	// Route the signal to a suitable thread that accepts it.
	// TODO: This isn't how signals should be routed to a particular thread.
	if ( CurrentThread()->process == this )
		return CurrentThread()->DeliverSignal(signum);
	return firstthread->DeliverSignal(signum);
}

int sys_raise(int signum)
{
	if ( !CurrentThread()->DeliverSignal(signum) && errno != ESIGPENDING )
		return -1;
	return errno = 0, 0;
}

bool Thread::DeliverSignal(int signum)
{
	ScopedLock lock(&process->signal_lock);
	return DeliverSignalUnlocked(signum);
}

bool Thread::DeliverSignalUnlocked(int signum) // thread->process->signal_lock held
{
	if ( signum <= 0 || SIG_MAX_NUM <= signum )
		return errno = EINVAL, false;

	// Discard the null signal, which does error checking, but doesn't actually
	// deliver a signal to the process or thread.
	if ( signum == 0 )
		return true;

	if ( sigismember(&signal_pending, signum) )
		return errno = ESIGPENDING, false;

	sigaddset(&signal_pending, signum);
	if ( signum == SIGSTOP || signum == SIGTSTP ||
	     signum == SIGTTIN || signum == SIGTTOU )
		sigdelset(&signal_pending, SIGCONT);
	if ( signum == SIGCONT )
	{
		sigdelset(&signal_pending, SIGSTOP);
		sigdelset(&signal_pending, SIGTSTP);
		sigdelset(&signal_pending, SIGTTIN);
		sigdelset(&signal_pending, SIGTTOU);
	}
	UpdatePendingSignals(this);

	return true;
}

static int PickImportantSignal(const sigset_t* set)
{
	if ( sigismember(set, SIGKILL) )
		return SIGKILL;
	if ( sigismember(set, SIGSTOP) )
		return SIGSTOP;
	for ( int i = 1; i < SIG_MAX_NUM; i++ )
		if ( sigismember(set, i) )
			return i;
	return 0;
}

static void EncodeMachineContext(mcontext_t* mctx,
                                 const struct thread_registers* regs,
                                 const struct interrupt_context* intctx)
{
	memset(mctx, 0, sizeof(*mctx));
#if defined(__i386__)
	// TODO: REG_GS
	// TODO: REG_FS
	// TODO: REG_ES
	// TODO: REG_DS
	mctx->gregs[REG_EDI] = regs->edi;
	mctx->gregs[REG_ESI] = regs->esi;
	mctx->gregs[REG_EBP] = regs->ebp;
	mctx->gregs[REG_ESP] = regs->esp;
	mctx->gregs[REG_EBX] = regs->ebx;
	mctx->gregs[REG_EDX] = regs->edx;
	mctx->gregs[REG_ECX] = regs->ecx;
	mctx->gregs[REG_EAX] = regs->eax;
	mctx->gregs[REG_EIP] = regs->eip;
	// TODO: REG_CS
	mctx->gregs[REG_EFL] = regs->eflags & 0x0000FFFF;
	mctx->gregs[REG_CR2] = intctx->cr2;
	// TODO: REG_SS
	memcpy(mctx->fpuenv, regs->fpuenv, 512);
#elif defined(__x86_64__)
	mctx->gregs[REG_R8] = regs->r8;
	mctx->gregs[REG_R9] = regs->r9;
	mctx->gregs[REG_R10] = regs->r10;
	mctx->gregs[REG_R11] = regs->r11;
	mctx->gregs[REG_R12] = regs->r12;
	mctx->gregs[REG_R13] = regs->r13;
	mctx->gregs[REG_R14] = regs->r14;
	mctx->gregs[REG_R15] = regs->r15;
	mctx->gregs[REG_RDI] = regs->rdi;
	mctx->gregs[REG_RSI] = regs->rsi;
	mctx->gregs[REG_RBP] = regs->rbp;
	mctx->gregs[REG_RBX] = regs->rbx;
	mctx->gregs[REG_RDX] = regs->rdx;
	mctx->gregs[REG_RAX] = regs->rax;
	mctx->gregs[REG_RCX] = regs->rcx;
	mctx->gregs[REG_RSP] = regs->rsp;
	mctx->gregs[REG_RIP] = regs->rip;
	mctx->gregs[REG_EFL] = regs->rflags & 0x000000000000FFFF;
	// TODO: REG_CSGSFS.
	mctx->gregs[REG_CR2] = intctx->cr2;
	mctx->gregs[REG_FSBASE] = 0x0;
	mctx->gregs[REG_GSBASE] = 0x0;
	memcpy(mctx->fpuenv, regs->fpuenv, 512);
#else
#error "You need to implement conversion to mcontext"
#endif
}

static void DecodeMachineContext(const mcontext_t* mctx,
                                 struct thread_registers* regs)
{
#if defined(__i386__) || defined(__x86_64__)
	unsigned long user_flags = FLAGS_CARRY | FLAGS_PARITY | FLAGS_AUX
	                         | FLAGS_ZERO | FLAGS_SIGN | FLAGS_DIRECTION
	                         | FLAGS_OVERFLOW;
#endif
#if defined(__i386__)
	regs->edi = mctx->gregs[REG_EDI];
	regs->esi = mctx->gregs[REG_ESI];
	regs->ebp = mctx->gregs[REG_EBP];
	regs->esp = mctx->gregs[REG_ESP];
	regs->ebx = mctx->gregs[REG_EBX];
	regs->edx = mctx->gregs[REG_EDX];
	regs->ecx = mctx->gregs[REG_ECX];
	regs->eax = mctx->gregs[REG_EAX];
	regs->eip = mctx->gregs[REG_EIP];
	regs->eflags &= ~user_flags;
	regs->eflags |= mctx->gregs[REG_EFL] & user_flags;
	memcpy(regs->fpuenv, mctx->fpuenv, 512);
#elif defined(__x86_64__)
	regs->r8 = mctx->gregs[REG_R8];
	regs->r9 = mctx->gregs[REG_R9];
	regs->r10 = mctx->gregs[REG_R10];
	regs->r11 = mctx->gregs[REG_R11];
	regs->r12 = mctx->gregs[REG_R12];
	regs->r13 = mctx->gregs[REG_R13];
	regs->r14 = mctx->gregs[REG_R14];
	regs->r15 = mctx->gregs[REG_R15];
	regs->rdi = mctx->gregs[REG_RDI];
	regs->rsi = mctx->gregs[REG_RSI];
	regs->rbp = mctx->gregs[REG_RBP];
	regs->rbx = mctx->gregs[REG_RBX];
	regs->rdx = mctx->gregs[REG_RDX];
	regs->rax = mctx->gregs[REG_RAX];
	regs->rcx = mctx->gregs[REG_RCX];
	regs->rsp = mctx->gregs[REG_RSP];
	regs->rip = mctx->gregs[REG_RIP];
	regs->rflags &= ~user_flags;
	regs->rflags |= mctx->gregs[REG_EFL] & user_flags;
	memcpy(regs->fpuenv, mctx->fpuenv, 512);
#else
#error "You need to implement conversion to mcontext"
#endif
}

#if defined(__i386__)
struct stack_frame
{
	uintptr_t misalignment[3];
	uintptr_t sigreturn;
	int signum_param;
	siginfo_t* siginfo_param;
	ucontext_t* ucontext_param;
	void* cookie_param;
	uintptr_t canary;
	ucontext_t ucontext;
	siginfo_t siginfo;
};
#elif defined(__x86_64__)
struct stack_frame
{
	uintptr_t misalignment[1];
	uintptr_t sigreturn;
	uintptr_t canary;
	ucontext_t ucontext;
	siginfo_t siginfo;
};
#else
#error "You need to implement struct stack_frame"
#endif

void Thread::HandleSignal(struct interrupt_context* intctx)
{
	assert(Interrupt::IsEnabled());
	assert(this == CurrentThread());

	ScopedLock lock(&process->signal_lock);

	assert(process->sigreturn);

retry_another_signal:

	// Determine which signals are not blocked.
	sigset_t permitted_signals;
	signotset(&permitted_signals, &signal_mask);
	sigorset(&permitted_signals, &permitted_signals, &unblockable_signals);

	// Determine which signals can currently be delivered to this thread.
	sigset_t deliverable_signals;
	sigandset(&deliverable_signals, &permitted_signals, &signal_pending);

	// Decide which signal to deliver to the thread.
	int signum = PickImportantSignal(&deliverable_signals);
	if ( !signum )
		return;

	// Unmark the selected signal as pending.
	sigdelset(&signal_pending, signum);
	UpdatePendingSignals(this);
	intctx->signal_pending = asm_signal_is_pending;

	// Destroy the current thread if the signal is critical.
	if ( signum == SIGKILL )
	{
		lock.Reset();
		kthread_exit();
	}

	struct sigaction* action = &process->signal_actions[signum];

	// Stop the current thread upon receipt of a stop signal that isn't handled
	// or cannot be handled (SIGSTOP).
	if ( (action->sa_handler == SIG_DFL &&
	      sigismember(&default_stop_signals, signum) ) ||
	     signum == SIGSTOP )
	{
		Log::PrintF("%s:%u: `%s' FIXME SIGSTOP\n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
		// TODO: Stop the current process.
		// TODO: Deliver SIGCHLD to the parent except if SA_NOCLDSTOP is set in
		//       the parent's SIGCHLD sigaction.
		// TODO: SIGCHLD should not be delivered until all the threads in the
		//       process has received SIGSTOP and stopped?
		// TODO: SIGKILL must still be deliverable to a stopped process.
	}

	// Resume the current thread upon receipt of SIGCONT.
	if ( signum == SIGCONT )
	{
		Log::PrintF("%s:%u: `%s' FIXME SIGCONT\n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
		// TODO: Resume the current process.
		// TODO: Can SIGCONT be masked?
		// TODO: Can SIGCONT be handled?
		// TODO: Can SIGCONT be ignored?
		// TODO: Deliver SIGCHLD to the parent except if SA_NOCLDSTOP is set in
		//       the parent's SIGCHLD sigaction.
	}

	// Signals that would be ignored are already filtered away at this point.
	assert(action->sa_handler != SIG_IGN);
	assert(action->sa_handler != SIG_DFL || !sigismember(&default_ignored_signals, signum));

	// The default action must be to terminate the process. Signals that are
	// ignored by default got discarded earlier.
	if ( action->sa_handler == SIG_DFL )
	{
		kthread_mutex_unlock(&process->signal_lock);
		process->ExitThroughSignal(signum);
		kthread_mutex_lock(&process->signal_lock);
		goto retry_another_signal;
	}

	// At this point we have to attempt to invoke the user-space signal handler,
	// which will then return control to us through sigreturn. However, we can't
	// save the kernel state because 1) we can't trust the user-space stack 2)
	// we can't rely on the kernel stack being intact as the signal handler may
	// invoke system calls. For those reasons, we'll have to modify the saved
	// registers so they restore a user-space state. We can do this because
	// threads in the kernel cannot be delivered signals except when returning
	// from a system call, so we'll simply save the state that would have been
	// returned to user-space had no signal occured.
	if ( !InUserspace(intctx) )
	{
#if defined(__i386__)
		uint32_t* params = (uint32_t*) intctx->ebx;
		intctx->eip = params[0];
		intctx->eflags = params[2];
		intctx->esp = params[3];
		intctx->cs = UCS | URPL;
		intctx->ds = UDS | URPL;
		intctx->ss = UDS | URPL;
		intctx->ebx = 0;
#elif defined(__x86_64__)
		intctx->rip = intctx->rdi;
		intctx->rflags = intctx->rsi;
		intctx->rsp = intctx->r8;
		intctx->cs = UCS | URPL;
		intctx->ds = UDS | URPL;
		intctx->ss = UDS | URPL;
		intctx->rdi = 0;
		intctx->rsi = 0;
		intctx->r8 = 0;
#else
#error "You may need to fix the registers"
#endif
	}

	struct thread_registers stopped_regs;
	Scheduler::SaveInterruptedContext(intctx, &stopped_regs);

	sigset_t new_signal_mask;
	memcpy(&new_signal_mask, &action->sa_mask, sizeof(sigset_t));
	sigorset(&new_signal_mask, &new_signal_mask, &signal_mask);

	// Prevent signals from interrupting themselves by default.
	if ( !(action->sa_flags & SA_NODEFER) )
		sigaddset(&new_signal_mask, signum);

	// Determine whether we use an alternate signal stack.
	bool signal_uses_altstack = action->sa_flags & SA_ONSTACK;
	bool usable_altstack = !(signal_stack.ss_flags & (SS_DISABLE | SS_ONSTACK));
	bool use_altstack = signal_uses_altstack && usable_altstack;

	// Determine which signal stack to use and what to save.
	stack_t old_signal_stack, new_signal_stack;
	uintptr_t stack_location;
	if ( use_altstack )
	{
		old_signal_stack = signal_stack;
		new_signal_stack = signal_stack;
		new_signal_stack.ss_flags |=  SS_ONSTACK;
#if defined(__i386__) || defined(__x86_64__)
		stack_location = (uintptr_t) signal_stack.ss_sp + signal_stack.ss_size;
#else
#error "You need to implement getting the alternate stack pointer"
#endif
	}
	else
	{
		old_signal_stack.ss_sp = NULL;
		old_signal_stack.ss_flags = SS_DISABLE;
		old_signal_stack.ss_size = 0;
		new_signal_stack = signal_stack;
#if defined(__i386__)
		stack_location = (uintptr_t) stopped_regs.esp;
#elif defined(__x86_64__)
		stack_location = (uintptr_t) stopped_regs.rsp;
#else
#error "You need to implement getting the user-space stack pointer"
#endif
	}

	struct thread_registers handler_regs;
	memcpy(&handler_regs, &stopped_regs, sizeof(handler_regs));

	struct stack_frame stack_frame;
	memset(&stack_frame, 0, sizeof(stack_frame));

	void* handler_ptr = action->sa_flags & SA_COOKIE ?
	                    (void*) action->sa_sigaction_cookie :
	                    action->sa_flags & SA_SIGINFO ?
	                    (void*) action->sa_sigaction :
	                    (void*) action->sa_handler;

#if defined(__i386__)
	stack_location -= sizeof(stack_frame);
	stack_location &= ~(16UL-1UL); /* 16-byte align */
	struct stack_frame* stack = (struct stack_frame*) stack_location;

	stack_frame.sigreturn = (uintptr_t) process->sigreturn;
	stack_frame.signum_param = signum;
	stack_frame.siginfo_param = &stack->siginfo;
	stack_frame.ucontext_param = &stack->ucontext;
	stack_frame.cookie_param = action->sa_cookie;

	handler_regs.esp = (unsigned long) stack + sizeof(stack->misalignment);
	handler_regs.eip = (unsigned long) handler_ptr;
	handler_regs.eflags &= ~FLAGS_DIRECTION;
#elif defined(__x86_64__)
	stack_location -= 128; /* Red zone. */
	stack_location -= sizeof(stack_frame);
	stack_location &= ~(16UL-1UL); /* 16-byte align */
	struct stack_frame* stack = (struct stack_frame*) stack_location;

	stack_frame.sigreturn = (uintptr_t) process->sigreturn;
	handler_regs.rdi = (unsigned long) signum;
	handler_regs.rsi = (unsigned long) &stack->siginfo;
	handler_regs.rdx = (unsigned long) &stack->ucontext;
	handler_regs.rcx = (unsigned long) action->sa_cookie;

	handler_regs.rsp = (unsigned long) stack + sizeof(stack->misalignment);
	handler_regs.rip = (unsigned long) handler_ptr;
	handler_regs.rflags &= ~FLAGS_DIRECTION;
#else
#error "You need to format the stack frame"
#endif

	// Store a canary so it can later be verified this is a real signal being
	// returned from.
	if ( signal_count == 0 )
		arc4random_buf(&signal_canary, sizeof(signal_canary));
	stack_frame.canary = signal_canary ^ (uintptr_t) stack;

	// Format the siginfo into the stack frame.
	stack_frame.siginfo.si_signo = signum;
#if defined(__i386__) || defined(__x86_64__)
	// TODO: Is this cr2 value trustworthy? I don't think it is.
	if ( signum == SIGSEGV )
		stack_frame.siginfo.si_addr = (void*) intctx->cr2;
#else
#warning "You need to tell user-space where it crashed"
#endif

	// Format the ucontext into the stack frame.
	stack_frame.ucontext.uc_link = NULL;
	memcpy(&stack_frame.ucontext.uc_sigmask, &signal_mask, sizeof(signal_mask));
	memcpy(&stack_frame.ucontext.uc_stack, &signal_stack, sizeof(signal_stack));
	EncodeMachineContext(&stack_frame.ucontext.uc_mcontext, &stopped_regs, intctx);

	if ( !CopyToUser(stack, &stack_frame, sizeof(stack_frame)) )
	{
		// Self-destruct if we crashed during delivering the crash signal.
		if ( signum == SIGSEGV )
		{
			kthread_mutex_unlock(&process->signal_lock);
			process->ExitThroughSignal(signum);
			kthread_mutex_lock(&process->signal_lock);
			goto retry_another_signal;
		}

		// Deliver SIGSEGV if we could not deliver the signal on the stack.
		// TODO: Is it possible to block SIGSEGV here?
		kthread_mutex_unlock(&process->signal_lock);
		DeliverSignal(SIGSEGV);
		kthread_mutex_lock(&process->signal_lock);
		goto retry_another_signal;
	}

	// Update the current signal mask.
	memcpy(&signal_mask, &new_signal_mask, sizeof(sigset_t));

	// Update the current alternate signal stack.
	signal_stack = new_signal_stack;

	// Update the current registers.
	Scheduler::LoadInterruptedContext(intctx, &handler_regs);

	// Reset the signal handler if this signal handler is once only.
	if ( action->sa_flags & SA_RESETHAND )
	{
		// POSIX mandates SA_RESETHAND is silently ignored for these signals.
		if ( signum != SIGILL && signum != SIGTRAP )
		{
			action->sa_flags &= ~SA_RESETHAND;
			action->sa_handler = SIG_DFL;
		}
	}

	// Know for sure when there's no signal still being handled. This is an
	// over-approximation as programs may do absolutely awful things such as
	// longjmp(3)ing out of signal handlers, so each delivered signal does not
	// nessesarily mean a sigreturn. This will work correctly in reasonable
	// programs though and will harden those programs. Remember the stack frame
	// as well for verification if there is no recursive signal handling.
	if ( signal_count != SIZE_MAX )
		signal_count++;
	if ( (signal_single = signal_count == 1) )
		signal_single_frame = (uintptr_t) stack;

	// Run the signal handler by returning to user-space.
	return;
}

void Thread::HandleSigreturn(struct interrupt_context* intctx)
{
	assert(Interrupt::IsEnabled());
	assert(this == CurrentThread());

	struct stack_frame stack_frame;
	struct stack_frame* user_stack_frame;
#if defined(__i386__)
	user_stack_frame = (struct stack_frame*)
		(intctx->esp - offsetof(struct stack_frame, sigreturn) - 4);
	bool wrong_ip = intctx->eip != (uintptr_t) process->sigreturn + 2;
#elif defined(__x86_64__)
	user_stack_frame = (struct stack_frame*)
		(intctx->rsp - offsetof(struct stack_frame, sigreturn) - 8);
	bool wrong_ip = intctx->rip != (uintptr_t) process->sigreturn + 2;
#else
#error "You need to locate the stack we passed the signal handler"
#endif

	// Protect against sigreturn oriented programming (SROP) as described in
	// "Framing Signals - A Return to Portable Shellcode" (Bosman and Bos 2014).

	// If the we're not called from the kernel sigreturn page, it is not a valid
	// sigreturn.
	if ( wrong_ip )
	{
		process->ExitThroughSignal(SIGABRT);
		Log::PrintF("%s[%ji]: sigreturn smashing detected: Bypassed kernel sigreturn page\n",
			process->program_image_path,
			(intmax_t) process->pid);
		// TODO: Allow debugging this event (see scram(2)).
		kthread_exit();
	}

	// If no signals are being serviced by this thread at the moment, it is not
	// a valid sigreturn.
	if ( signal_count == 0 )
	{
		process->ExitThroughSignal(SIGABRT);
		Log::PrintF("%s[%ji]: sigreturn smashing detected: Thread wasn't servicing a signal\n",
			process->program_image_path,
			(intmax_t) process->pid);
		// TODO: Allow debugging this event (see scram(2)).
		kthread_exit();
	}

	// If a single signal is being serviced by this thread at the moment, the
	// stack pointer must be what we expect it to be. If there's multiple, we
	// don't know which one is the correct. (We could keep track of them and
	// ensure it's one of them, but that's not really worth it. The list of such
	// delivered signals could grow without bound because it's valid to longjmp
	// out of a signal handler)
	if ( signal_single && (uintptr_t) user_stack_frame != signal_single_frame )
	{
		process->ExitThroughSignal(SIGABRT);
		Log::PrintF("%s[%ji]: sigreturn smashing detected: Stack pointer was wrong\n",
			process->program_image_path,
			(intmax_t) process->pid);
		// TODO: Allow debugging this event (see scram(2)).
		kthread_exit();
	}

	// If we couldn't read the frame, the sigreturn is certainly bad.
	if ( !CopyFromUser(&stack_frame, user_stack_frame, sizeof(stack_frame)) )
	{
		process->ExitThroughSignal(SIGABRT);
		Log::PrintF("%s[%ji]: sigreturn smashing detected: Couldn't read stack frame: %m\n",
			process->program_image_path,
			(intmax_t) process->pid);
		// TODO: Allow debugging this event (see scram(2)).
		kthread_exit();
	}
	ZeroUser(user_stack_frame, sizeof(*user_stack_frame));

	// If the random canary isn't correct, the sigreturn is certianly bad.
	if ( stack_frame.canary != (signal_canary ^ (uintptr_t) user_stack_frame) )
	{
		process->ExitThroughSignal(SIGABRT);
		Log::PrintF("%s[%ji]: sigreturn smashing detected: Verification value was incorrect\n",
			process->program_image_path,
			(intmax_t) process->pid);
		// TODO: Allow debugging this event (see scram(2)).
		kthread_exit();
	}

	ScopedLock lock(&process->signal_lock);

	memcpy(&signal_mask, &stack_frame.ucontext.uc_sigmask, sizeof(signal_mask));
	memcpy(&signal_stack, &stack_frame.ucontext.uc_stack, sizeof(signal_stack));
	signal_stack.ss_flags &= __SS_SUPPORTED_FLAGS;
	struct thread_registers resume_regs;
	Scheduler::SaveInterruptedContext(intctx, &resume_regs);
	DecodeMachineContext(&stack_frame.ucontext.uc_mcontext, &resume_regs);
	Scheduler::LoadInterruptedContext(intctx, &resume_regs);

	if ( signal_count != SIZE_MAX )
		signal_count--;
	signal_single = false;

	UpdatePendingSignals(this);
	intctx->signal_pending = asm_signal_is_pending;

	lock.Reset();

	assert(Interrupt::IsEnabled());
	HandleSignal(intctx);
}

namespace Signal {

void DispatchHandler(struct interrupt_context* intctx, void* /*user*/)
{
	assert(Interrupt::IsEnabled());
	return CurrentThread()->HandleSignal(intctx);
}

void ReturnHandler(struct interrupt_context* intctx, void* /*user*/)
{
	assert(Interrupt::IsEnabled());
	return CurrentThread()->HandleSigreturn(intctx);
}

void Init()
{
	sigemptyset(&default_ignored_signals);
	sigaddset(&default_ignored_signals, SIGCHLD);
	sigaddset(&default_ignored_signals, SIGURG);
	sigaddset(&default_ignored_signals, SIGPWR);
	sigaddset(&default_ignored_signals, SIGWINCH);
	sigemptyset(&default_stop_signals);
	sigaddset(&default_stop_signals, SIGTSTP);
	sigaddset(&default_stop_signals, SIGTTIN);
	sigaddset(&default_stop_signals, SIGTTOU);
	sigemptyset(&unblockable_signals);
	sigaddset(&unblockable_signals, SIGKILL);
	sigaddset(&unblockable_signals, SIGSTOP);
}

} // namespace Signal

} // namespace Sortix
