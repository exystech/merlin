PROGRAM=carray

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

# TODO: HACK
.ifndef HOST_IS_SORTIX
  CPPFLAGS+=-D_GNU_SOURCE
.endif
