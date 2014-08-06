PROGRAM=trianglix
PROGRAM_LANG=cpp
LDLIBS+=-ldispd

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

.ifdef CPU_IS_X86
CXXFLAGS+=-msse -msse2
.endif
