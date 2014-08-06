SORTIXKERNEL=../kernel

PROGRAM=initrdfs mkinitrd
PROGRAM_BINDIR=$(SBINDIR)
PROGRAM_OBJS=rules.o serialize.o
initrdfs_OBJS=$(PROGRAM_OBJS) initrdfs.o
mkinitrd_OBJS=$(PROGRAM_OBJS) mkinitrd.o
MANPAGE=$(PROGRAM:=.8)

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/man.mk>
.include <template/program.mk>

CPPFLAGS+=-I$(SORTIXKERNEL)/include -I.

# TODO: HACK
.ifndef HOST_IS_SORTIX
  CPPFLAGS+=-D_GNU_SOURCE
.endif
