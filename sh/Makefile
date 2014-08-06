PROGRAM=sh proper-sh
proper-sh_OBJS=proper-sh.o
sh_OBJS=\
editline.o \
sh.o \
showline.o \
util.o \

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

install-bin:
	mkdir -p $(DESTDIR)$(BINDIR)
	install sh $(DESTDIR)$(BINDIR)/sortix-sh
	install proper-sh $(DESTDIR)$(BINDIR)/sh
