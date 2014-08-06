PROGRAM=update-initrd
MANPAGE=update-initrd.8

.PHONY: all clean install install-bin

.include <sortix/sys.mk>
.include <dirs.mk>
.include <template/man.mk>

clean:

install: install-bin

install-bin:
	mkdir -p $(DESTDIR)$(SBINDIR)
	install $(PROGRAM) $(DESTDIR)$(SBINDIR)
