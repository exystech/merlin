SOFTWARE_MEANT_FOR_SORTIX=1
include ../build-aux/platform.mak
include ../build-aux/compiler.mak
include ../build-aux/dirs.mak

OPTLEVEL?=$(DEFAULT_OPTLEVEL)
CFLAGS?=$(OPTLEVEL)

CFLAGS += -Wall -Wextra

BINARIES = chkblayout
MANPAGES1 = chkblayout.1

LIBS = -ldisplay

all: $(BINARIES)

.PHONY: all install clean

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	install $(BINARIES) $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp $(MANPAGES1) $(DESTDIR)$(MANDIR)/man1

%: %.c
	$(CC) -std=gnu11 $(CFLAGS) $(CPPFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f $(BINARIES)
