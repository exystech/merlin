PROGRAM=login
MANPAGE=login.8
PROGRAM_BIN=$(SBINDIR)
PROGRAM_OBJS=\
framebuffer.o \
graphical.o \
login.o \
pixel.o \
vgafont.o \

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/man.mk>
.include <template/program.mk>

arrow.inc: arrow.rgb
	carray -cs --identifier=arrow arrow.rgb -o $@

$(PROGRAM_OBJS): arrow.inc

clean: clean-inc

.PHONY: clean-inc
clean-inc:
	rm -f arrow.inc
