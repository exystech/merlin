#!/bin/sh
# Verifies the port(5) files.
set -e
for port in ports/*/*.port; do \
  tix-vars -t "$port"
  NAME=$(tix-vars "$port" NAME)
  if [ "$port" != "ports/$NAME/$NAME.port" ]; then
    echo "error: $port should be ports/$NAME/$NAME.port"
    exit 1
  fi
  DEVELOPMENT=$(tix-vars -d unset "$port" DEVELOPMENT)
  if [ "$DEVELOPMENT" != unset ]; then
    echo "error: $port: DEVELOPMENT should be not be set"
    exit 1
  fi
done
