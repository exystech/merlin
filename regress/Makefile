TESTDIR?=$(LIBEXECDIR)/test

regress_BINDIR=$(BINDIR)
PROGRAM_BINDIR=$(TESTDIR)
PROGRAM=\
regress \
test-fmemopen \
test-pthread-argv \
test-pthread-basic \
test-pthread-main-join \
test-pthread-once \
test-pthread-self \
test-pthread-tls \
test-signal-raise \

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>
