.include <sortix/sys.mk>
.include <dirs.mk>
.include <template/dependencies.mk>
.include <template/headers.mk>

CXXFLAGS+=-ffreestanding -fbuiltin
CPPFLAGS+=-I. -D__is_sortix_kernel

# TODO: HACK (?)
.ifdef PANIC_SHORT
  CPPFLAGS+=-DPANIC_SHORT
.endif

# TODO: HACK
.ifdef CPU_IS_X86
  X86FAMILY=1
  CPUDIR=$(CPU)
  CPUOBJS=$(CPUDIR)/boot.o
.endif

# TODO: HACK
.ifdef CPU_IS_X64
  X86FAMILY=1
  CPUDIR=$(CPU)
  CXXFLAGS+= -mno-red-zone -mno-mmx -mno-sse -mno-sse2
  CPUOBJS=$(CPUDIR)/boot.o
.endif

CPUDIR?=.

# TODO: HACK
.ifdef X86FAMILY
  CPUOBJS+=\
           $(CPUDIR)/memorymanagement.o \
           x86-family/memorymanagement.o \
           $(CPUDIR)/interrupt.o \
           x86-family/interrupt.o \
           x86-family/ioport.o \
           x86-family/pic.o \
           x86-family/gdt.o \
           x86-family/idt.o \
           $(CPUDIR)/syscall.o \
           x86-family/cmos.o \
           x86-family/time.o \
           x86-family/pat.o \
           x86-family/float.o \
           x86-family/ps2.o \
           x86-family/x86-family.o
.endif

CRTI_OBJ=$(CPUDIR)/crti.o
# TODO: It would be nice if these were done lazily, this makefile may be used
#       before the compiler exists.
CRTBEGIN_OBJ!=$(CXX) $(CXXFLAGS) -print-file-name=crtbegin.o 2>/dev/null || true
CRTEND_OBJ!=$(CXX) $(CXXFLAGS) -print-file-name=crtend.o 2>/dev/null || true
CRTN_OBJ=$(CPUDIR)/crtn.o

LDLIBS=-nostdlib -lk -lgcc

OBJS=\
$(CPUOBJS) \
addralloc.o \
alarm.o \
clock.o \
com.o \
copy.o \
$(CPUDIR)/kthread.o \
descriptor.o \
disk/ahci/ahci.o \
disk/ahci/hba.o \
disk/ahci/port.o \
disk/ata/ata.o \
disk/ata/hba.o \
disk/ata/port.o \
disk/node.o \
dtable.o \
elf.o \
fcache.o \
fs/full.o \
fsfunc.o \
fs/kram.o \
fs/null.o \
fs/random.o \
fs/user.o \
fs/util.o \
fs/zero.o \
gpu/bga/bga.o \
hostname.o \
identity.o \
initrd.o \
inode.o \
interlock.o \
interrupt.o \
ioctx.o \
io.o \
kb/kblayout.o \
kb/ps2.o \
kernelinfo.o \
kernel.o \
kthread.o \
lfbtextbuffer.o \
libk.o \
linebuffer.o \
log.o \
logterminal.o \
memorymanagement.o \
mouse/ps2.o \
mtable.o \
net/fs.o \
op-new.o \
panic.o \
partition.o \
pci-mmio.o \
pci.o \
pipe.o \
poll.o \
process.o \
psctl.o \
ptable.o \
random.o \
refcount.o \
registers.o \
resource.o \
scheduler.o \
segment.o \
signal.o \
sockopt.o \
string.o \
syscall.o \
textbuffer.o \
textterminal.o \
thread.o \
time.o \
timer.o \
uart.o \
user-timer.o \
vga.o \
vgatextbuffer.o \
video.o \
vnode.o \
worker.o \

ALLOBJS=$(CRTI_OBJ) $(OBJS) $(CRTN_OBJ) end.o

LINK_OBJECTS=\
$(CRTI_OBJ) $(CRTBEGIN_OBJ) $(OBJS) $(LDLIBS) $(CRTN_OBJ) $(CRTEND_OBJ) end.o

.for obj in $(ALLOBJS)
.  tryinclude "$(obj:.o=.d)"
.endfor

.PHONY: all clean install install-kernel

.DEFAULT_GOAL = all
all: sortix.bin

.ifdef CPU_IS_X86
sortix.bin: $(ALLOBJS)
	$(CXX) $(CXXFLAGS) -Wl,-Ttext -Wl,100000 $(LINK_OBJECTS) -o $@
.endif

.ifdef CPU_IS_X64
sortix-x86_64.bin: $(ALLOBJS)
	$(CXX) $(CXXFLAGS) -Wl,-Ttext -Wl,100000 -Wl,-z -Wl,max-page-size=0x1000 $(LINK_OBJECTS) -o $@

sortix.bin: sortix-x86_64.bin
	$(OBJCOPY) $< -O elf32-i386-sortix $@
.endif

.SUFFIXES: .kblayout .kblayout.bin

.kblayout.kblayout.bin:
	kblayout-compiler --format=sortix-kblayout-1 --compression=none $< -o $@

kb/default-kblayout.h: kb/default-kblayout.kblayout.bin
	carray -cgis --uint8_t --guard=SORTIX_KB_DEFAULT_KBLAYOUT_H --identifier=default_kblayout $< -o $@

vgafont.h: vgafont.f16
	carray -gis --uint8_t --guard=VGAFONT_H --identifier=vgafont $< -o $@

$(ALLOBJS): kb/default-kblayout.h vgafont.h

clean:
	rm -f $(ALLOBJS) sortix.bin
.ifdef CPU_IS_X64
	rm -f sortix-x86_64.bin
.endif
	rm -f $(ALLOBJS:.o=.d)
	rm -f kb/default-kblayout.kblayout.bin kb/default-kblayout.h vgafont.h

install: install-kernel

install-kernel:
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp sortix.bin $(DESTDIR)$(BOOTDIR)/sortix.bin
