#!/bin/sh
set -e

NAME=$(tix-vars -d '' "$1" NAME)
COMPRESSION=$(tix-vars -d '' "$1" COMPRESSION)
DISTNAME_REGEX=$(tix-vars -d '' "$1" DISTNAME_REGEX)
RELEASE_SEARCH_PAGE=$(tix-vars -d '' "$1" RELEASE_SEARCH_PAGE)
RELEASE_SEARCH_REGEX=$(tix-vars -d '' "$1" RELEASE_SEARCH_REGEX)
UPSTREAM_ARCHIVE=$(tix-vars -d '' "$1" UPSTREAM_ARCHIVE)
UPSTREAM_SITE=$(tix-vars -d '' "$1" UPSTREAM_SITE)
VERSION=$(tix-vars -d '' "$1" VERSION)
VERSION_REGEX=$(tix-vars -d '' "$1" VERSION_REGEX)

if [ -z "$UPSTREAM_ARCHIVE" ]; then exit; fi

escape_regex() {
  echo "$1" | sed -E 's,[\\+*?.{}<>],\\\0,g'
}

TAR_REGEX="(\.tar(\.(gz|bz2|xz)))"

if [ -z "$RELEASE_SEARCH_PAGE" ]; then
  case "$UPSTREAM_SITE" in
  https://github.com/*/releases/*)
    RELEASE_SEARCH_PAGE=$(echo "$UPSTREAM_SITE" | grep -Eo '.*/releases/');;
  *) RELEASE_SEARCH_PAGE="$UPSTREAM_SITE/";;
  esac
fi

if [ -z "$VERSION_REGEX" ]; then
  VERSION_REGEX="([0-9]+\.[0-9]+(\.[0-9]+)*)"
fi

if [ -z "$DISTNAME_REGEX" ]; then
  DISTNAME_REGEX=$(echo "$UPSTREAM_ARCHIVE" |
                   sed -E 's,\+,\\+,g' |
                   sed -E "s,$VERSION_REGEX(\.tar.*),$(escape_regex "$VERSION_REGEX"),")
fi

if [ -z "$RELEASE_SEARCH_REGEX" ]; then
  RELEASE_SEARCH_REGEX="\<$DISTNAME_REGEX$TAR_REGEX\>"
fi

upgrade_version() {
  sed -E -e "s,^(VERSION_MAJOR)=.*,\\1=$(echo "$2" | sed -E 's,([0-9]+)\.([0-9]+)(\.([0-9]+))?.*,\1,')," \
         -e "s,^(VERSION_MINOR)=.*,\\1=$(echo "$2" | sed -E 's,([0-9]+)\.([0-9]+)(\.([0-9]+))?.*,\2,')," \
         -e "s,^(VERSION_PATCH)=.*,\\1=$(echo "$2" | sed -E 's,([0-9]+)\.([0-9]+)(\.([0-9]+))?.*,\4,')," \
         -e "s/^(VERSION)=[^$]*$/\\1=$2/" \
         -e "s/^(COMPRESSION)=.*/\1=$COMPRESSION/" \
         "$1"
}

instantiate() {
  upgrade_version "$1" "$2" | tix-vars - "$3"
}

LATEST=$(curl -Ls "$RELEASE_SEARCH_PAGE" | grep -Eo "$RELEASE_SEARCH_REGEX" | sort -Vu | tail -1)
case "$LATEST" in
*.tar) COMPRESSION=tar;;
*.tar.gz) COMPRESSION=tar.gz;;
*.tar.bz2) COMPRESSION=tar.bz2;;
*.tar.xz) COMPRESSION=tar.xz;;
esac
NEW_VERSION="$(echo "$LATEST" | sed -E "s,$RELEASE_SEARCH_REGEX,\1,")"

if [ -t 1 ]; then
  RED='\033[91m'
  GREEN='\033[92m'
  RESET='\033[m'
else
  RED=''
  GREEN=''
  RESET=''
fi

if [ -z "$NEW_VERSION" ]; then
  printf "$RED%s$RESET\n" "$UPSTREAM_ARCHIVE failed to find available versions: $RELEASE_SEARCH_PAGE | grep -E '$RELEASE_SEARCH_REGEX'"
  exit 1
fi

NEW_UPSTREAM_SITE=$(instantiate "$1" "$NEW_VERSION" "UPSTREAM_SITE")
NEW_UPSTREAM_ARCHIVE=$(instantiate "$1" "$NEW_VERSION" "UPSTREAM_ARCHIVE")

if ! wget -q "$NEW_UPSTREAM_SITE/$NEW_UPSTREAM_ARCHIVE" -O /dev/null; then
  printf "$RED%s$RESET\n" "$NAME failed to download: $NEW_UPSTREAM_SITE/$NEW_UPSTREAM_ARCHIVE"
  exit 1
fi

if [ "$VERSION" = "$NEW_VERSION" ]; then
  echo "$NAME $VERSION is up to date"
  exit
fi

if [ "$2" = upgrade ]; then
  if [ "$(tix-vars -d false "$1" DEVELOPMENT)" = true ]; then
    echo "$0: warning: Not upgrading $1 in DEVELOPMENT=true mode" >&2
    exit
  fi
  (upgrade_version "$1" "$NEW_VERSION" && echo DEVELOPMENT=true) > "$1.new"
  mv "$1.new" "$1"
fi

printf "$GREEN%s$RESET\n" "$NAME $VERSION -> $NEW_VERSION $NEW_UPSTREAM_SITE/$NEW_UPSTREAM_ARCHIVE"
