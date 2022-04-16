SOFTWARE_MEANT_FOR_SORTIX=1
include ../build-aux/platform.mak
include ../build-aux/compiler.mak
include ../build-aux/version.mak
include ../build-aux/dirs.mak

OPTLEVEL?=-g -O2
CXXFLAGS?=$(OPTLEVEL)

CXXFLAGS:=$(CXXFLAGS) -std=gnu++11 -Wall -Wextra -fno-exceptions -fno-rtti -fcheck-new

BINARY:=trianglix

LIBS:=-ldispd

all: $(BINARY)

.PHONY: all install uninstall clean

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	install $(BINARY) $(DESTDIR)$(BINDIR)

%: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f $(BINARY) *.o
