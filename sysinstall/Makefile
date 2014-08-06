MANPAGE=sysinstall.8 sysmerge.8 sysupgrade.8
PROGRAM=sysinstall sysmerge sysupgrade
PROGRAM_BINDIR=$(SBINDIR)
sysinstall_OBJS=sysinstall.o devices.o execute.o fileops.o interactive.o manifest.o
sysmerge_OBJS=sysmerge.o conf.o fileops.o execute.o manifest.o release.o
sysupgrade_OBJS=sysupgrade.o conf.o devices.o execute.o fileops.o interactive.o manifest.o release.o
LDLIBS+=-lmount

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>
.include <template/man.mk>
