/*
 * Copyright (c) 2011, 2012, 2013, 2014, 205, 2016 Jonas 'Sortie' Termansen.
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
 * sortix/kernel/process.h
 * A named collection of threads.
 */

#ifndef INCLUDE_SORTIX_KERNEL_PROCESS_H
#define INCLUDE_SORTIX_KERNEL_PROCESS_H

#include <sortix/fork.h>
#include <sortix/resource.h>
#include <sortix/sigaction.h>
#include <sortix/signal.h>
#include <sortix/sigset.h>

#include <sortix/kernel/clock.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/registers.h>
#include <sortix/kernel/segment.h>
#include <sortix/kernel/time.h>
#include <sortix/kernel/timer.h>
#include <sortix/kernel/user-timer.h>
#include <sortix/kernel/cpu.h>

#define PROCESS_TIMER_NUM_MAX 32

namespace Sortix {

class Thread;
class Process;
class Descriptor;
class DescriptorTable;
class MountTable;
class ProcessTable;
struct ProcessSegment;
struct ProcessTimer;
struct ioctx_struct;
typedef struct ioctx_struct ioctx_t;
struct segment;

class Process
{
friend void Process__OnLastThreadExit(void*);

public:
	Process();
	~Process();

public:
	char* program_image_path;
	addr_t addrspace;
	pid_t pid;

public:
	kthread_mutex_t nicelock;
	int nice;

public:
	kthread_mutex_t idlock;
	uid_t uid, euid;
	gid_t gid, egid;
	mode_t umask;

private:
	kthread_mutex_t ptrlock;
	Ref<Descriptor> tty;
	Ref<Descriptor> root;
	Ref<Descriptor> cwd;
	Ref<MountTable> mtable;
	Ref<DescriptorTable> dtable;

public:
	Ref<ProcessTable> ptable;

public:
	kthread_mutex_t resource_limits_lock;
	struct rlimit resource_limits[RLIMIT_NUM_DECLARED];

public:
	kthread_mutex_t signal_lock;
	struct sigaction signal_actions[SIG_MAX_NUM];
	sigset_t signal_pending;
	void (*sigreturn)(void);

public:
	void BootstrapTables(Ref<DescriptorTable> dtable, Ref<MountTable> mtable);
	void BootstrapDirectories(Ref<Descriptor> root);
	Ref<DescriptorTable> GetDTable();
	Ref<MountTable> GetMTable();
	Ref<ProcessTable> GetPTable();
	Ref<Descriptor> GetTTY();
	Ref<Descriptor> GetRoot();
	Ref<Descriptor> GetCWD();
	Ref<Descriptor> GetDescriptor(int fd);
	void SetTTY(Ref<Descriptor> tty);
	void SetRoot(Ref<Descriptor> newroot);
	void SetCWD(Ref<Descriptor> newcwd);

public:
	Process* parent;
	Process* prevsibling;
	Process* nextsibling;
	Process* firstchild;
	Process* zombiechild;
	Process* group;
	Process* groupprev;
	Process* groupnext;
	Process* groupfirst;
	Process* session;
	Process* sessionprev;
	Process* sessionnext;
	Process* sessionfirst;
	kthread_mutex_t childlock;
	kthread_mutex_t parentlock;
	kthread_cond_t zombiecond;
	bool iszombie;
	bool nozombify;
	bool limbo;
	int exit_code;

public:
	Thread* firstthread;
	kthread_mutex_t threadlock;
	size_t threads_not_exiting_count;
	bool threads_exiting;

public:
	struct segment* segments;
	size_t segments_used;
	size_t segments_length;
	kthread_mutex_t segment_write_lock;
	kthread_mutex_t segment_lock;

public:
	kthread_mutex_t user_timers_lock;
	UserTimer user_timers[PROCESS_TIMER_NUM_MAX];
	Timer alarm_timer;
	Clock execute_clock;
	Clock system_clock;
	Clock child_execute_clock;
	Clock child_system_clock;

public:
	int Execute(const char* programname, const uint8_t* program,
	            size_t programsize, int argc, const char* const* argv,
	            int envc, const char* const* envp,
	            struct thread_registers* regs);
	void ResetAddressSpace();
	void ExitThroughSignal(int signal);
	void ExitWithCode(int exit_code);
	pid_t Wait(pid_t pid, int* status, int options);
	bool DeliverSignal(int signum);
	bool DeliverGroupSignal(int signum);
	bool DeliverSessionSignal(int signum);
	void OnThreadDestruction(Thread* thread);
	void ScheduleDeath();
	void AbortConstruction();
	bool MapSegment(struct segment* result, void* hint, size_t size, int flags,
	                int prot);
	void GroupRemoveMember(Process* child);
	void SessionRemoveMember(Process* child);

public:
	Process* Fork();

private:
	void LastPrayer();
	void WaitedFor();
	void NotifyChildExit(Process* child, bool zombify);
	void DeleteTimers();
	bool IsLimboDone();

public:
	void OnLastThreadExit();
	void AfterLastThreadExit();
	void ResetForExecute();

};

extern kthread_mutex_t process_family_lock;

Process* CurrentProcess();

} // namespace Sortix

#endif
