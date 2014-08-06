# template/program.mk
# Make one or more program from one or more source files.

# Provides:
#   all target (default)
#   install target
#   clean target
#   header dependency tracking (if -MD)

# Usage (hello made from hello.c):
#   PROGRAM=hello
#   PROGRAM_LANG=c # (defaults to c)
#   .include <template/program.mk>

# Usage (hello from hello.c, and goodbye from goodbye.c):
#   PROGRAM=hello goodbye
#   PROGRAM_LANG=c # (defaults to c)
#   .include <template/program.mk>

# Usage (advanced):
#   PROGRAM=hello goodbye
#   PROGRAM_LANG=c # (defaults to C)
#   hello_OBJ=hel.o lo.o
#   hello_LANG=c # (defaults to $(PROGRAM_LANG))
#   goodbye_OBJ=good.o bye.o
#   goodbye_LANG=c++ # (defaults to $(PROGRAM_LANG))
#   .include <template/program.mk>

.include <dirs.mk>

.ifdef PROGRAM_OBJS
.  for program in $(PROGRAM)
.    ifndef $(program)_OBJS
       $(program)_OBJS = $(PROGRAM_OBJS)
.    endif
.  endfor
.endif

PROGRAM_LANG ?= c
.for program in $(PROGRAM)
   $(program)_LANG ?= $(PROGRAM_LANG)
.endfor

PROGRAM_BINDIR ?= $(BINDIR)
.for program in $(PROGRAM)
   $(program)_BINDIR ?= $(PROGRAM_BINDIR)
.endfor

.DEFAULT_GOAL ?= all

.PHONY: all clean clean-bin install install-bin

all: $(PROGRAM)

.for program in $(PROGRAM)
.  ifdef $(program)_OBJS
$(program): $($(program)_OBJS)
	$(LINK.$($(program)_LANG)) -MD $($(program)_OBJS) -o $@ $(LDLIBS)
.  else
$(program): $(program).$($(program)_LANG)
	$(LINK.$($(program)_LANG)) -MD $(program).$($(program)_LANG) -o $@ $(LDLIBS)
.  endif
.endfor

install: install-bin

install-bin: $(PROGRAM)
.for program in $(PROGRAM)
	mkdir -p $(DESTDIR)$($(program)_BINDIR)
	install $(program) $(DESTDIR)$($(program)_BINDIR)
.endfor

clean: clean-bin

clean-bin:
.for program in $(PROGRAM)
.  ifdef $(program)_OBJS
	rm -f $(program) $($(program)_OBJS) $($(program)_OBJS:.o=.d)
.  else
	rm -f $(program) $(program).d
.  endif
.endfor

.for program in $(PROGRAM)
.  ifdef $(program)_OBJS
.    for obj in $($(program)_OBJS)
.      tryinclude "$(obj:.o=.d)"
.    endfor
.  else
.    tryinclude "$(program).d"
.  endif
.endfor
