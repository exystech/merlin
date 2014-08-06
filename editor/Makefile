PROGRAM=editor
PROGRAM_OBJS=\
command.o \
cursor.o \
display.o \
editor.o \
highlight.o \
input.o \
modal.o \
multibyte.o \
terminal.o \

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

# TODO: HACK
.ifndef HOST_IS_SORTIX
  CPPFLAGS+=-D_GNU_SOURCE
.endif
