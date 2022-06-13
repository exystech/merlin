#!/bin/sh
set -e

make_dir_path_absolute() {
  (cd "$1" && pwd)
}

has_command() {
  which "$1" > /dev/null
}

# Determine what's supposed to happen.
if [ $# = 0 ]; then
  echo "$0: usage: $0 <operation>" >&2
  exit 1
fi
OPERATION="$1"
case "$OPERATION" in
download|extract|build) ;;
*) echo "$0: error: Invalid operation: $OPERATION" >&2
   exit 1
esac

# Detect if the environment isn't set up properly.
if [ -z "$HOST" ]; then
  echo "$0: error: You need to set \$HOST" >&2
  exit 1
elif [ -z "$SYSROOT" ]; then
  echo "$0: error: You need to set \$SYSROOT" >&2
  exit 1
elif [ -z "$SORTIX_PORTS_DIR" ]; then
  echo "$0: error: You need to set \$SORTIX_PORTS_DIR" >&2
  exit 1
elif [ -z "$SORTIX_MIRROR_DIR" ]; then
  echo "$0: error: You need to set \$SORTIX_MIRROR_DIR" >&2
  exit 1
elif [ -z "$SORTIX_REPOSITORY_DIR" ]; then
  echo "$0: error: You need to set \$SORTIX_REPOSITORY_DIR" >&2
  exit 1
elif ! [ -d "$SORTIX_PORTS_DIR" ]; then
  echo "Warning: No ports directory found, third party software will not be built"
  exit 0
elif ! has_command tix-rmdiff; then
  echo "$0: error: You need to have installed Tix locally to compile ports." >&2
  exit 1
fi

# Create the mirror directory for downloaded archives.
mkdir -p "$SORTIX_MIRROR_DIR"

# Add the platform triplet to the binary repository path.
SORTIX_REPOSITORY_DIR="$SORTIX_REPOSITORY_DIR/$HOST"
mkdir -p "$SORTIX_REPOSITORY_DIR"

# Create the system root if absent.
if [ "$OPERATION" = build ]; then
  mkdir -p "$SYSROOT"
fi

# Make paths absolute for later use.
if [ "$OPERATION" = build ]; then
  SYSROOT=$(make_dir_path_absolute "$SYSROOT")
fi
SORTIX_MIRROR_DIR=$(make_dir_path_absolute "$SORTIX_MIRROR_DIR")
SORTIX_PORTS_DIR=$(make_dir_path_absolute "$SORTIX_PORTS_DIR")
SORTIX_REPOSITORY_DIR=$(make_dir_path_absolute "$SORTIX_REPOSITORY_DIR")

# Decide the optimization options with which the ports will be built.
if [ -z "${OPTLEVEL+x}" ]; then OPTLEVEL="-Os -s"; fi
if [ -z "${PORTS_OPTLEVEL+x}" ]; then PORTS_OPTLEVEL="$OPTLEVEL"; fi
if [ -z "${PORTS_CFLAGS+x}" ]; then PORTS_CFLAGS="$PORTS_OPTLEVEL"; fi
if [ -z "${PORTS_CXXFLAGS+x}" ]; then PORTS_CXXFLAGS="$PORTS_OPTLEVEL"; fi
if [ -z "${CFLAGS+x}" ]; then CFLAGS="$PORTS_CFLAGS"; fi
if [ -z "${CXXFLAGS+x}" ]; then CXXFLAGS="$PORTS_CXXFLAGS"; fi
CFLAGS="$CFLAGS -Werror=format -Wno-error=format-contains-nul -Werror=implicit-function-declaration"
CXXFLAGS="$CXXFLAGS -Werror=format -Wno-error=format-contains-nul"
export CFLAGS
export CXXFLAGS

# Initialize Tix package management in the system root if absent.
if [ "$OPERATION" = build ]; then
  if [ ! -e "$SYSROOT/tix/collection.conf" ]; then
    tix-collection "$SYSROOT" create --platform=$HOST --prefix= --generation=2
  fi
fi

PACKAGES="$("$(dirname -- "$0")"/list-packages.sh PACKAGES)"

# Simply stop if there is no packages available.
if [ -z "$PACKAGES" ]; then
  exit 0
fi

# Decide the order the packages are built in according to their dependencies.
if [ "$OPERATION" = build ]; then
  PACKAGES="$(echo "$PACKAGES" | tr ' ' '\n' | sort -R)"
  DEPENDENCY_MAKEFILE=$(mktemp)
  (for PACKAGE in $PACKAGES; do
     DEPENDENCIES="$(PACKAGES="$PACKAGES" \
                     "$(dirname -- "$0")"/list-packages.sh \
                     --dependencies $PACKAGE)"
     echo "$PACKAGE: $(echo "$DEPENDENCIES" | tr '\n' ' ' | sed -E 's/ +$/\n/')"
     echo "	@echo $PACKAGE"
   done;
   printf ".PHONY:"
   for PACKAGE in $PACKAGES; do
     printf " $PACKAGE"
   done;
   echo) > "$DEPENDENCY_MAKEFILE"

  BUILD_LIST=$(unset MAKE;
               unset MFLAGS;
               unset MAKEFLAGS;
               make -Bs -f "$DEPENDENCY_MAKEFILE" $PACKAGES)
  rm -f "$DEPENDENCY_MAKEFILE"
  PACKAGES="$BUILD_LIST"
fi

unset CACHE_PACKAGE
unset END
if [ "$OPERATION" = download ]; then
  END=download
elif [ "$OPERATION" = extract ]; then
  END=extract
else
  CACHE_PACKAGE=--cache-package
fi

# Build and install all the packages.
for PACKAGE in $PACKAGES; do
  SOURCE_PORT=$(tix-vars -d '' $SORTIX_PORTS_DIR/$PACKAGE/$PACKAGE.port \
                         SOURCE_PORT)
  tix-port \
    ${BUILD:+--build="$BUILD"} \
    $CACHE_PACKAGE \
    --collection="$SYSROOT" \
    --destination="$SORTIX_REPOSITORY_DIR" \
    ${END:+--end="$END"} \
    --generation=2 \
    --host=$HOST \
    ${SORTIX_PORTS_MIRROR:+--mirror="$SORTIX_PORTS_MIRROR"} \
    --mirror-directory="$SORTIX_MIRROR_DIR" \
    --prefix= \
    ${SOURCE_PORT:+--source-port="$SORTIX_PORTS_DIR/$SOURCE_PORT/$SOURCE_PORT"} \
    --sysroot="$SYSROOT" \
    "$SORTIX_PORTS_DIR/$PACKAGE/$PACKAGE"
done
