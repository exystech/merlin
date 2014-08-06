.include <sortix/version.mk>

CXX += -fno-exceptions -fno-rtti
CFLAGS += -std=gnu11
CXXFLAGS += -std=gnu++11
CPPFLAGS += -DVERSIONSTR=\"$(VERSION)\"

.ifdef HOST_IS_i686-sortix
HOST_IS_SORTIX=1
MACHINE=i686
CPU_IS_X86=1
CPU = x86

.elifdef HOST_IS_x86_64-sortix
HOST_IS_SORTIX=1
MACHINE=x86_64
CPU_IS_X64=1
CPU = x64
.endif

.ifdef HOST_IS_SORTIX
PREFIX?=
.endif

# TODO: IS_LINUX is needed by ext/TIXmakefile
