PROGRAM=\
porttix-create \
srctix-create \
tix \
tix-build \
tix-collection \
tix-execdiff \
tix-execpatch \
tix-install \
tix-rmpatch \

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

# TODO: HACK
.ifndef HOST_IS_SORTIX
  CPPFLAGS+=-D_GNU_SOURCE
.endif

.PHONY: install install-bin-extra

install: install-bin-extra

install-bin-extra: install-bin
	install tix-eradicate-libtool-la $(DESTDIR)$(BINDIR)
