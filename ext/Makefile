PROGRAM=extfs
PROGRAM_LANG=cpp
PROGRAM_OBJS=\
block.o \
blockgroup.o \
device.o \
extfs.o \
filesystem.o \
fsmarshall.o \
fuse.o \
inode.o \

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

.ifdef IS_LINUX
  CXXFLAGS+=-pthread
  CPPFLAGS+=-D_FILE_OFFSET_BITS=64
  LDLIBS+=-lfuse
.endif
