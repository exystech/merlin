# Makefile that builds the Sortix operating system in one pass.
#
# This Makefile only works with Sortix make (tixmake).
# You need to bootstrap it from the make/ directory if you're not on Sortix.
# The cross developer documentation covers this, which you can see by running:
#
#   man share/man/man7/cross-development.7

# Don't implicitly include <sys.mk> (see above if this fails).
.nobuiltin

# Use the new set of make includes and include the new <sys.mk>.
PWD!=pwd
MKDIR=$(PWD)/make/mk
.includepath $(MKDIR)
.include <sys.mk>

# Include Sortix-specific definitions.
.include <sortix/sys.mk>

BUILD_TOOLS=\
carray \
kblayout-compiler \
make \
mkinitrd \
sf \
tix \

# TODO: Warn if TARGET isn't set to Sortix for build-tools?

BASE_HEADER_MODULES=\
kernel \
libc \
libm \

MODULES=\
doc \
libc \
libm \
dispd \
libmount \
bench \
carray \
disked \
editor \
ext \
games \
init \
kblayout \
kblayout-compiler \
login \
make \
mkinitrd \
regress \
sf \
sh \
sysinstall \
tix \
trianglix \
update-initrd \
utils \
kernel

FSH_DIRECTORIES=\
/ \
/bin \
/boot \
/dev \
/etc \
/etc/skel \
/home \
/include \
/lib \
/libexec \
/mnt \
/sbin \
/share \
/tix \
/tix/manifest \
/tmp \
/var \
/var/empty

SYSROOT?=$(PWD)/sysroot
SYSROOT_OVERLAY?=$(PWD)/sysroot-overlay

SORTIX_BUILDS_DIR?=builds
SORTIX_PORTS_DIR?=ports
SORTIX_RELEASE_DIR?=release
SORTIX_REPOSITORY_DIR?=repository
SORTIX_ISO_COMPRESSION?=xz
SORTIX_ISO_COMPRESSION_XZ=1 # TODO: HACK

SORTIX_INCLUDE_SOURCE_GIT_REPO?!=test -d .git && echo "file://`pwd`"
#SORTIX_INCLUDE_SOURCE_GIT_ORIGIN?=
SORTIX_INCLUDE_SOURCE_GIT_CLONE_OPTIONS?=--single-branch
SORTIX_INCLUDE_SOURCE_GIT_BRANCHES?=master
# TODO: And SORTIX_INCLUDE_SOURCE_GIT_REPO got set.
# TODO: HACK
HAS_GIT_WHAT!=which git > /dev/null || echo _LACK
HAS_GIT$(HAS_GIT_WHAT)=1
.ifdef HAS_GIT
  SORTIX_INCLUDE_SOURCE_GIT=1
  SORTIX_INCLUDE_SOURCE?=git
.else
  SORTIX_INCLUDE_SOURCE_YES=1
  SORTIX_INCLUDE_SOURCE?=yes
.endif

BUILD_NAME:=sortix-$(VERSION)-$(MACHINE)

LIVE_INITRD:=$(SORTIX_BUILDS_DIR)/$(BUILD_NAME).live.initrd
OVERLAY_INITRD:=$(SORTIX_BUILDS_DIR)/$(BUILD_NAME).overlay.initrd
SRC_INITRD:=$(SORTIX_BUILDS_DIR)/$(BUILD_NAME).src.initrd
SYSTEM_INITRD:=$(SORTIX_BUILDS_DIR)/$(BUILD_NAME).system.initrd

.DEFAULT_GOAL = all

.PHONY: all
all: sysroot

.PHONY: sysmerge
sysmerge: sysroot
	"$(SYSROOT)/sbin/sysmerge" "$(SYSROOT)"

.PHONY: sysmerge-wait
sysmerge-wait: sysroot
	"$(SYSROOT)/sbin/sysmerge" --wait "$(SYSROOT)"

.PHONY: clean-build-tools
clean-build-tools:
.for module in $(BUILD_TOOLS)
	+$(MAKE) -I "$(MKDIR)" -C $(module) clean
.endfor

.PHONY: build-tools
build-tools:
.for module in $(BUILD_TOOLS)
	+$(MAKE) -I "$(MKDIR)" -C $(module)
.endfor

.PHONY: install-build-tools
install-build-tools:
.for module in $(BUILD_TOOLS)
	+$(MAKE) -I "$(MKDIR)" -C $(module) install
.endfor

.PHONY: sysroot-fsh
sysroot-fsh:
.for dir in $(FSH_DIRECTORIES)
	mkdir -m 755 -p "$(SYSROOT)$(dir)"
.endfor
	ln -sfT . "$(SYSROOT)/usr"

.PHONY: sysroot-base-mk
sysroot-base-mk: sysroot-fsh

.PHONY: sysroot-base-headers
sysroot-base-headers: sysroot-base-mk
.for module in $(BASE_HEADER_MODULES)
	+SYSROOT="$(SYSROOT)" \
	$(MAKE) -I "$(MKDIR)" -C $(module) install-headers DESTDIR="$(SYSROOT)"
.endfor

.PHONY: sysroot-system
sysroot-system: sysroot-fsh sysroot-base-headers
	rm -f "$(SYSROOT)/tix/manifest/system"
.for dir in $(FSH_DIRECTORIES)
	echo $(dir) >> "$(SYSROOT)/tix/manifest/system"
.endfor
	echo "$(HOST_MACHINE)" > "$(SYSROOT)/etc/machine"
	echo /etc/machine >> "$(SYSROOT)/tix/manifest/system"
	(echo 'NAME="Sortix"' && \
	 echo 'VERSION="$(VERSION)"' && \
	 echo 'ID=sortix' && \
	 echo 'VERSION_ID="$(VERSION)"' && \
	 echo 'PRETTY_NAME="Sortix $(VERSION)"' && \
	 echo 'SORTIX_ABI=0.0' && \
	 true) > "$(SYSROOT)/etc/sortix-release"
	echo /etc/sortix-release >> "$(SYSROOT)/tix/manifest/system"
	ln -sf sortix-release "$(SYSROOT)/etc/os-release"
	echo /etc/os-release >> "$(SYSROOT)/tix/manifest/system"
	find share | sed -e 's,^,/,' >> "$(SYSROOT)/tix/manifest/system"
	cp -RT share "$(SYSROOT)/share"
.for module in $(MODULES)
	+SYSROOT="$(SYSROOT)" \
	$(MAKE) -I "$(MKDIR)" -C $(module)
	rm -rf "$(SYSROOT).destdir"
	mkdir -p "$(SYSROOT).destdir"
	+SYSROOT="$(SYSROOT)" \
	$(MAKE) -I "$(MKDIR)" -C $(module) install DESTDIR="$(SYSROOT).destdir"
	(cd "$(SYSROOT).destdir" && find .) | sed -e 's/\.//' -e 's/^$$/\//' | \
	grep -E '^.+$$' >> "$(SYSROOT)/tix/manifest/system"
	cp -RT --preserve=mode,timestamp,links "$(SYSROOT).destdir" "$(SYSROOT)"
	rm -rf "$(SYSROOT).destdir"
.endfor
	LC_ALL=C sort -u "$(SYSROOT)/tix/manifest/system" > "$(SYSROOT)/tix/manifest/system.new"
	mv "$(SYSROOT)/tix/manifest/system.new" "$(SYSROOT)/tix/manifest/system"

.PHONY: sysroot-source
sysroot-source: sysroot-fsh
# TODO: HACK:
#.if ${SORTIX_INCLUDE_SOURCE_GIT} == "git"
.ifdef SORTIX_INCLUDE_SOURCE_GIT
	rm -rf "$(SYSROOT)/src"
	git clone --no-hardlinks $(SORTIX_INCLUDE_SOURCE_GIT_CLONE_OPTIONS) -- $(SORTIX_INCLUDE_SOURCE_GIT_REPO) "$(SYSROOT)/src"
.for branch in $(SORTIX_INCLUDE_SOURCE_GIT_BRANCHES
	-cd "$(SYSROOT)/src" && \
	git fetch origin $(BRANCH) && \
	git branch -f $(BRANCH) FETCH_HEAD
.endfor
.ifdef ($(SORTIX_INCLUDE_SOURCE_GIT_ORIGIN),)
	cd "$(SYSROOT)/src" && git remote set-url origin $(SORTIX_INCLUDE_SOURCE_GIT_ORIGIN)
.else
	-cd "$(SYSROOT)/src" && git remote rm origin
.endif
.elifdef SORTIX_INCLUDE_SOURCE_YES
	cp .gitignore -t "$(SYSROOT)/src"
	cp LICENSE -t "$(SYSROOT)/src"
# TODO: Be sure to update this if s/TIXmakefile/Makefile/g.
	cp TIXmakefile -t "$(SYSROOT)/src"
	cp README -t "$(SYSROOT)/src"
	cp -RT build-aux "$(SYSROOT)/src/build-aux"
.for module in $(MODULES)
	cp -R $(module) -t "$(SYSROOT)/src"
	+$(MAKE) -I "$(SYSROOT)/src/make/mk" -C "$(SYSROOT)/src/$(module)" clean
.endfor
.endif
.ifndef SORTIX_INCLUDE_SOURCE_NO
	(cd "$(SYSROOT)" && find .) | sed 's/\.//' | grep -E '^/src(/.*)?$$' | \
	LC_ALL=C sort > "$(SYSROOT)/tix/manifest/src"
.endif

.PHONY: sysroot-ports
sysroot-ports: sysroot-system sysroot-source
	SORTIX_PORTS_DIR="$(SORTIX_PORTS_DIR)" \
	SORTIX_REPOSITORY_DIR="$(SORTIX_REPOSITORY_DIR)" \
	SYSROOT="$(SYSROOT)" \
	HOST="$(HOST)" \
	build-aux/build-ports.sh

.PHONY: sysroot-overlay
sysroot-overlay: sysroot-system sysroot-ports
	! [ -d "$(SYSROOT_OVERLAY)" ] || \
	cp -RT --preserve=mode,timestamp,links "$(SYSROOT_OVERLAY)" "$(SYSROOT)"

.PHONY: sysroot
sysroot: sysroot-system sysroot-source sysroot-ports sysroot-overlay

.PHONY: clean-core
clean-core:
.for module in $(MODULES)
	+$(MAKE) -I "$(MKDIR)" -C $(module) clean
.endfor

.PHONY: clean-ports
clean-ports:
	SORTIX_PORTS_DIR="$(SORTIX_PORTS_DIR)" \
	HOST="$(HOST)" \
	build-aux/clean-ports.sh

.PHONY: clean-builds
clean-builds:
	rm -rf "$(SORTIX_BUILDS_DIR)"
	rm -f sortix.bin
	rm -f sortix.initrd
	rm -f sortix.iso

.PHONY: clean-release
clean-release:
	rm -rf "$(SORTIX_RELEASE_DIR)"

.PHONY: clean-repository
clean-repository:
	rm -rf "$(SORTIX_REPOSITORY_DIR)"

.PHONY: clean-sysroot
clean-sysroot:
	rm -rf "$(SYSROOT)"
	rm -rf "$(SYSROOT)".destdir

.PHONY: clean
clean: clean-core clean-ports

.PHONY: mostlyclean
mostlyclean: clean-core clean-ports clean-builds clean-release clean-sysroot

.PHONY: distclean
distclean: clean-core clean-ports clean-builds clean-release clean-repository clean-sysroot

$(SORTIX_BUILDS_DIR):
	mkdir -p $(SORTIX_BUILDS_DIR)

$(LIVE_INITRD): sysroot $(SORTIX_BUILDS_DIR)
	mkdir -p `dirname $(LIVE_INITRD)`
	rm -rf $(LIVE_INITRD).d
	mkdir -p $(LIVE_INITRD).d
	mkdir -p $(LIVE_INITRD).d/etc
	mkdir -p $(LIVE_INITRD).d/etc/init
	echo single-user > $(LIVE_INITRD).d/etc/init/target
	echo "root::0:0:root:/root:sh" > $(LIVE_INITRD).d/etc/passwd
	echo "root::0:root" > $(LIVE_INITRD).d/etc/group
	mkdir -p $(LIVE_INITRD).d/home
	mkdir -p $(LIVE_INITRD).d/root -m 700
	cp -RT "$(SYSROOT)/etc/skel" $(LIVE_INITRD).d/root
	cp doc/welcome $(LIVE_INITRD).d/root
	tix-collection $(LIVE_INITRD).d create --platform=$(HOST) --prefix= --generation=2
	mkinitrd --format=sortix-initrd-2 $(LIVE_INITRD).d -o $(LIVE_INITRD)
	rm -rf $(LIVE_INITRD).d

.PHONY: $(OVERLAY_INITRD)
$(OVERLAY_INITRD): sysroot $(SORTIX_BUILDS_DIR)
	test ! -d "$(SYSROOT_OVERLAY)" || \
	mkinitrd --format=sortix-initrd-2 "$(SYSROOT_OVERLAY)" -o $(OVERLAY_INITRD)

$(SRC_INITRD): sysroot $(SORTIX_BUILDS_DIR)
	mkinitrd --format=sortix-initrd-2 --manifest="$(SYSROOT)/tix/manifest/src" "$(SYSROOT)" -o $(SRC_INITRD)

$(SYSTEM_INITRD): sysroot $(SORTIX_BUILDS_DIR)
	mkinitrd --format=sortix-initrd-2 --manifest="$(SYSROOT)/tix/manifest/system" "$(SYSROOT)" -o $(SYSTEM_INITRD)

$(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso: sysroot $(LIVE_INITRD) $(OVERLAY_INITRD) $(SRC_INITRD) $(SYSTEM_INITRD) $(SORTIX_BUILDS_DIR)
	rm -rf $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
	mkdir -p $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
	mkdir -p $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot
	mkdir -p $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/repository
	SORTIX_PORTS_DIR="$(SORTIX_PORTS_DIR)" \
	SORTIX_REPOSITORY_DIR="$(SORTIX_REPOSITORY_DIR)" \
	SYSROOT="$(SYSROOT)" \
	HOST="$(HOST)" \
	build-aux/iso-repository.sh $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/repository
.ifdef SORTIX_ISO_COMPRESSION_XZ
	xz -c "$(SYSROOT)/boot/sortix.bin" > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/sortix.bin.xz
	xz -c $(LIVE_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/live.initrd.xz
	test ! -e "$(OVERLAY_INITRD)" || \
	xz -c $(OVERLAY_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/overlay.initrd.xz
	xz -c $(SRC_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/src.initrd.xz
	xz -c $(SYSTEM_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/system.initrd.xz
	build-aux/iso-grub-cfg.sh --platform $(HOST) --version $(VERSION) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
	grub-mkrescue --compress=xz -o $(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
.elifdef SORTIX_ISO_COMPRESSION_GZIP
	gzip -c "$(SYSROOT)/boot/sortix.bin" > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/sortix.bin.gz
	gzip -c $(LIVE_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/live.initrd.gz
	test ! -e "$(OVERLAY_INITRD)" || \
	gzip -c $(OVERLAY_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/overlay.initrd.gz
	gzip -c $(SRC_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/src.initrd.gz
	gzip -c $(SYSTEM_INITRD) > $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/system.initrd.gz
	build-aux/iso-grub-cfg.sh --platform $(HOST) --version $(VERSION) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
	grub-mkrescue --compress=gz -o $(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
.else
	cp "$(SYSROOT)/boot/sortix.bin" $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/sortix.bin
	cp $(LIVE_INITRD) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/live.initrd
	test ! -e "$(OVERLAY_INITRD)" || \
	cp $(OVERLAY_INITRD) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/overlay.initrd
	cp $(SRC_INITRD) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/src.initrd
	cp $(SYSTEM_INITRD) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso/boot/system.initrd
	build-aux/iso-grub-cfg.sh --platform $(HOST) --version $(VERSION) $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
	grub-mkrescue -o $(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso
.endif
	rm -rf $(SORTIX_BUILDS_DIR)/$(BUILD_NAME)-iso

.PHONY: iso
iso: $(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso

sortix.iso: $(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso
	cp $< $@

# Release

$(SORTIX_RELEASE_DIR)/$(VERSION):
	mkdir -p $@

$(SORTIX_RELEASE_DIR)/$(VERSION)/builds: $(SORTIX_RELEASE_DIR)/$(VERSION)
	mkdir -p $@

$(SORTIX_RELEASE_DIR)/$(VERSION)/builds/$(BUILD_NAME).iso: $(SORTIX_BUILDS_DIR)/$(BUILD_NAME).iso $(SORTIX_RELEASE_DIR)/$(VERSION)/builds
	cp $< $@

.PHONY: release-iso
release-iso: $(SORTIX_RELEASE_DIR)/$(VERSION)/builds/$(BUILD_NAME).iso

.PHONY: release-builds
release-builds: release-iso

$(SORTIX_RELEASE_DIR)/$(VERSION)/README: README $(SORTIX_RELEASE_DIR)/$(VERSION)
	cp $< $@

.PHONY: release-readme
release-readme: $(SORTIX_RELEASE_DIR)/$(VERSION)/README

.PHONY: release-arch
release-arch: release-builds release-readme

.PHONY: release-shared
release-shared: release-readme

.PHONY: release
release: release-arch release-shared

# Virtualization
.PHONY: run-virtualbox
run-virtualbox: sortix.iso
	virtualbox --startvm sortix

.PHONY: run-virtualbox-debug
run-virtualbox-debug: sortix.iso
	virtualbox --debug --start-running --startvm sortix

# Statistics
.PHONY: linecount
linecount:
	wc -l `find . -type f | grep -E '\.(h|h\+\+|c|cpp|c\+\+|s|S|asm|kblayout|mak|sh)$$|(^|/)Makefile$$' | grep -Ev '^\./(\.git|sysroot|sysroot-overlay|ports|repository)(/|$$)'` | sort -n
