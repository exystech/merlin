# template/headers.mk
# Adds an include/ directory to CPPFLAGS and installs into the system directory.

.PHONY: headers install install-headers

HEADERS ?= include
.for headers in $(HEADERS)
# TODO: There's no good way to do this at the moment. I need to implement a
#       modified like :S/^/-I/ that adds something at the beginning of a word so
#       I can do this at once, instead with a loop.
CPPFLAGS +:= -I$(headers)
.endfor

headers:

install: install-headers

install-headers: headers
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
.for headers in $(HEADERS)
	cp -RT --preserve=mode,timestamp,links $(headers) $(DESTDIR)$(INCLUDEDIR)
.endfor
