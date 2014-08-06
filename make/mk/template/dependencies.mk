# template/dependencies.mk
# Enables header dependency tracking that makes .d files with makefile rules.
# You still need to include the applicable .d files if they exist.

COMPILE.c += -MD
COMPILE.c++ += -MD
