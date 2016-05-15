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
 * process.cpp
 * A named collection of threads.
 */

#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <msr.h>
#include <scram.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sortix/clock.h>
#include <sortix/fcntl.h>
#include <sortix/fork.h>
#include <sortix/mman.h>
#include <sortix/resource.h>
#include <sortix/signal.h>
#include <sortix/stat.h>
#include <sortix/unistd.h>
#include <sortix/uthread.h>
#include <sortix/wait.h>

#include <sortix/kernel/addralloc.h>
#include <sortix/kernel/copy.h>
#include <sortix/kernel/descriptor.h>
#include <sortix/kernel/dtable.h>
#include <sortix/kernel/elf.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/kernel.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/memorymanagement.h>
#include <sortix/kernel/mtable.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/ptable.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/scheduler.h>
#include <sortix/kernel/sortedlist.h>
#include <sortix/kernel/string.h>
#include <sortix/kernel/syscall.h>
#include <sortix/kernel/thread.h>
#include <sortix/kernel/time.h>
#include <sortix/kernel/worker.h>

#if defined(__i386__) || defined(__x86_64__)
#include "x86-family/float.h"
#include "x86-family/gdt.h"
#endif

namespace Sortix {

Process::Process()
{
	program_image_path = NULL;
	addrspace = 0;
	pid = 0;

	nicelock = KTHREAD_MUTEX_INITIALIZER;
	nice = 0;

	idlock = KTHREAD_MUTEX_INITIALIZER;
	uid = euid = 0;
	gid = egid = 0;
	umask = 0022;

	ptrlock = KTHREAD_MUTEX_INITIALIZER;
	// root set to null reference in the member constructor.
	// cwd set to null reference in the member constructor.
	// mtable set to null reference in the member constructor.
	// dtable set to null reference in the member constructor.

	// ptable set to null reference in the member constructor.

	resource_limits_lock = KTHREAD_MUTEX_INITIALIZER;
	for ( size_t i = 0; i < RLIMIT_NUM_DECLARED; i++ )
	{
		resource_limits[i].rlim_cur = RLIM_INFINITY;
		resource_limits[i].rlim_max = RLIM_INFINITY;
	}

	signal_lock = KTHREAD_MUTEX_INITIALIZER;
	memset(&signal_actions, 0, sizeof(signal_actions));
	for ( int i = 0; i < SIG_MAX_NUM; i++ )
	{
		sigemptyset(&signal_actions[i].sa_mask);
		signal_actions[i].sa_handler = SIG_DFL;
		signal_actions[i].sa_cookie = NULL;
		signal_actions[i].sa_flags = 0;
	}
	sigemptyset(&signal_pending);
	sigreturn = NULL;

	parent = NULL;
	prevsibling = NULL;
	nextsibling = NULL;
	firstchild = NULL;
	zombiechild = NULL;
	childlock = KTHREAD_MUTEX_INITIALIZER;
	parentlock = KTHREAD_MUTEX_INITIALIZER;
	zombiecond = KTHREAD_COND_INITIALIZER;
	iszombie = false;
	nozombify = false;
	exit_code = -1;

	group = NULL;
	groupprev = NULL;
	groupnext = NULL;
	groupfirst = NULL;

	groupparentlock = KTHREAD_MUTEX_INITIALIZER;
	groupchildlock = KTHREAD_MUTEX_INITIALIZER;
	groupchildleft = KTHREAD_COND_INITIALIZER;
	grouplimbo = false;

	firstthread = NULL;
	threadlock = KTHREAD_MUTEX_INITIALIZER;
	threads_exiting = false;

	segments = NULL;
	segments_used = 0;
	segments_length = 0;
	segment_write_lock = KTHREAD_MUTEX_INITIALIZER;
	segment_lock = KTHREAD_MUTEX_INITIALIZER;

	user_timers_lock = KTHREAD_MUTEX_INITIALIZER;
	memset(&user_timers, 0, sizeof(user_timers));
	// alarm_timer initialized in member constructor.
	// execute_clock initialized in member constructor.
	// system_clock initialized in member constructor.
	// child_execute_clock initialized in member constructor.
	// child_system_clock initialized in member constructor.
	Time::InitializeProcessClocks(this);
	alarm_timer.Attach(Time::GetClock(CLOCK_MONOTONIC));
}

Process::~Process()
{
	if ( alarm_timer.IsAttached() )
		alarm_timer.Detach();
	delete[] program_image_path;
	assert(!zombiechild);
	assert(!firstchild);
	assert(!addrspace);
	assert(!segments);
	assert(!dtable);
	assert(!mtable);
	assert(!cwd);
	assert(!root);

	assert(ptable);
	ptable->Free(pid);
	ptable.Reset();
}

void Process::BootstrapTables(Ref<DescriptorTable> dtable, Ref<MountTable> mtable)
{
	ScopedLock lock(&ptrlock);
	assert(!this->dtable);
	assert(!this->mtable);
	this->dtable = dtable;
	this->mtable = mtable;
}

void Process::BootstrapDirectories(Ref<Descriptor> root)
{
	ScopedLock lock(&ptrlock);
	assert(!this->root);
	assert(!this->cwd);
	this->root = root;
	this->cwd = root;
}

void Process__OnLastThreadExit(void* user);

void Process::OnThreadDestruction(Thread* thread)
{
	assert(thread->process == this);
	kthread_mutex_lock(&threadlock);
	if ( thread->prevsibling )
		thread->prevsibling->nextsibling = thread->nextsibling;
	if ( thread->nextsibling )
		thread->nextsibling->prevsibling = thread->prevsibling;
	if ( thread == firstthread )
		firstthread = thread->nextsibling;
	if ( firstthread )
		firstthread->prevsibling = NULL;
	thread->prevsibling = thread->nextsibling = NULL;
	bool threadsleft = firstthread;
	kthread_mutex_unlock(&threadlock);

	// We are called from the threads destructor, let it finish before we
	// we handle the situation by killing ourselves.
	if ( !threadsleft )
		ScheduleDeath();
}

void Process::ScheduleDeath()
{
	// All our threads must have exited at this point.
	assert(!firstthread);
	Worker::Schedule(Process__OnLastThreadExit, this);
}

// Useful for killing a partially constructed process without waiting for
// it to die and garbage collect its zombie. It is not safe to access this
// process after this call as another thread may garbage collect it.
void Process::AbortConstruction()
{
	nozombify = true;
	ScheduleDeath();
}

void Process__OnLastThreadExit(void* user)
{
	return ((Process*) user)->OnLastThreadExit();
}

void Process::OnLastThreadExit()
{
	LastPrayer();
}

void Process::DeleteTimers()
{
	for ( timer_t i = 0; i < PROCESS_TIMER_NUM_MAX; i++ )
	{
		if ( user_timers[i].timer.IsAttached() )
		{
			user_timers[i].timer.Cancel();
			user_timers[i].timer.Detach();
		}
	}
}

void Process::LastPrayer()
{
	assert(this);
	// This must never be called twice.
	assert(!iszombie);

	// This must be called from a thread using another address space as the
	// address space of this process is about to be destroyed.
	Thread* curthread = CurrentThread();
	assert(curthread->process != this);

	// This can't be called if the process is still alive.
	assert(!firstthread);

	// Disarm and detach all the timers in the process.
	DeleteTimers();
	if ( alarm_timer.IsAttached() )
	{
		alarm_timer.Cancel();
		alarm_timer.Detach();
	}

	// We need to temporarily reload the correct addrese space of the dying
	// process such that we can unmap and free its memory.
	addr_t prevaddrspace = Memory::SwitchAddressSpace(addrspace);

	ResetAddressSpace();

	if ( dtable ) dtable.Reset();
	if ( cwd ) cwd.Reset();
	if ( root ) root.Reset();
	if ( mtable ) mtable.Reset();

	// Destroy the address space and safely switch to the replacement
	// address space before things get dangerous.
	Memory::DestroyAddressSpace(prevaddrspace);
	addrspace = 0;

	// Init is nice and will gladly raise our orphaned children and zombies.
	Process* init = Scheduler::GetInitProcess();
	assert(init);
	kthread_mutex_lock(&childlock);
	while ( firstchild )
	{
		ScopedLock firstchildlock(&firstchild->parentlock);
		ScopedLock initlock(&init->childlock);
		Process* process = firstchild;
		firstchild = process->nextsibling;
		process->parent = init;
		process->prevsibling = NULL;
		process->nextsibling = init->firstchild;
		if ( init->firstchild )
			init->firstchild->prevsibling = process;
		init->firstchild = process;
		process->nozombify = true;
	}
	// Since we have no more children (they are with init now), we don't
	// have to worry about new zombie processes showing up, so just collect
	// those that are left. Then we satisfiy the invariant !zombiechild that
	// applies on process termination.
	while ( zombiechild )
	{
		Process* zombie = zombiechild;
		zombiechild = zombie->nextsibling;
		zombie->nextsibling = NULL;
		if ( zombiechild )
			zombiechild->prevsibling = NULL;
		zombie->nozombify = true;
		zombie->WaitedFor();
	}
	kthread_mutex_unlock(&childlock);

	iszombie = true;

	bool zombify = !nozombify;

	// Remove ourself from our process group.
	kthread_mutex_lock(&groupchildlock);
	if ( group )
		group->NotifyMemberExit(this);
	kthread_mutex_unlock(&groupchildlock);

	// This class instance will be destroyed by our parent process when it
	// has received and acknowledged our death.
	kthread_mutex_lock(&parentlock);
	if ( parent )
		parent->NotifyChildExit(this, zombify);
	kthread_mutex_unlock(&parentlock);

	// If nobody is waiting for us, then simply commit suicide.
	if ( !zombify )
		WaitedFor();
}

void Process::WaitedFor()
{
	kthread_mutex_lock(&parentlock);
	parent = NULL;
	kthread_mutex_unlock(&parentlock);
	kthread_mutex_lock(&groupparentlock);
	bool in_limbo = groupfirst && (grouplimbo = true);
	kthread_mutex_unlock(&groupparentlock);
	if ( !in_limbo )
		delete this;
}

void Process::ResetAddressSpace()
{
	ScopedLock lock1(&segment_write_lock);
	ScopedLock lock2(&segment_lock);

	assert(Memory::GetAddressSpace() == addrspace);

	for ( size_t i = 0; i < segments_used; i++ )
		Memory::UnmapRange(segments[i].addr, segments[i].size, PAGE_USAGE_USER_SPACE);

	Memory::Flush();

	segments_used = segments_length = 0;
	free(segments);
	segments = NULL;
}

void Process::NotifyMemberExit(Process* child)
{
	assert(child->group == this);
	kthread_mutex_lock(&groupparentlock);
	if ( child->groupprev )
		child->groupprev->groupnext = child->groupnext;
	else
		groupfirst = child->groupnext;
	if ( child->groupnext )
		child->groupnext->groupprev = child->groupprev;
	kthread_cond_signal(&groupchildleft);
	kthread_mutex_unlock(&groupparentlock);

	child->group = NULL;

	NotifyLeftProcessGroup();
}

void Process::NotifyLeftProcessGroup()
{
	ScopedLock parentlock(&groupparentlock);
	if ( !grouplimbo || groupfirst )
		return;
	grouplimbo = false;
	delete this;
}

void Process::NotifyChildExit(Process* child, bool zombify)
{
	kthread_mutex_lock(&childlock);

	if ( child->prevsibling )
		child->prevsibling->nextsibling = child->nextsibling;
	if ( child->nextsibling )
		child->nextsibling->prevsibling = child->prevsibling;
	if ( firstchild == child )
		firstchild = child->nextsibling;
	if ( firstchild )
		firstchild->prevsibling = NULL;

	if ( zombify )
	{
		if ( zombiechild )
			zombiechild->prevsibling = child;
		child->prevsibling = NULL;
		child->nextsibling = zombiechild;
		zombiechild = child;
	}

	kthread_mutex_unlock(&childlock);

	if ( zombify )
		NotifyNewZombies();
}

void Process::NotifyNewZombies()
{
	ScopedLock lock(&childlock);
	DeliverSignal(SIGCHLD);
	kthread_cond_broadcast(&zombiecond);
}

pid_t Process::Wait(pid_t thepid, int* status_ptr, int options)
{
	// TODO: Process groups are not supported yet.
	if ( thepid < -1 || thepid == 0 )
		return errno = ENOSYS, -1;

	ScopedLock lock(&childlock);

	// A process can only wait if it has children.
	if ( !firstchild && !zombiechild )
		return errno = ECHILD, -1;

	// Processes can only wait for their own children to exit.
	if ( 0 < thepid )
	{
		// TODO: This is a slow but multithread safe way to verify that the
		// target process has the correct parent.
		bool found = false;
		for ( Process* p = firstchild; !found && p; p = p->nextsibling )
			if ( p->pid == thepid && !p->nozombify )
				found = true;
		for ( Process* p = zombiechild; !found && p; p = p->nextsibling )
			if ( p->pid == thepid && !p->nozombify )
				found = true;
		if ( !found )
			return errno = ECHILD, -1;
	}

	Process* zombie = NULL;
	while ( !zombie )
	{
		for ( zombie = zombiechild; zombie; zombie = zombie->nextsibling )
			if ( (thepid == -1 || thepid == zombie->pid) && !zombie->nozombify )
				break;
		if ( zombie )
			break;
		if ( options & WNOHANG )
			return 0;
		if ( !kthread_cond_wait_signal(&zombiecond, &childlock) )
			return errno = EINTR, -1;
	}

	// Remove from the list of zombies.
	if ( zombie->prevsibling )
		zombie->prevsibling->nextsibling = zombie->nextsibling;
	if ( zombie->nextsibling )
		zombie->nextsibling->prevsibling = zombie->prevsibling;
	if ( zombiechild == zombie )
		zombiechild = zombie->nextsibling;
	if ( zombiechild )
		zombiechild->prevsibling = NULL;

	thepid = zombie->pid;

	// It is safe to access these clocks directly as the child process is no
	// longer running at this point and the values are nicely frozen.
	child_execute_clock.Advance(zombie->child_execute_clock.current_time);
	child_system_clock.Advance(zombie->child_system_clock.current_time);

	int status = zombie->exit_code;
	if ( status < 0 )
		status = WCONSTRUCT(WNATURE_SIGNALED, 128 + SIGKILL, SIGKILL);

	zombie->WaitedFor();

	if ( status_ptr )
		*status_ptr = status;

	return thepid;
}

pid_t sys_waitpid(pid_t pid, int* user_status, int options)
{
	int status = 0;
	pid_t ret = CurrentProcess()->Wait(pid, &status, options);
	if ( 0 < ret && !CopyToUser(user_status, &status, sizeof(status)) )
		return -1;
	return ret;
}

void Process::ExitThroughSignal(int signal)
{
	ExitWithCode(WCONSTRUCT(WNATURE_SIGNALED, 128 + signal, signal));
}

void Process::ExitWithCode(int requested_exit_code)
{
	ScopedLock lock(&threadlock);
	if ( exit_code == -1 )
		exit_code = requested_exit_code;

	// Broadcast SIGKILL to all our threads which will begin our long path
	// of process termination. We simply can't stop the threads as they may
	// be running in kernel mode doing dangerous stuff. This thread will be
	// destroyed by SIGKILL once the system call returns.
	for ( Thread* t = firstthread; t; t = t->nextsibling )
		t->DeliverSignal(SIGKILL);
}

void Process::AddChildProcess(Process* child)
{
	ScopedLock mylock(&childlock);
	ScopedLock itslock(&child->parentlock);
	assert(!child->parent);
	assert(!child->nextsibling);
	assert(!child->prevsibling);
	child->parent = this;
	child->nextsibling = firstchild;
	child->prevsibling = NULL;
	if ( firstchild )
		firstchild->prevsibling = child;
	firstchild = child;
}

Ref<MountTable> Process::GetMTable()
{
	ScopedLock lock(&ptrlock);
	assert(mtable);
	return mtable;
}

Ref<DescriptorTable> Process::GetDTable()
{
	ScopedLock lock(&ptrlock);
	assert(dtable);
	return dtable;
}

Ref<ProcessTable> Process::GetPTable()
{
	ScopedLock lock(&ptrlock);
	assert(ptable);
	return ptable;
}

Ref<Descriptor> Process::GetRoot()
{
	ScopedLock lock(&ptrlock);
	assert(root);
	return root;
}

Ref<Descriptor> Process::GetCWD()
{
	ScopedLock lock(&ptrlock);
	assert(cwd);
	return cwd;
}

void Process::SetRoot(Ref<Descriptor> newroot)
{
	ScopedLock lock(&ptrlock);
	assert(newroot);
	root = newroot;
}

void Process::SetCWD(Ref<Descriptor> newcwd)
{
	ScopedLock lock(&ptrlock);
	assert(newcwd);
	cwd = newcwd;
}

Ref<Descriptor> Process::GetDescriptor(int fd)
{
	ScopedLock lock(&ptrlock);
	assert(dtable);
	return dtable->Get(fd);
}

Process* Process::Fork()
{
	assert(CurrentProcess() == this);

	// TODO: This adds the new process to the process table, but it's not ready
	//       and functions that access this new process will be surprised that
	//       it's not fully constructed and really bad things will happen.
	Process* clone = new Process;
	if ( !clone )
		return NULL;

	if ( (clone->pid = (clone->ptable = ptable)->Allocate(clone)) < 0 )
	{
		delete clone;
		return NULL;
	}

	struct segment* clone_segments = NULL;

	// Fork the segment list.
	if ( segments )
	{
		size_t segments_size = sizeof(struct segment) * segments_used;
		if ( !(clone_segments = (struct segment*) malloc(segments_size)) )
		{
			delete clone;
			return NULL;
		}
		memcpy(clone_segments, segments, segments_size);
	}

	// Fork address-space here and copy memory.
	clone->addrspace = Memory::Fork();
	if ( !clone->addrspace )
	{
		free(clone_segments);
		delete clone;
		return NULL;
	}

	// Now it's too late to clean up here, if anything goes wrong, we simply
	// ask the process to commit suicide before it goes live.
	clone->segments = clone_segments;
	clone->segments_used = segments_used;
	clone->segments_length = segments_used;

	// Remember the relation to the child process.
	AddChildProcess(clone);

	// Add the new process to the current process group.
	kthread_mutex_lock(&groupchildlock);
	kthread_mutex_lock(&group->groupparentlock);
	clone->group = group;
	clone->groupprev = NULL;
	if ( (clone->groupnext = group->groupfirst) )
		group->groupfirst->groupprev = clone;
	group->groupfirst = clone;
	kthread_mutex_unlock(&group->groupparentlock);
	kthread_mutex_unlock(&groupchildlock);

	// Initialize everything that is safe and can't fail.
	kthread_mutex_lock(&resource_limits_lock);
	for ( size_t i = 0; i < RLIMIT_NUM_DECLARED; i++ )
		clone->resource_limits[i] = resource_limits[i];
	kthread_mutex_unlock(&resource_limits_lock);

	kthread_mutex_lock(&nicelock);
	clone->nice = nice;
	kthread_mutex_unlock(&nicelock);

	kthread_mutex_lock(&ptrlock);
	clone->root = root;
	clone->cwd = cwd;
	kthread_mutex_unlock(&ptrlock);

	kthread_mutex_lock(&idlock);
	clone->uid = uid;
	clone->gid = gid;
	clone->euid = euid;
	clone->egid = egid;
	clone->umask = umask;
	kthread_mutex_unlock(&idlock);

	kthread_mutex_lock(&signal_lock);
	memcpy(&clone->signal_actions, &signal_actions, sizeof(signal_actions));
	sigemptyset(&clone->signal_pending);
	clone->sigreturn = sigreturn;
	kthread_mutex_unlock(&signal_lock);

	// Initialize things that can fail and abort if needed.
	bool failure = false;

	kthread_mutex_lock(&ptrlock);
	if ( !(clone->dtable = dtable->Fork()) )
		failure = true;
	//if ( !(clone->mtable = mtable->Fork()) )
	//	failure = true;
	clone->mtable = mtable;
	kthread_mutex_unlock(&ptrlock);

	if ( !(clone->program_image_path = String::Clone(program_image_path)) )
		failure = true;

	// If the proces creation failed, ask the process to commit suicide and
	// not become a zombie, as we don't wait for it to exit. It will clean
	// up all the above resources and delete itself.
	if ( failure )
	{
		clone->AbortConstruction();
		return NULL;
	}

	return clone;
}

void Process::ResetForExecute()
{
	DeleteTimers();

	for ( int i = 0; i < SIG_MAX_NUM; i++ )
	{
		signal_actions[i].sa_flags = 0;
		if ( signal_actions[i].sa_handler == SIG_DFL )
			continue;
		if ( signal_actions[i].sa_handler == SIG_IGN )
			continue;
		signal_actions[i].sa_handler = SIG_DFL;
	}

	sigreturn = NULL;
	stack_t* signal_stack = &CurrentThread()->signal_stack;
	memset(signal_stack, 0, sizeof(*signal_stack));
	signal_stack->ss_flags = SS_DISABLE;

	ResetAddressSpace();
}

bool Process::MapSegment(struct segment* result, void* hint, size_t size,
                         int flags, int prot)
{
	// process->segment_write_lock is held at this point.
	// process->segment_lock is held at this point.

	if ( !size )
		size = 1;

	if ( !PlaceSegment(result, this, hint, size, flags) )
		return false;
	if ( !Memory::MapMemory(this, result->addr, result->size, result->prot = prot) )
	{
		// The caller is expected to self-destruct in this case, so the
		// segment just created is not removed.
		return false;
	}
	Memory::Flush();
	return true;
}

int Process::Execute(const char* programname, const uint8_t* program,
                     size_t programsize, int argc, const char* const* argv,
                     int envc, const char* const* envp,
                     struct thread_registers* regs)
{
	assert(argc != INT_MAX);
	assert(envc != INT_MAX);
	assert(CurrentProcess() == this);

	char* programname_clone = String::Clone(programname);
	if ( !programname_clone )
		return -1;

	ELF::Auxiliary aux;

	addr_t entry = ELF::Load(program, programsize, &aux);
	if ( !entry ) { delete[] programname_clone; return -1; }

	delete[] program_image_path;
	program_image_path = programname_clone; programname_clone = NULL;

	uintptr_t userspace_addr;
	size_t userspace_size;
	Memory::GetUserVirtualArea(&userspace_addr, &userspace_size);

	const size_t stack_size = 512UL * 1024UL;
	void* stack_hint = (void*) (userspace_addr + userspace_size - stack_size);
	const int stack_prot = PROT_READ | PROT_WRITE | PROT_KREAD | PROT_KWRITE | PROT_FORK;

	if ( !aux.tls_mem_align )
		aux.tls_mem_align = 1;
	if ( Page::Size() < aux.tls_mem_align )
		return errno = EINVAL, -1;
	if ( !aux.uthread_align )
		aux.uthread_align = 1;
	if ( Page::Size() < aux.uthread_align )
		return errno = EINVAL, -1;
	if ( aux.uthread_size < sizeof(struct uthread) )
		aux.uthread_size = sizeof(struct uthread);

	size_t raw_tls_size = aux.tls_mem_size;
	size_t raw_tls_size_aligned = -(-raw_tls_size & ~(aux.tls_mem_align-1));
	if ( raw_tls_size && raw_tls_size_aligned == 0 /* overflow */ )
		return errno = EINVAL, -1;
	int raw_tls_kprot = PROT_KWRITE | PROT_FORK;
	int raw_tls_prot = PROT_READ | PROT_KREAD | PROT_FORK;
	void* raw_tls_hint = stack_hint;

	size_t tls_size = raw_tls_size_aligned + aux.uthread_size;
	size_t tls_offset_tls = 0;
	size_t tls_offset_uthread = raw_tls_size_aligned;
	if ( aux.tls_mem_align < aux.uthread_align )
	{
		size_t more_aligned = -(-raw_tls_size_aligned & ~(aux.uthread_align-1));
		if ( raw_tls_size_aligned && more_aligned == 0 /* overflow */ )
			return errno = EINVAL, -1;
		size_t difference = more_aligned - raw_tls_size_aligned;
		tls_size += difference;
		tls_offset_tls += difference;
		tls_offset_uthread += difference;
	}
	assert((tls_offset_tls & (aux.tls_mem_align-1)) == 0);
	assert((tls_offset_uthread & (aux.uthread_align-1)) == 0);
	int tls_prot = PROT_READ | PROT_WRITE | PROT_KREAD | PROT_KWRITE | PROT_FORK;
	void* tls_hint = stack_hint;

	size_t auxcode_size = Page::Size();
	int auxcode_kprot = PROT_KWRITE | PROT_FORK;
	int auxcode_prot = PROT_EXEC | PROT_READ | PROT_KREAD | PROT_FORK;
	void* auxcode_hint = stack_hint;

	size_t arg_size = 0;

	size_t argv_size = sizeof(char*) * (argc + 1);
	size_t envp_size = sizeof(char*) * (envc + 1);

	arg_size += argv_size;
	arg_size += envp_size;

	for ( int i = 0; i < argc; i++ )
		arg_size += strlen(argv[i]) + 1;
	for ( int i = 0; i < envc; i++ )
		arg_size += strlen(envp[i]) + 1;

	struct segment arg_segment;
	struct segment stack_segment;
	struct segment raw_tls_segment;
	struct segment tls_segment;
	struct segment auxcode_segment;

	kthread_mutex_lock(&segment_write_lock);
	kthread_mutex_lock(&segment_lock);

	if ( !(MapSegment(&arg_segment, stack_hint, arg_size, 0, stack_prot) &&
	       MapSegment(&stack_segment, stack_hint, stack_size, 0, stack_prot) &&
	       MapSegment(&raw_tls_segment, raw_tls_hint, raw_tls_size, 0, raw_tls_kprot) &&
	       MapSegment(&tls_segment, tls_hint, tls_size, 0, tls_prot) &&
	       MapSegment(&auxcode_segment, auxcode_hint, auxcode_size, 0, auxcode_kprot)) )
	{
		kthread_mutex_unlock(&segment_lock);
		kthread_mutex_unlock(&segment_write_lock);
		ResetForExecute();
		return errno = ENOMEM, -1;
	}

	char** target_argv = (char**) ((char*) arg_segment.addr + 0);
	char** target_envp = (char**) ((char*) arg_segment.addr + argv_size);
	char* target_strings = (char*) ((char*) arg_segment.addr + argv_size + envp_size);
	size_t target_strings_offset = 0;

	for ( int i = 0; i < argc; i++ )
	{
		const char* arg = argv[i];
		size_t arg_len = strlen(arg);
		char* target_arg = target_strings + target_strings_offset;
		strcpy(target_arg, arg);
		target_argv[i] = target_arg;
		target_strings_offset += arg_len + 1;
	}
	target_argv[argc] = (char*) NULL;

	for ( int i = 0; i < envc; i++ )
	{
		const char* env = envp[i];
		size_t env_len = strlen(env);
		char* target_env = target_strings + target_strings_offset;
		strcpy(target_env, env);
		target_envp[i] = target_env;
		target_strings_offset += env_len + 1;
	}
	target_envp[envc] = (char*) NULL;

	const uint8_t* file_raw_tls = program + aux.tls_file_offset;

	uint8_t* target_raw_tls = (uint8_t*) raw_tls_segment.addr;
	memcpy(target_raw_tls, file_raw_tls, aux.tls_file_size);
	memset(target_raw_tls + aux.tls_file_size, 0, aux.tls_mem_size - aux.tls_file_size);
	Memory::ProtectMemory(this, raw_tls_segment.addr, raw_tls_segment.size, raw_tls_prot);

	uint8_t* target_tls = (uint8_t*) (tls_segment.addr + tls_offset_tls);
	assert((((uintptr_t) target_tls) & (aux.tls_mem_align-1)) == 0);
	memcpy(target_tls, file_raw_tls, aux.tls_file_size);
	memset(target_tls + aux.tls_file_size, 0, aux.tls_mem_size - aux.tls_file_size);

	struct uthread* uthread = (struct uthread*) (tls_segment.addr + tls_offset_uthread);
	assert((((uintptr_t) uthread) & (aux.uthread_align-1)) == 0);
	memset(uthread, 0, sizeof(*uthread));
	uthread->uthread_pointer = uthread;
	uthread->uthread_size = aux.uthread_size;
	uthread->uthread_flags = UTHREAD_FLAG_INITIAL;
	uthread->tls_master_mmap = (void*) raw_tls_segment.addr;
	uthread->tls_master_size = aux.tls_mem_size;
	uthread->tls_master_align = aux.tls_mem_align;
	uthread->tls_mmap = (void*) tls_segment.addr;
	uthread->tls_size = tls_size;
	uthread->stack_mmap = (void*) stack_segment.addr;
	uthread->stack_size = stack_segment.size;
	uthread->arg_mmap = (void*) arg_segment.addr;
	uthread->arg_size = arg_segment.size;
	memset(uthread + 1, 0, aux.uthread_size - sizeof(struct uthread));

	memset(regs, 0, sizeof(*regs));
#if defined(__i386__)
	regs->eax = argc;
	regs->ebx = (size_t) target_argv;
	regs->edx = envc;
	regs->ecx = (size_t) target_envp;
	regs->eip = entry;
	regs->esp = (stack_segment.addr + stack_segment.size) & ~15UL;
	regs->ebp = regs->esp;
	regs->cs = UCS | URPL;
	regs->ds = UDS | URPL;
	regs->ss = UDS | URPL;
	regs->eflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	regs->signal_pending = 0;
	regs->gsbase = (uint32_t) uthread;
	regs->cr3 = addrspace;
	regs->kernel_stack = GDT::GetKernelStack();
	memcpy(regs->fpuenv, Float::fpu_initialized_regs, 512);
#elif defined(__x86_64__)
	regs->rdi = argc;
	regs->rsi = (size_t) target_argv;
	regs->rdx = envc;
	regs->rcx = (size_t) target_envp;
	regs->rip = entry;
	regs->rsp = (stack_segment.addr + stack_segment.size) & ~15UL;
	regs->rbp = regs->rsp;
	regs->cs = UCS | URPL;
	regs->ds = UDS | URPL;
	regs->ss = UDS | URPL;
	regs->rflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	regs->signal_pending = 0;
	regs->fsbase = (uint64_t) uthread;
	regs->cr3 = addrspace;
	regs->kernel_stack = GDT::GetKernelStack();
	memcpy(regs->fpuenv, Float::fpu_initialized_regs, 512);
#else
#warning "You need to implement initializing the first thread after execute"
#endif

	uint8_t* auxcode = (uint8_t*) auxcode_segment.addr;
#if defined(__i386__)
	sigreturn = (void (*)(void)) &auxcode[0];
	auxcode[0] = 0xCD; /* int .... */
	auxcode[1] = 0x83; /* ... $131 */
#elif defined(__x86_64__)
	sigreturn = (void (*)(void)) &auxcode[0];
	auxcode[0] = 0xCD; /* int .... */
	auxcode[1] = 0x83; /* ... $131 */
#else
	(void) auxcode;
	#warning "You need to initialize auxcode with a sigreturn routine"
#endif
	Memory::ProtectMemory(this, auxcode_segment.addr, auxcode_segment.size, auxcode_prot);

	kthread_mutex_unlock(&segment_lock);
	kthread_mutex_unlock(&segment_write_lock);

	dtable->OnExecute();

	return 0;
}

static
const char* shebang_lookup_environment(const char* name, char* const* envp)
{
	size_t equalpos = strcspn(name, "=");
	if ( name[equalpos] == '=' )
		return NULL;
	size_t namelen = equalpos;
	for ( size_t i = 0; envp[i]; i++ )
	{
		if ( strncmp(name, envp[i], namelen) )
			continue;
		if ( envp[i][namelen] != '=' )
			continue;
		return envp[i] + namelen + 1;
	}
	return NULL;
}

static char* shebang_tokenize(char** saved)
{
	char* data = *saved;
	if ( !data )
		return *saved = NULL;
	while ( data[0] && isspace((unsigned char) data[0]) )
		data++;
	if ( !data[0] )
		return *saved = NULL;
	size_t input = 0;
	size_t output = 0;
	bool singly = false;
	bool doubly = false;
	bool escaped = false;
	for ( ; data[input]; input++ )
	{
		char c = data[input];
		if ( !escaped && !singly && !doubly && isspace((unsigned char) c) )
			break;
		if ( !escaped && !doubly && c == '\'' )
		{
			singly = !singly;
			continue;
		}
		if ( !escaped && !singly && c == '"' )
		{
			doubly = !doubly;
			continue;
		}
		if ( !singly && !escaped && c == '\\' )
		{
			escaped = true;
			continue;
		}
		if ( escaped )
		{
			switch ( c )
			{
			case 'a': c = '\a'; break;
			case 'b': c = '\b'; break;
			case 'e': c = '\e'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'v': c = '\v'; break;
			default: break;
			};
		}
		escaped = false;
		data[output++] = c;
	}
	if ( data[input] )
		*saved = data + input + 1;
	else
		*saved = NULL;
	data[output] = '\0';
	return data;
}

static size_t shebang_count_arguments(char* line)
{
	size_t result = 0;
	while ( shebang_tokenize(&line) )
		result++;
	return result;
}

// NOTE: The PATH-searching logic is repeated multiple places. Until this logic
//       can be shared somehow, you need to keep this comment in sync as well
//       as the logic in these files:
//         * kernel/process.cpp
//         * libc/unistd/execvpe.c
//         * utils/which.c
// NOTE: See comments in execvpe() for algorithmic commentary.

static bool sys_execve_alloc(addralloc_t* alloc, size_t size)
{
	if ( !AllocateKernelAddress(alloc, size) )
		return false;
	if ( !Memory::MapRange(alloc->from, alloc->size, PROT_KREAD | PROT_KWRITE, PAGE_USAGE_EXECUTE) )
		return FreeKernelAddress(alloc), false;
	Memory::Flush();
	return true;
}

static void sys_execve_free(addralloc_t* alloc)
{
	Memory::UnmapRange(alloc->from, alloc->size, PAGE_USAGE_EXECUTE);
	Memory::Flush();
	FreeKernelAddress(alloc);
}

static
int sys_execve_kernel(const char* filename,
                      int argc,
                      char* const* argv,
                      int envc,
                      char* const* envp,
                      struct thread_registers* regs)
{
	Process* process = CurrentProcess();

	ioctx_t ctx;
	SetupKernelIOCtx(&ctx);
	Ref<Descriptor> from = filename[0] == '/' ? process->GetRoot() : process->GetCWD();
	Ref<Descriptor> desc = from->open(&ctx, filename, O_EXEC | O_READ, 0);
	if ( !desc )
		return -1;
	from.Reset();

	struct stat st;
	if ( desc->stat(&ctx, &st) )
		return -1;
	if ( !(st.st_mode & 0111) )
		return errno = EACCES, -1;
	if ( st.st_size < 0 )
		return errno = EINVAL, -1;
	if ( (uintmax_t) SIZE_MAX < (uintmax_t) st.st_size )
		return errno = EFBIG, -1;

	size_t filesize = (size_t) st.st_size;

	addralloc_t buffer_alloc;
	if ( !sys_execve_alloc(&buffer_alloc, filesize) )
		return -1;

	uint8_t* buffer = (uint8_t*) buffer_alloc.from;
	for ( size_t sofar = 0; sofar < filesize; )
	{
		ssize_t amount = desc->read(&ctx, buffer + sofar, filesize - sofar);
		if ( amount < 0 )
			return sys_execve_free(&buffer_alloc), -1;
		if ( amount == 0 )
			return sys_execve_free(&buffer_alloc), errno = EEOF, -1;
		sofar += amount;
	}

	desc.Reset();

	int result = process->Execute(filename, buffer, filesize, argc, argv, envc, envp, regs);

	if ( result == 0 || errno != ENOEXEC ||
	     filesize < 2 || buffer[0] != '#' || buffer[1] != '!' )
		return sys_execve_free(&buffer_alloc), result;

	size_t line_length = 0;
	while ( 2 + line_length < filesize && buffer[2 + line_length] != '\n' )
		line_length++;
	if ( line_length == filesize )
		return sys_execve_free(&buffer_alloc), errno = ENOEXEC, -1;

	char* line = new char[line_length+1];
	if ( !line )
		return sys_execve_free(&buffer_alloc), -1;
	memcpy(line, buffer + 2, line_length);
	line[line_length] = '\0';
	sys_execve_free(&buffer_alloc);

	char* line_clone = String::Clone(line);
	if ( !line_clone )
		return delete[] line, -1;
	size_t argument_count = shebang_count_arguments(line_clone);
	delete[] line_clone;

	if ( !argument_count || INT_MAX < argument_count )
		return delete[] line, errno = ENOEXEC, -1;

	int sb_argc = (int) argument_count;
	char** sb_argv = new char*[sb_argc];
	if ( !sb_argv )
		return delete[] line, -1;

	char* sb_saved = line;
	for ( int i = 0; i < sb_argc; i++ )
		sb_argv[i] = shebang_tokenize(&sb_saved);

	if ( INT_MAX - argc <= sb_argc )
		return delete[] sb_argv, delete[] line, errno = EOVERFLOW, -1;

	if ( !sb_argv[0] || !sb_argv[0][0] )
		return delete[] sb_argv, delete[] line, errno = ENOENT, -1;

	int new_argc = sb_argc + argc;
	char** new_argv = new char*[new_argc + 1];
	if ( !new_argv )
		return delete[] sb_argv, delete[] line, -1;

	for ( int i = 0; i < sb_argc; i++ )
		new_argv[i] = sb_argv[i];
	new_argv[sb_argc + 0] = (char*) filename;
	for ( int i = 1; i < argc; i++ )
		new_argv[sb_argc + i] = argv[i];
	new_argv[new_argc] = (char*) NULL;

	result = -1;

	// (See the above comment block before editing this searching logic)
	const char* path = shebang_lookup_environment("PATH", envp);
	bool search_path = !strchr(sb_argv[0], '/') && path;
	bool any_tries = false;
	bool any_eacces = false;

	const char* new_argv0 = sb_argv[0];
	while ( search_path && *path )
	{
		size_t len = strcspn(path, ":");
		if ( !len )
		{
			path++;
			continue;
		}

		any_tries = true;

		char* dirpath = strndup(path, len);
		if ( !dirpath )
			return -1;
		if ( (path += len)[0] == ':' )
			path++;
		while ( len && dirpath[len - 1] == '/' )
			dirpath[--len] = '\0';

		char* fullpath;
		if ( asprintf(&fullpath, "%s/%s", dirpath, sb_argv[0]) < 0 )
			return free(dirpath), -1;

		result = sys_execve_kernel(fullpath, new_argc, new_argv, envc, envp, regs);

		free(fullpath);
		free(dirpath);

		if ( result == 0 )
			break;

		if ( errno == ENOENT )
			continue;

		if ( errno == ELOOP ||
		     errno == EISDIR ||
		     errno == ENAMETOOLONG ||
		     errno == ENOTDIR )
			continue;

		if ( errno == EACCES )
		{
			any_eacces = true;
			continue;
		}

		if ( errno == EACCES )
		{
			any_eacces = true;
			continue;
		}

		break;
	}

	if ( !any_tries )
		result = sys_execve_kernel(new_argv0, new_argc, new_argv, envc, envp, regs);

	if ( result < 0 && any_eacces )
		errno = EACCES;

	delete[] new_argv;
	delete[] sb_argv;
	delete[] line;

	return result;
}

int sys_execve(const char* user_filename,
               char* const* user_argv,
               char* const* user_envp)
{
	char* filename;
	int argc;
	int envc;
	char** argv;
	char** envp;
	int result = -1;
	struct thread_registers regs;
	memset(&regs, 0, sizeof(regs));

	if ( !user_filename || !user_argv || !user_envp )
		return errno = EFAULT, -1;

	if ( !(filename = GetStringFromUser(user_filename)) )
		goto cleanup_done;

	argc = 0;
	while ( true )
	{
		const char* user_arg;
		if ( !CopyFromUser(&user_arg, user_argv + argc, sizeof(user_arg)) )
			goto cleanup_filename;
		if ( !user_arg )
			break;
		if ( ++argc == INT_MAX )
		{
			errno = E2BIG;
			goto cleanup_filename;
		}
	}

	argv = new char*[argc+1];
	if ( !argv )
		goto cleanup_filename;
	memset(argv, 0, sizeof(char*) * (argc+1));

	for ( int i = 0; i < argc; i++ )
	{
		const char* user_arg;
		if ( !CopyFromUser(&user_arg, user_argv + i, sizeof(user_arg)) )
			goto cleanup_argv;
		if ( !(argv[i] = GetStringFromUser(user_arg)) )
			goto cleanup_argv;
	}

	envc = 0;
	while ( true )
	{
		const char* user_env;
		if ( !CopyFromUser(&user_env, user_envp + envc, sizeof(user_env)) )
			goto cleanup_argv;
		if ( !user_env )
			break;
		if ( ++envc == INT_MAX  )
		{
			errno = E2BIG;
			goto cleanup_argv;
		}
	}

	envp = new char*[envc+1];
	if ( !envp )
		goto cleanup_argv;
	memset(envp, 0, sizeof(char*) * (envc+1));

	for ( int i = 0; i < envc; i++ )
	{
		const char* user_env;
		if ( !CopyFromUser(&user_env, user_envp + i, sizeof(user_env)) )
			goto cleanup_envp;
		if ( !(envp[i] = GetStringFromUser(user_envp[i])) )
			goto cleanup_envp;
	}

	result = sys_execve_kernel(filename, argc, argv, envc, envp, &regs);

cleanup_envp:
	for ( int i = 0; i < envc; i++)
		delete[] envp[i];
	delete[] envp;
cleanup_argv:
	for ( int i = 0; i < argc; i++)
		delete[] argv[i];
	delete[] argv;
cleanup_filename:
	delete[] filename;
cleanup_done:
	if ( result == 0 )
		LoadRegisters(&regs);
	return result;
}

pid_t sys_tfork(int flags, struct tfork* user_regs)
{
	struct tfork regs;
	if ( !CopyFromUser(&regs, user_regs, sizeof(regs)) )
		return -1;

	if ( Signal::IsPending() )
		return errno = EINTR, -1;

	bool making_process = flags == SFFORK;
	bool making_thread = (flags & (SFPROC | SFPID | SFFD | SFMEM | SFCWD | SFROOT)) == SFPROC;

	// TODO: Properly support tfork(2).
	if ( !(making_thread || making_process) )
		return errno = ENOSYS, -1;

	if ( regs.altstack.ss_flags & ~__SS_SUPPORTED_FLAGS )
		return errno = EINVAL, -1;

	size_t stack_alignment = 16;

	// TODO: Is it a hack to create a new kernel stack here?
	Thread* curthread = CurrentThread();
	size_t newkernelstacksize = curthread->kernelstacksize;
	uint8_t* newkernelstack = new uint8_t[newkernelstacksize + stack_alignment];
	if ( !newkernelstack )
		return -1;

	uintptr_t stack_aligned = (uintptr_t) newkernelstack;
	size_t stack_aligned_size = newkernelstacksize;

	if ( ((uintptr_t) stack_aligned) & (stack_alignment-1) )
		stack_aligned = (stack_aligned + 16) & ~(stack_alignment-1);
	stack_aligned_size &= 0xFFFFFFF0;

	Process* child_process;
	if ( making_thread )
		child_process = CurrentProcess();
	else if ( !(child_process = CurrentProcess()->Fork()) )
	{
		delete[] newkernelstack;
		return -1;
	}

	struct thread_registers cpuregs;
	memset(&cpuregs, 0, sizeof(cpuregs));
#if defined(__i386__)
	cpuregs.eip = regs.eip;
	cpuregs.esp = regs.esp;
	cpuregs.eax = regs.eax;
	cpuregs.ebx = regs.ebx;
	cpuregs.ecx = regs.ecx;
	cpuregs.edx = regs.edx;
	cpuregs.edi = regs.edi;
	cpuregs.esi = regs.esi;
	cpuregs.ebp = regs.ebp;
	cpuregs.cs = UCS | URPL;
	cpuregs.ds = UDS | URPL;
	cpuregs.ss = UDS | URPL;
	cpuregs.eflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	cpuregs.fsbase = regs.fsbase;
	cpuregs.gsbase = regs.gsbase;
	cpuregs.cr3 = child_process->addrspace;
	cpuregs.kernel_stack = stack_aligned + stack_aligned_size;
	memcpy(&cpuregs.fpuenv, Float::fpu_initialized_regs, 512);
#elif defined(__x86_64__)
	cpuregs.rip = regs.rip;
	cpuregs.rsp = regs.rsp;
	cpuregs.rax = regs.rax;
	cpuregs.rbx = regs.rbx;
	cpuregs.rcx = regs.rcx;
	cpuregs.rdx = regs.rdx;
	cpuregs.rdi = regs.rdi;
	cpuregs.rsi = regs.rsi;
	cpuregs.rbp = regs.rbp;
	cpuregs.r8  = regs.r8;
	cpuregs.r9  = regs.r9;
	cpuregs.r10 = regs.r10;
	cpuregs.r11 = regs.r11;
	cpuregs.r12 = regs.r12;
	cpuregs.r13 = regs.r13;
	cpuregs.r14 = regs.r14;
	cpuregs.r15 = regs.r15;
	cpuregs.cs = UCS | URPL;
	cpuregs.ds = UDS | URPL;
	cpuregs.ss = UDS | URPL;
	cpuregs.rflags = FLAGS_RESERVED1 | FLAGS_INTERRUPT | FLAGS_ID;
	cpuregs.fsbase = regs.fsbase;
	cpuregs.gsbase = regs.gsbase;
	cpuregs.cr3 = child_process->addrspace;
	cpuregs.kernel_stack = stack_aligned + stack_aligned_size;
	memcpy(&cpuregs.fpuenv, Float::fpu_initialized_regs, 512);
#else
#warning "You need to implement initializing the registers of the new thread"
#endif

	// If the thread could not be created, make the process commit suicide
	// in a manner such that we don't wait for its zombie.
	Thread* thread = CreateKernelThread(child_process, &cpuregs);
	if ( !thread )
	{
		if ( making_process )
			child_process->AbortConstruction();
		return -1;
	}

	thread->kernelstackpos = (addr_t) newkernelstack;
	thread->kernelstacksize = newkernelstacksize;
	thread->kernelstackmalloced = true;
	memcpy(&thread->signal_mask, &regs.sigmask, sizeof(sigset_t));
	memcpy(&thread->signal_stack, &regs.altstack, sizeof(stack_t));

	StartKernelThread(thread);

	return child_process->pid;
}

pid_t sys_getpid(void)
{
	return CurrentProcess()->pid;
}

pid_t Process::GetParentProcessId()
{
	ScopedLock lock(&parentlock);
	if( !parent )
		return 0;
	return parent->pid;
}

pid_t sys_getppid(void)
{
	return CurrentProcess()->GetParentProcessId();
}

pid_t sys_getpgid(pid_t pid)
{
	Process* process = !pid ? CurrentProcess() : CurrentProcess()->GetPTable()->Get(pid);
	if ( !process )
		return errno = ESRCH, -1;

	// Prevent the process group from changing while we read it.
	ScopedLock childlock(&process->groupchildlock);
	assert(process->group);

	return process->group->pid;
}

int sys_setpgid(pid_t pid, pid_t pgid)
{
	// TODO: Prevent changing the process group of zombies and other volatile
	//       things that are about to implode.
	// TODO: Either prevent changing the process group after an exec or provide
	//       a version of this system call with a flags parameter that lets you
	//       decide if you want this behavior. This will fix a race condition
	//       where the shell spawns a child and both parent and child sets the
	//       process group, but the child sets the process group and execve's
	//       and the new program image exploits this 'bug' and also changes the
	//       process group, and then the shell gets around to change the process
	//       group. This probably unlikely, but correctness over all!

	// Find the processes in question.
	Process* process = !pid ? CurrentProcess() : CurrentProcess()->GetPTable()->Get(pid);
	if ( !process )
		return errno = ESRCH, -1;
	Process* group = !pgid ? process : CurrentProcess()->GetPTable()->Get(pgid);
	if ( !group )
		return errno = ESRCH, -1;

	// Prevent the current group from being changed while we also change it
	ScopedLock childlock(&process->groupchildlock);
	assert(process->group);

	// Exit early if this is a noop.
	if ( process->group == group )
		return 0;

	// Prevent changing the process group of a process group leader.
	if ( process->group == process )
		return errno = EPERM, -1;

	// Remove the process from its current process group.
	kthread_mutex_lock(&process->group->groupparentlock);
	if ( process->groupprev )
		process->groupprev->groupnext = process->groupnext;
	else
		process->group->groupfirst = process->groupnext;
	if ( process->groupnext )
		process->groupnext->groupprev = process->groupprev;
	kthread_cond_signal(&process->group->groupchildleft);
	kthread_mutex_unlock(&process->group->groupparentlock);
	process->group->NotifyLeftProcessGroup();
	process->group = NULL;

	// TODO: Somehow prevent joining a zombie group, or worse yet, one that is
	//       currently being deleted by its parent!

	// Insert the process into its new process group.
	kthread_mutex_lock(&group->groupparentlock);
	process->groupprev = NULL;
	process->groupnext = group->groupfirst;
	if ( group->groupfirst )
		group->groupfirst->groupprev = process;
	group->groupfirst = process;
	process->group = group;
	kthread_mutex_unlock(&group->groupparentlock);

	return 0;
}

size_t sys_getpagesize(void)
{
	return Page::Size();
}

mode_t sys_umask(mode_t newmask)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->idlock);
	mode_t oldmask = process->umask;
	process->umask = newmask & 0666;
	return oldmask;
}

mode_t sys_getumask(void)
{
	Process* process = CurrentProcess();
	ScopedLock lock(&process->idlock);
	return process->umask;
}

static void GetAssertInfo(struct scram_assert* info,
                          const void* user_info_ptr)
{
	memset(info, 0, sizeof(*info));
	struct scram_assert user_info;
	if ( !CopyFromUser(&user_info, user_info_ptr, sizeof(user_info)) )
		return;
	info->filename = GetStringFromUser(user_info.filename);
	info->line = user_info.line;
	info->function = GetStringFromUser(user_info.function);
	info->expression = GetStringFromUser(user_info.expression);
}

static void FreeAssertInfo(struct scram_assert* info)
{
	delete[] (char*) info->filename;
	delete[] (char*) info->function;
	delete[] (char*) info->expression;
}

static
void GetUndefinedBehaviorInfo(struct scram_undefined_behavior* info,
                              const void* user_info_ptr)
{
	memset(info, 0, sizeof(*info));
	struct scram_undefined_behavior user_info;
	if ( !CopyFromUser(&user_info, user_info_ptr, sizeof(user_info)) )
		return;
	info->filename = GetStringFromUser(user_info.filename);
	info->line = user_info.line;
	info->column = user_info.column;
	info->violation = GetStringFromUser(user_info.violation);
}

static void FreeUndefinedBehaviorInfo(struct scram_undefined_behavior* info)
{
	delete[] info->filename;
	delete[] info->violation;
}

__attribute__((noreturn))
void sys_scram(int event, const void* user_info)
{
	Process* process = CurrentProcess();
	// TODO: Prohibit execve such that program_image_path is protected.
	process->ExitThroughSignal(SIGABRT);
	if ( event == SCRAM_ASSERT )
	{
		struct scram_assert info;
		GetAssertInfo(&info, user_info);
		Log::PrintF("%s[%ji]: Assertion failure: %s:%lu: %s: %s\n",
			process->program_image_path,
			(intmax_t) process->pid,
			info.filename ? info.filename : "<unknown>",
			info.line,
			info.function ? info.function : "<unknown>",
			info.expression ? info.expression : "<unknown>");
		FreeAssertInfo(&info);
	}
	else if ( event == SCRAM_STACK_SMASH )
	{
		Log::PrintF("%s[%ji]: Stack smashing detected\n",
			process->program_image_path,
			(intmax_t) process->pid);
	}
	else if ( event == SCRAM_UNDEFINED_BEHAVIOR )
	{
		struct scram_undefined_behavior info;
		GetUndefinedBehaviorInfo(&info, user_info);
		Log::PrintF("%s[%ji]: Undefined behavior: %s at %s:%lu:%lu\n",
			process->program_image_path,
			(intmax_t) process->pid,
			info.violation ? info.violation : "<unknown>",
			info.filename ? info.filename : "<unknown>",
			info.line,
			info.column);
		FreeUndefinedBehaviorInfo(&info);
	}
	else
	{
		Log::PrintF("%s[%ji]: Unknown scram event %i\n",
			process->program_image_path,
			(intmax_t) process->pid,
			event);
	}
	// TODO: Allow debugging this event (and see signal.cpp sigreturn).
	kthread_exit();
}

} // namespace Sortix
