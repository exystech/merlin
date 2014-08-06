.tryinclude <build.mk>
.tryinclude <host.mk>

.ifdef BUILD
BUILD_IS_$(BUILD) = 1
.endif
.ifdef HOST
HOST_IS_$(HOST) = 1
.endif

.ifdef HOST
.ifndef BUILD_IS_$(HOST)
AS ?= $(HOST)-as
AR ?= $(HOST)-ar
# TODO: Ensure that $(HOST)-cc exists according to cross-development(7).
CC ?= $(HOST)-gcc
CXX ?= $(HOST)-g++
OBJCOPY ?= $(HOST)-objcopy
.endif
.endif

AS ?= as
ASFLAGS ?=
COMPILE.s ?= $(AS) $(ASFLAGS)
LINK.s ?= $(LINK.c)
COMPILE.S ?= $(COMPILE.c)
LINK.S ?= $(LINK.c)

AR ?= ar
ARFLAGS ?= -cr

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra
CPPFLAGS ?=
COMPILE.c ?= $(CC) $(CFLAGS) $(CPPFLAGS) -c
LINK.c ?= $(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

CXX ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra
COMPILE.c++ ?= $(CXX) $(CXXFLAGS) $(CPPFLAGS) -c
LINK.c++ ?= $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS)
COMPILE.cpp ?= $(COMPILE.c++)
LINK.cpp ?= $(LINK.c++)

LEX ?= lex
LFLAGS ?=
LEX.l ?= $(LEX) $(LFLAGS)

OBJCOPY ?= objcopy

YACC ?= yacc
YFLAGS ?=
YACC.y ?= $(YACC) $(YFLAGS)

LDFLAGS ?=

.SUFFIXES: .o .c .c++ .cpp .y .l .a .sh .f .s .S

.ifdef SYSROOT
CC += --sysroot $(SYSROOT)
CXX += --sysroot $(SYSROOT)
LD += --sysroot $(SYSROOT)
.endif

# C
.c:
	$(LINK.c) $< -o $@ $(LDLIBS)
.c.o:
	$(COMPILE.c) $< -o $@
.c.a:
	$(COMPILE.c) $< -o $*.o
	$(AR) $(ARFLAGS) $@ $*.o
	rm -f $*.o

# C++
.c++:
	$(LINK.c++) $< -o $@ $(LDLIBS)
.c++.o:
	$(COMPILE.c++) $< -o $@
.c++.a:
	$(COMPILE.c++) $< -o $*.o
	$(AR) $(ARFLAGS) $@ $*.o
	rm -f $*.o
.cpp:
	$(LINK.c++) $< -o $@ $(LDLIBS)
.cpp.o:
	$(COMPILE.c++) $< -o $@
.cpp.a:
	$(COMPILE.c++) $< -o $*.o
	$(AR) $(ARFLAGS) $@ $*.o
	rm -f $*.o

# TODO: The temporary lex.yy.c file isn't safe for concurrent builds!
# Lex
.l:
	$(LEX.l) $<
	$(LINK.c) lex.yy.c -o $@ $(LDLIBS)
	rm -f lex.yy.c
.l.o:
	$(LEX.l) $<
	$(COMPILE.c) lex.yy.c -o $@
	rm -f lex.yy.c
.l.c:
	$(LEX.l) $<
	mv lex.yy.c $@

# Assembly.
.s:
	$(LINK.s) $< -o $@  $(LDLIBS)
.s.o:
	$(COMPILE.s) $< -o $@
.s.a:
	$(COMPILE.s) $< -o $*.o
	$(AR) $(ARFLAGS) $@ $*.o
	rm -f $*.o

# Assembly (preprocessed).
.S:
	$(LINK.S) $< -o $@  $(LDLIBS)
.S.o:
	$(COMPILE.S) $< -o $@
.S.a:
	$(COMPILE.S) $< -o $*.o
	$(AR) $(ARFLAGS) $@ $*.o
	rm -f $*.o

# Shell
.sh:
	cp $< $@
	chmod a+x $@

# TODO: The temporary y.tab.c file isn't safe for concurrent builds!
# Yacc
.y:
	$(YACC.y) $<
	$(LINK.c) y.tab.c -o $@ $(LDLIBS)
	rm -f y.tab.c
.y.o:
	$(YACC.y) $<
	$(COMPILE.c) y.tab.c -o $@
	rm -f y.tab.c
.y.c:
	$(YACC.y) $<
	mv y.tab.c $@
