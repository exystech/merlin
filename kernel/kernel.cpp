/*
 * Copyright (c) 2011-2018, 2021-2022 Jonas 'Sortie' Termansen.
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
 * kernel.cpp
 * The main kernel initialization routine. Configures hardware and starts an
 * initial process from the init ramdisk, allowing a full operating system.
 */

#include <sys/ioctl.h>
#include <sys/types.h>

#include <assert.h>
#include <brand.h>
#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sortix/fcntl.h>
#include <sortix/mman.h>
#include <sortix/stat.h>
#include <sortix/wait.h>

#include <sortix/kernel/copy.h>
#include <sortix/kernel/decl.h>
#include <sortix/kernel/descriptor.h>
#include <sortix/kernel/dtable.h>
#include <sortix/kernel/fcache.h>
#include <sortix/kernel/inode.h>
#include <sortix/kernel/interrupt.h>
#include <sortix/kernel/ioctx.h>
#include <sortix/kernel/keyboard.h>
#include <sortix/kernel/kthread.h>
#include <sortix/kernel/log.h>
#include <sortix/kernel/memorymanagement.h>
#include <sortix/kernel/mtable.h>
#include <sortix/kernel/panic.h>
#include <sortix/kernel/pci.h>
#include <sortix/kernel/process.h>
#include <sortix/kernel/ptable.h>
#include <sortix/kernel/random.h>
#include <sortix/kernel/refcount.h>
#include <sortix/kernel/scheduler.h>
#include <sortix/kernel/signal.h>
#include <sortix/kernel/string.h>
#include <sortix/kernel/textbuffer.h>
#include <sortix/kernel/thread.h>
#include <sortix/kernel/time.h>
#include <sortix/kernel/user-timer.h>
#include <sortix/kernel/video.h>
#include <sortix/kernel/vnode.h>
#include <sortix/kernel/worker.h>

#include "com.h"
#include "disk/ahci/ahci.h"
#include "disk/ata/ata.h"
#include "fs/full.h"
#include "fs/kram.h"
#include "fs/null.h"
#include "fs/random.h"
#include "fs/zero.h"
#include "gpu/bga/bga.h"
#include "initrd.h"
#include "kb/default-kblayout.h"
#include "kb/kblayout.h"
#include "kb/ps2.h"
#include "logterminal.h"
#include "mouse/ps2.h"
#include "multiboot.h"
#include "net/em/em.h"
#include "net/fs.h"
#include "net/lo/lo.h"
#include "net/ping.h"
#include "net/tcp.h"
#include "net/udp.h"
#include "poll.h"
#include "pty.h"
#include "uart.h"
#include "vga.h"

#if defined(__i386__) || defined(__x86_64__)
#include "x86-family/cmos.h"
#include "x86-family/float.h"
#include "x86-family/gdt.h"
#include "x86-family/ps2.h"
#include "x86-family/vbox.h"
#endif

// Keep the stack size aligned with $CPU/boot.s
const size_t STACK_SIZE = 64*1024;
extern "C" { __attribute__((aligned(16))) size_t stack[STACK_SIZE / sizeof(size_t)]; }

namespace Sortix {

// Forward declarations.
static void BootThread(void* user);
static void InitThread(void* user);
static void SystemIdleThread(void* user);

static int argc;
static char** argv;
static multiboot_info_t* bootinfo;
static bool enable_em = true;
static bool enable_network_drivers = true;

static char* cmdline_tokenize(char** saved)
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

extern "C" void KernelInit(unsigned long magic, multiboot_info_t* bootinfo_p)
{
	(void) magic;
	bootinfo = bootinfo_p;

	//
	// Stage 1. Initialization of Early Environment.
	//

	// TODO: Call global constructors using the _init function.

	// Detect available physical memory.
	Memory::Init(bootinfo);

	// Initialize randomness from the random seed if provided.
	Random::Init(bootinfo);

	// Initialize the kernel log.
	Log::Init(bootinfo);

	// Display the logo.
	Log::PrintF("\e[37;41m\e[2J");
	Log::Center(BRAND_LOGO);

	char* cmdline = NULL;
	if ( bootinfo->flags & MULTIBOOT_INFO_CMDLINE && bootinfo->cmdline )
	{
		addr_t physical_from = Page::AlignDown(bootinfo->cmdline);
		size_t offset = bootinfo->cmdline - physical_from;
		size_t desired = 16 * Page::Size();
		size_t mapped = offset + desired;
		addralloc_t alloc;
		if ( !AllocateKernelAddress(&alloc, mapped) )
			Panic("Failed to allocate virtual space for command line");
		for ( size_t i = 0; i < mapped; i += Page::Size() )
		{
			if ( !Memory::Map(physical_from + i, alloc.from + i, PROT_KREAD) )
				Panic("Failed to memory map command line");
		}
		char* bootloader_cmdline = (char*) (alloc.from + offset);
		size_t cmdline_length = strnlen(bootloader_cmdline, desired);
		if ( desired <= cmdline_length )
			Panic("Kernel command line is too long");
		if ( !(cmdline = strdup(bootloader_cmdline)) )
			Panic("Failed to strdup command line");
		for ( size_t i = 0; i < mapped; i += Page::Size() )
			Memory::Unmap(alloc.from + i);
		Memory::Flush();
		FreeKernelAddress(&alloc);
	}

	int argmax = 1;
	argv = new char*[argmax + 1];
	if ( !argv )
		Panic("Failed to allocate kernel command line");

	char* arg_saved = cmdline;
	char* arg;
	while ( (arg = cmdline_tokenize(&arg_saved)) )
	{
		if ( argc == argmax )
		{
			argmax = argmax ? 2 * argmax : 8;
			char** new_argv = new char*[argmax + 1];
			if ( !new_argv )
				Panic("Failed to allocate kernel command line");
			for ( int i = 0; i < argc; i++ )
				new_argv[i] = argv[i];
			argv = new_argv;
		}
		argv[argc++] = arg;
	}
	argv[argc] = NULL;

	bool no_random_seed = false;
	for ( int i = 0; i < argc; i++ )
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
			default:
				Log::PrintF("\r\e[J");
				Log::PrintF("kernel: fatal: unknown option -- '%c'\n", c);
				HaltKernel();
			}
		}
		else if ( !strcmp(arg, "--disable-em") )
			enable_em = false;
		else if ( !strcmp(arg, "--enable-em") )
			enable_em = true;
		else if ( !strcmp(arg, "--disable-network-drivers") )
			enable_network_drivers = false;
		else if ( !strcmp(arg, "--enable-network-drivers") )
			enable_network_drivers = true;
		else if ( !strcmp(arg, "--no-random-seed") )
			no_random_seed = true;
		else
		{
			Log::PrintF("\r\e[J");
			Log::PrintF("kernel: fatal: unrecognized option '%s'\n", arg);
			HaltKernel();
		}
	}

	compact_arguments(&argc, &argv);

	if ( argc == 0 )
	{
		argv[argc++] = (char*) "/sbin/init";
		argv[argc] = NULL;
	}

	// Initialize the interrupt handler table and enable interrupts.
	Interrupt::Init();

	// Initialize the clocks.
	Time::Init();

	// Initialize the real-time clock.
	CMOS::Init();

	// Check a random seed was provided, or try to fallback and warn.
	int random_status = Random::GetFallbackStatus();
	if ( random_status )
	{
		if ( no_random_seed )
		{
			// There's not much we can if this is an initial boot.
		}
		else if ( random_status == 1 )
		{
			Log::PrintF("kernel: warning: No random seed file was loaded\n");
			Log::PrintF("kernel: warning: With GRUB, try: "
			            "module /boot/random.seed --random-seed\n");
		}
		else
		{
			Log::PrintF("kernel: warning: The random seed file is too small\n");
		}
	}

	//
	// Stage 2. Transition to Multithreaded Environment
	//

	// Initialize Unix Signals.
	Signal::Init();

	// Now that the base system has been loaded, it's time to go threaded. First
	// we create an object that represents this process.
	Ref<ProcessTable> ptable(new ProcessTable());
	if ( !ptable )
		Panic("Could not allocate the process table");
	Process* system = new Process;
	if ( !system )
		Panic("Could not allocate the system process");
	if ( (system->pid = (system->ptable = ptable)->Allocate(system)) < 0 )
		Panic("Could not allocate the system process a pid");
	ptable.Reset();
	system->addrspace = Memory::GetAddressSpace();
	system->group = system;
	system->groupprev = NULL;
	system->groupnext = NULL;
	system->groupfirst = system;
	system->session = system;
	system->sessionprev = NULL;
	system->sessionnext = NULL;
	system->sessionfirst = system;

	if ( !(system->program_image_path = String::Clone("<kernel process>")) )
		Panic("Unable to clone string for system process name");

	// We construct this thread manually for bootstrap reasons. We wish to
	// create a kernel thread that is the current thread and isn't put into the
	// scheduler's set of runnable threads, but rather run whenever there is
	// _nothing_ else to run on this CPU.
	Thread* idlethread = new Thread();
	idlethread->name = "idle";
	idlethread->process = system;
	idlethread->kernelstackpos = (addr_t) stack;
	idlethread->kernelstacksize = STACK_SIZE;
	idlethread->kernelstackmalloced = false;
	system->firstthread = idlethread;
	system->threads_not_exiting_count = 1;
	Scheduler::SetIdleThread(idlethread);

	// Let's create a regular kernel thread that can decide what happens next.
	// Note that we don't do the work here: if all other threads are not running
	// and this thread isn't runnable, then there is nothing to run. Therefore
	// we must become the system idle thread.
	RunKernelThread(BootThread, NULL, "boot");

	// The time driver will run the scheduler on the next timer interrupt.
	Time::Start();

	// Become the system idle thread.
	SystemIdleThread(NULL);
}

static void SystemIdleThread(void* /*user*/)
{
	// Alright, we are now the system idle thread. If there is nothing to do,
	// then we are run. Note that we must never do any real work here as the
	// idle thread must always be runnable.
	while ( true )
	{
#if defined(__i386__) || defined(__x86_64__)
		asm volatile ("hlt");
#else
#warning "Implement a power efficient kernel idle thread"
#endif
	}
}

static void BootThread(void* /*user*/)
{
	//
	// Stage 3. Spawning Kernel Worker Threads.
	//

	// Hello, threaded world! You can now regard the kernel as a multi-threaded
	// process with super-root access to the system. Before we boot the full
	// system we need to start some worker threads.

	// Spawn worker threads to asyncronously draw the console thread.
	TextBuffer* textbuf = Log::device_textbufhandle->Acquire();
	if ( textbuf )
	{
		textbuf->SpawnThreads();
		Log::device_textbufhandle->Release(textbuf);
	}

	// Let's create the interrupt worker thread that executes additional work
	// requested by interrupt handlers, where such work isn't safe.
	Interrupt::interrupt_worker_thread =
		RunKernelThread(Interrupt::WorkerThread, NULL, "interrupt");
	if ( !Interrupt::interrupt_worker_thread )
		Panic("Could not create interrupt worker");

	// Initialize the worker thread data structures.
	Worker::Init();

	// Create a general purpose worker thread.
	Thread* worker_thread = RunKernelThread(Worker::Thread, NULL, "worker");
	if ( !worker_thread )
		Panic("Unable to create general purpose worker thread");

	//
	// Stage 4. Initialize the Filesystem
	//

	// Bring up the filesystem cache.
	FileCache::Init();

	Ref<DescriptorTable> dtable(new DescriptorTable());
	if ( !dtable )
		Panic("Unable to allocate descriptor table");
	Ref<MountTable> mtable(new MountTable());
	if ( !mtable )
		Panic("Unable to allocate mount table.");
	CurrentProcess()->BootstrapTables(dtable, mtable);

	// Let's begin preparing the filesystem.
	// TODO: Setup the right device id for the KRAMFS dir?
	Ref<Inode> iroot(new KRAMFS::Dir((dev_t) 0, (ino_t) 0, 0, 0, 0755));
	if ( !iroot )
		Panic("Unable to allocate root inode.");
	ioctx_t ctx; SetupKernelIOCtx(&ctx);
	Ref<Vnode> vroot(new Vnode(iroot, Ref<Vnode>(NULL), 0, 0));
	if ( !vroot )
		Panic("Unable to allocate root vnode.");
	Ref<Descriptor> droot(new Descriptor(vroot, O_SEARCH));
	if ( !droot )
		Panic("Unable to allocate root descriptor.");
	CurrentProcess()->BootstrapDirectories(droot);

	// Initialize the root directory.
	if ( iroot->link_raw(&ctx, ".", iroot) != 0 )
		Panic("Unable to link /. to /");
	if ( iroot->link_raw(&ctx, "..", iroot) != 0 )
		Panic("Unable to link /.. to /");

	// Extract the initrds.
	if ( bootinfo->mods_count == 0 )
		Panic("No initrd was loaded");
	ExtractModules(bootinfo, droot);

	//
	// Stage 5. Loading and Initializing Core Drivers.
	//

	// Get a descriptor for the /dev directory so we can populate it.
	if ( droot->mkdir(&ctx, "dev", 0775) != 0 && errno != EEXIST )
		Panic("Unable to create RAM filesystem /dev directory.");
	Ref<Descriptor> slashdev = droot->open(&ctx, "dev", O_READ | O_DIRECTORY);
	if ( !slashdev )
		Panic("Unable to create descriptor for RAM filesystem /dev directory.");

	// Initialize the keyboard.
	PS2Keyboard* keyboard = new PS2Keyboard();
	if ( !keyboard )
		Panic("Could not allocate PS2 Keyboard driver");
	KeyboardLayoutExecutor* kblayout = new KeyboardLayoutExecutor;
	if ( !kblayout )
		Panic("Could not allocate keyboard layout executor");
	if ( !kblayout->Upload(default_kblayout, sizeof(default_kblayout)) )
		Panic("Could not load the default keyboard layout into the executor");

	// Initialize the mouse.
	PS2Mouse* mouse = new PS2Mouse();
	if ( !mouse )
		Panic("Could not allocate PS2 Mouse driver");

	// Initialize the PS/2 controller.
	PS2::Init(keyboard, mouse);

	// Register /dev/tty as the current-terminal factory.
	Ref<Inode> tty(new DevTTY(slashdev->dev, 0666, 0, 0));
	if ( !tty )
		Panic("Could not allocate a kernel terminal factory");
	if ( LinkInodeInDir(&ctx, slashdev, "tty", tty) != 0 )
		Panic("Unable to link /dev/tty to kernel terminal factory.");

	// Register the psuedo terminal filesystem as /dev/pts.
	pts = Ref<PTS>(new PTS(0755, 0, 0));
	if ( !pts )
		Panic("Could not allocate a psuedo terminal filesystem");
	if ( slashdev->mkdir(&ctx, "pts", 0755) < 0 )
		Panic("Could not mkdir /dev/pts");
	Ref<Descriptor> ptsdir =
		slashdev->open(&ctx, "pts", O_DIRECTORY | O_READ);
	if ( !ptsdir )
		Panic("Could not open /dev/pts");
	if ( !mtable->AddMount(ptsdir->ino, ptsdir->dev, pts, true) )
		Panic("Could not mount pseudo terminal filesystem on /dev/pts");
	if ( slashdev->symlink(&ctx, "pts/ptmx", "ptmx") < 0 )
		Panic("Could not symlink /dev/ptmx -> pts/ptmx");

	// Register the kernel terminal as /dev/tty1.
	Ref<Inode> tty1(new LogTerminal(slashdev->dev, 0666, 0, 0,
	                keyboard, kblayout, "tty1"));
	if ( !tty1 )
		Panic("Could not allocate a kernel terminal");
	if ( LinkInodeInDir(&ctx, slashdev, "tty1", tty1) != 0 )
		Panic("Unable to link /dev/tty1 to kernel terminal.");

	// Register the mouse as /dev/mouse.
	Ref<Inode> mousedev(new PS2MouseDevice(slashdev->dev, 0666, 0, 0, mouse));
	if ( !mousedev )
		Panic("Could not allocate a mouse device");
	if ( LinkInodeInDir(&ctx, slashdev, "mouse", mousedev) != 0 )
		Panic("Unable to link /dev/mouse to mouse.");

	// Register the null device as /dev/null.
	Ref<Inode> null_device(new Null(slashdev->dev, (ino_t) 0, (uid_t) 0,
	                                (gid_t) 0, (mode_t) 0666));
	if ( !null_device )
		Panic("Could not allocate a null device");
	if ( LinkInodeInDir(&ctx, slashdev, "null", null_device) != 0 )
		Panic("Unable to link /dev/null to the null device.");

	// Register the zero device as /dev/zero.
	Ref<Inode> zero_device(new Zero(slashdev->dev, (ino_t) 0, (uid_t) 0,
	                                (gid_t) 0, (mode_t) 0666));
	if ( !zero_device )
		Panic("Could not allocate a zero device");
	if ( LinkInodeInDir(&ctx, slashdev, "zero", zero_device) != 0 )
		Panic("Unable to link /dev/zero to the zero device.");

	// Register the full device as /dev/full.
	Ref<Inode> full_device(new Full(slashdev->dev, (ino_t) 0, (uid_t) 0,
	                                (gid_t) 0, (mode_t) 0666));
	if ( !full_device )
		Panic("Could not allocate a full device");
	if ( LinkInodeInDir(&ctx, slashdev, "full", full_device) != 0 )
		Panic("Unable to link /dev/full to the full device.");

	// Register the random device as /dev/random.
	Ref<Inode> random_device(new DevRandom(slashdev->dev, (ino_t) 0, (uid_t) 0,
	                                       (gid_t) 0, (mode_t) 0666));
	if ( !random_device )
		Panic("Could not allocate a random device");
	if ( LinkInodeInDir(&ctx, slashdev, "random", random_device) != 0 )
		Panic("Unable to link /dev/random to the random device.");
	if ( LinkInodeInDir(&ctx, slashdev, "urandom", random_device) != 0 )
		Panic("Unable to link /dev/urandom to the random device.");

	// Initialize the COM ports.
	COM::Init("/dev", slashdev);

	// Initialize the VGA driver.
	VGA::Init("/dev", slashdev);

	// Search for PCI devices and load their drivers.
	PCI::Init();

#if defined(__i386__) || defined(__x86_64__)
	// Initialize the VirtualBox Guest Additions.
	VBox::Init();
#endif

	// Initialize AHCI devices.
	AHCI::Init("/dev", slashdev);

	// Initialize ATA devices.
	ATA::Init("/dev", slashdev);

	// Initialize the BGA driver.
	BGA::Init();

	// Initialize the filesystem network.
	NetFS::Init();

	// Initialize the ping protocol.
	Ping::Init();

	// Initialize the TCP.
	TCP::Init();

	// Initialize the UDP.
	UDP::Init();

	// Initialize the loopback driver.
	Loopback::Init("/dev", slashdev);

	// Initialize the network drivers.
	if ( enable_network_drivers )
	{
		// Initialize the EM driver.
		if ( enable_em )
			EM::Init("/dev", slashdev);
	}

	//
	// Stage 6. Executing Hosted Environment ("User-Space")
	//
	// Finally, let's transfer control to a new kernel process that will
	// eventually run user-space code known as the operating system.
	addr_t initaddrspace = Memory::Fork();
	if ( !initaddrspace ) { Panic("Could not create init's address space"); }

	Process* init = new Process;
	if ( !init )
		Panic("Could not allocate init process");
	if ( (init->pid = (init->ptable = CurrentProcess()->ptable)->Allocate(init)) < 0 )
		Panic("Could not allocate init a pid");

	kthread_mutex_lock(&process_family_lock);
	Process* kernel_process = CurrentProcess();
	init->parent = kernel_process;
	init->nextsibling = kernel_process->firstchild;
	init->prevsibling = NULL;
	if ( kernel_process->firstchild )
		kernel_process->firstchild->prevsibling = init;
	kernel_process->firstchild = init;
	init->group = init;
	init->groupprev = NULL;
	init->groupnext = NULL;
	init->groupfirst = init;
	init->session = init;
	init->sessionprev = NULL;
	init->sessionnext = NULL;
	init->sessionfirst = init;
	kthread_mutex_unlock(&process_family_lock);

	// TODO: Why don't we fork from pid=0 and this is done for us?
	// TODO: Fork dtable and mtable, don't share them!
	init->BootstrapTables(dtable, mtable);
	dtable.Reset();
	mtable.Reset();
	init->BootstrapDirectories(droot);
	init->addrspace = initaddrspace;
	Scheduler::SetInitProcess(init);

	Thread* initthread = RunKernelThread(init, InitThread, NULL, "main");
	if ( !initthread )
		Panic("Could not create init thread");

	// Wait until init is done and then shut down the computer.
	int status;
	pid_t pid = CurrentProcess()->Wait(init->pid, &status, 0);
	if ( pid != init->pid )
		PanicF("Waiting for init to exit returned %ji (errno=%i)", (intmax_t) pid, errno);

	status = WEXITSTATUS(status);

	switch ( status )
	{
	case 0:
		CPU::ShutDown();
	case 1:
		CPU::Reboot();
	case 2:
		Log::Print("kernel: fatal: Halting system due to init fatality\n");
		Log::Sync();
		HaltKernel();
	default:
		Log::PrintF("kernel: fatal: init exited with unexpected exit code %i\n",
		            status);
		Log::Sync();
		HaltKernel();
	}
}

static void InitThread(void* /*user*/)
{
	// We are the init process's first thread. Let's load the init program from
	// the init ramdisk and transfer execution to it. We will then become a
	// regular user-space program with root permissions.

	Process* process = CurrentProcess();

	ioctx_t ctx; SetupKernelIOCtx(&ctx);
	Ref<Descriptor> root = CurrentProcess()->GetRoot();

	Ref<DescriptorTable> dtable = process->GetDTable();

	Ref<Descriptor> tty1 = root->open(&ctx, "/dev/tty1", O_READ | O_WRITE);
	if ( !tty1 )
		PanicF("/dev/tty1: %m");
	if ( tty1->ioctl(&ctx, TIOCSCTTY, 0) < 0 )
		PanicF("/dev/tty1: ioctl: TIOCSCTTY: %m");
	tty1.Reset();

	Ref<Descriptor> tty_stdin = root->open(&ctx, "/dev/tty", O_READ);
	if ( !tty_stdin || dtable->Allocate(tty_stdin, 0) != 0 )
		Panic("Could not prepare stdin for initialization process");
	Ref<Descriptor> tty_stdout = root->open(&ctx, "/dev/tty", O_WRITE);
	if ( !tty_stdout || dtable->Allocate(tty_stdout, 0) != 1 )
		Panic("Could not prepare stdout for initialization process");
	Ref<Descriptor> tty_stderr = root->open(&ctx, "/dev/tty", O_WRITE);
	if ( !tty_stderr || dtable->Allocate(tty_stderr, 0) != 2 )
		Panic("Could not prepare stderr for initialization process");

	dtable.Reset();

	const char* initpath = argv[0];
	Ref<Descriptor> init = root->open(&ctx, initpath, O_EXEC | O_READ);
	if ( !init )
		PanicF("Could not open %s in early kernel RAM filesystem:\n%s",
		       initpath, strerror(errno));
	struct stat st;
	if ( init->stat(&ctx, &st) )
		PanicF("Could not stat '%s' in initrd.", initpath);
	assert(0 <= st.st_size);
	if ( (uintmax_t) SIZE_MAX < (uintmax_t) st.st_size )
		PanicF("%s is bigger than SIZE_MAX.", initpath);
	size_t programsize = st.st_size;
	uint8_t* program = new uint8_t[programsize];
	if ( !program )
		PanicF("Unable to allocate 0x%zx bytes needed for %s.", programsize, initpath);
	size_t sofar = 0;
	while ( sofar < programsize )
	{
		ssize_t numbytes = init->read(&ctx, program+sofar, programsize-sofar);
		if ( !numbytes )
			PanicF("Premature EOF when reading %s.", initpath);
		if ( numbytes < 0 )
			PanicF("IO error when reading %s.", initpath);
		sofar += numbytes;
	}

	init.Reset();

	Log::PrintF("\r\e[m\e[J");

	int envc = 1;
	const char* envp[] = { "TERM=sortix", NULL };
	struct thread_registers regs;
	assert((((uintptr_t) &regs) & (alignof(regs)-1)) == 0);

	if ( process->Execute(initpath, program, programsize, argc, argv, envc,
	                       envp, &regs) )
		PanicF("Unable to execute %s.", initpath);

	delete[] program;
	delete[] argv;

	// Now become the init process and the operation system shall run.
	LoadRegisters(&regs);
}

} // namespace Sortix
