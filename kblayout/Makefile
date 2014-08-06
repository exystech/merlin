KBLAYOUTDIR?=$(DATAROOTDIR)/kblayout
LAYOUTS=\
dk \
us \

.include <sortix/sys.mk>
.include <dirs.mk>

.PHONY: all install clean

.SUFFIXES: .kblayout

all: $(LAYOUTS)

.kblayout:
	kblayout-compiler --format=sortix-kblayout-1 --compression=none $< -o $@

install: all
	mkdir -p $(DESTDIR)$(KBLAYOUTDIR)
	cp $(LAYOUTS) $(DESTDIR)$(KBLAYOUTDIR)

clean:
	rm -f $(LAYOUTS)
