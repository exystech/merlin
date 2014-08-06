DOCUMENTS:=\
cross-compilation-sins \
obsolete-stuff \
porting-guide \
posix-divergence \
welcome \

.include <sortix/sys.mk>
.include <dirs.mk>

all:

.PHONY: all install clean

install: all
	mkdir -p $(DESTDIR)$(DOCDIR)
	cp -t $(DESTDIR)$(DOCDIR) $(DOCUMENTS)

clean:
