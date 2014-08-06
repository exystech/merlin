# template/man.mk
# Manual installation targets.

# Provides:
#   all target
#   install target

# Usage (install hello.1 and strfoo.3):
#   MANPAGE=hello.1 strfoo.3
#   .include <template/man.mk>

.include <dirs.mk>

all: $(MANPAGE)

.PHONY: install-man

install: install-man

install-man: $(MANPAGE)
# TODO: This may make the same dir multiple times.
.for page in $(MANPAGE)
	mkdir -p $(DESTDIR)$(MANDIR)/man$(page:E)
	cp $(page) $(DESTDIR)$(MANDIR)/man$(page:E)/$(page:T)
.endfor
