include mk/sortix/version.mk
include mk/dirs.mk

TARGET_PREFIX_CMD:=`[ -n "$(TARGET)" ] && echo "$(TARGET)-"`
TARGET_PREFIX?=$(TARGET_PREFIX_CMD)

BUILD?=
HOST?=$(BUILD)
TARGET?=$(HOST)

SHAREMKDIR?=$(DATADIR)/$(TARGET_PREFIX)mk

PROGRAM=$(TARGET_PREFIX)make
PROGRAM_OBJS=\
bstree.o \
data.o \
disambiguate.o \
execute.o \
import.o \
make.o \
rule.o \
signal.o \
util.o \
variable.o \

CFLAGS += -std=c11
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_ALL_SOURCE -D_GNU_SOURCE -D_BSD_SOURCE -D_DARWIN_C_SOURCE
CPPFLAGS += -DVERSIONSTR=\"$(VERSION)\" -DSHAREMKDIR=\"$(SHAREMKDIR)\"

.PHONY: all install clean

all: make

make: $(PROGRAM_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(PROGRAM_OBJS) -o make $(LDLIBS)

$(PROGRAM_OBJS): $(PROGRAM_OBJS:.o=.h)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	install make $(DESTDIR)$(BINDIR)/$(PROGRAM)
	mkdir -p $(DESTDIR)$(SHAREMKDIR)
	cp -R mk/. $(DESTDIR)$(SHAREMKDIR)/.
	if [ -n "$(HOST)" ]; then \
	  echo "BUILD = $(HOST)" > $(DESTDIR)$(SHAREMKDIR)/build.mk; \
	else \
	  echo "BUILD = $$($(CC) $(CFLAGS) -dumpmachine)" > $(DESTDIR)$(SHAREMKDIR)/build.mk; \
	fi
	if [ -n "$(TARGET)" ]; then \
	  echo "HOST = $(TARGET)" > $(DESTDIR)$(SHAREMKDIR)/host.mk; \
	else \
	  echo "HOST = $$($(CC) $(CFLAGS) -dumpmachine)" > $(DESTDIR)$(SHAREMKDIR)/host.mk; \
	fi

clean:
	rm -f make
	rm -f $(PROGRAM_OBJS)
	rm -f $(PROGRAM_OBJS:.o=.d)
	rm -f -- *make
