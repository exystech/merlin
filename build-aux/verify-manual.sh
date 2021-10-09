#!/bin/sh
# Checks if mandoc -Tlint warns about any manual pages.
RESULT=true
for MANUAL in $(git ls-files | grep -E '\.[0-9]$' | grep -Ev '^libm/man/'); do
  # TODO: mandoc on Sortix can't parse .Dd dates at the moment.
  #if ! mandoc -Tlint $MANUAL; then
  if mandoc -Tlint $MANUAL 2>&1 | grep -Ev "WARNING: cannot parse date"; then
    RESULT=false
  fi
done
$RESULT
