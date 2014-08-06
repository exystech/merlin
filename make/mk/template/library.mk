# template/library.mk
# Make one or more library from one or more source files.

# Provides:
#   all target (default)
#   install target
#   clean target
#   header dependency tracking (if -MD)

# Usage (libfoo.a made from foo.c and foofoo.c):
#   LIBRARY=libfoo
#   LIBRARY_OBJS=foo.o foofoo.o
#   PROGRAM_LANG=c # (defaults to c)
#   .include <template/library.mk>

# Usage (libfoo.a from libfoo.c, and libbar.a from libbar.c):
#   LIBRARY=libfoo libbar
#   PROGRAM_LANG=c # (defaults to c)
#   libfoo_OBJS=libfoo.o
#   libbar_OBJS=libbar.o
#   .include <template/library.mk>

# Usage (advanced):
#   LIBRARY=libfoo libbar
#   PROGRAM_LANG=c # (defaults to C)
#   libfoo_OBJ=hel.o lo.o
#   libfoo_LANG=c # (defaults to $(PROGRAM_LANG))
#   libbar_OBJ=good.o bye.o
#   libbar_LANG=c++ # (defaults to $(PROGRAM_LANG))
#   .include <template/library.mk>

.include <dirs.mk>

.ifdef LIBRARY_OBJS
.  for library in $(LIBRARY)
.    ifndef $(library)_OBJS
       $(library)_OBJS = $(LIBRARY_OBJS)
.    endif
.  endfor
.endif

PROGRAM_LANG ?= c
.for library in $(LIBRARY)
   $(library)_LANG ?= $(PROGRAM_LANG)
.endfor

.DEFAULT_GOAL ?= all

.PHONY: all clean clean-lib install install-lib

all: $(LIBRARY:=.a)

.for library in $(LIBRARY)
.  ifdef $(library)_OBJS
$(library).a: $($(library)_OBJS)
	$(AR) $(ARFLAGS) $@ $(LDLIBS) $($(library)_OBJS)
.  else
$(library).a: $(library).$($(library)_LANG)
	$(COMPILE.$(library)_LANG) $(library).$($(library)_LANG) -o $(library).o
	$(AR) $(ARFLAGS) $@ $(library).o
	rm -f $(library).a
.  endif
.endfor

install: install-lib

install-lib: $(LIBRARY:=.a)
	mkdir -p $(DESTDIR)$(LIBDIR)
.for library in $(LIBRARY)
	install $(library).a $(DESTDIR)$(LIBDIR)
.endfor

clean: clean-lib

clean-lib:
.for library in $(LIBRARY)
.  ifdef $(library)_OBJS
	rm -f $(library).a $($(library)_OBJS) $($(library)_OBJS:.o=.d)
.  else
	rm -f $(library).a $(library).o $(library).d
.  endif
.endfor

.for library in $(LIBRARY)
.  ifdef $(library)_OBJS
.    for obj in $($(library)_OBJS)
.      tryinclude "$(obj:.o=.d)"
.    endfor
.  else
.    tryinclude "$(library).d"
.  endif
.endfor
