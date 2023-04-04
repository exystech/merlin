#!/bin/sh
# Copyright (c) 2018, 2022 Jonas 'Sortie' Termansen.
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# iso-grub-cfg.sh
# Generate GRUB bootloader configuration for release .iso filesystems.

# Note: This file has detailed documentation in release-iso-bootconfig(7).

set -e

this=$(which -- "$0")
thisdir=$(dirname -- "$this")

directory=
mount=false
platform=
version=

dashdash=
previous_option=
for argument do
  if test -n "$previous_option"; then
    eval $previous_option=\$argument
    previous_option=
    continue
  fi

  case $argument in
  *=?*) parameter=$(expr "X$argument" : '[^=]*=\(.*\)' || true) ;;
  *=)   parameter= ;;
  *)    parameter=yes ;;
  esac

  case $dashdash$argument in
  --) dashdash=yes ;;
  --platform) previous_option=platform ;;
  --platform=*) platform=$parameter ;;
  --mount) mount=true ;;
  --version=*) version=$parameter ;;
  --version) previous_option=version ;;
  -*) echo "$0: unrecognized option $argument" >&2
      exit 1 ;;
  *) directory="$argument" ;;
  esac
done

if test -n "$previous_option"; then
  echo "$0: option '$argument' requires an argument" >&2
  exit 1
fi

if test -z "$platform"; then
  echo "$0: platform wasn't set with --platform" >&2
  exit 1
fi

if test -z "$version"; then
  echo "$0: version wasn't set with --version" >&2
  exit 1
fi

if test -z "$directory"; then
  echo "$0: no directory operand supplied" >&2
  exit 1
fi

machine=$(expr x"$platform" : 'x\([^-]*\).*')

maybe_compressed() {
  if [ -e "$1.xz" ]; then
    echo "$1.xz"
  elif [ -e "$1.gz" ]; then
    echo "$1.gz"
  elif [ -e "$1" ]; then
    echo "$1"
  fi
}

human_size() {
  (export LC_ALL=C; du -bh "$1" 2>/dev/null || du -h "$1") |
  sed -E 's/^([^[:space:]]+).*/\1/'
}

portvar() {
  echo "$1" | sed -e 's/-/_/g' -e 's/+/x/g'
}

isinset() {
  (for port in $2; do
     if [ x"$1" = x"$port" ]; then
       echo true
       exit 0
     fi
   done
   echo false
   exit 0)
}

. "$thisdir/ports.conf"

cd "$directory"

kernel=$(maybe_compressed boot/sortix.bin)
if $mount; then
  initrd=$(maybe_compressed boot/sortix.initrd)
  initrds=$initrd
else
  live_initrd=$(maybe_compressed boot/live.initrd)
  overlay_initrd=$(maybe_compressed boot/overlay.initrd)
  src_initrd=$(maybe_compressed boot/src.initrd)
  system_initrd=$(maybe_compressed boot/system.initrd)
  initrds="$system_initrd $src_initrd $live_initrd $overlay_initrd"
fi
if $mount; then
  ports=
else
  ports=$(ls repository |
         grep -E '\.tix\.tar\.xz$' |
         sed -E 's/\.tix\.tar\.xz$//')
fi

mkdir -p boot/grub
mkdir -p boot/grub/init

echo "furthermore" > boot/grub/init/furthermore
# TODO: Possibly use a 'try' feature to not warn in case it was already unset.
echo "unset require dhclient" > boot/grub/init/network-no-dhclient
echo "require ntpd optional" > boot/grub/init/local-ntpd
echo "require sshd optional" > boot/grub/init/local-sshd

exec > boot/grub/grub.cfg

for hook in \
advanced_menu_post \
advanced_menu_pre \
initrd_post \
kernel_post \
kernel_pre \
menu_post \
menu_pre \
ports_menu \
ports_menu_post \
ports_menu_pre \
ports_menu_sets \
ports_post \
ports_pre \
tix_menu \
tix_menu_post \
tix_menu_pre \
tix_menu_sets \
; do
  cat << EOF
function hook_$hook {
  true
}
EOF
done
for set in all $sets no; do
  cat << EOF
function hook_ports_set_$set {
  true
}
function hook_tix_set_$set {
  true
}
EOF
done
echo

cat << EOF
insmod part_msdos
insmod ext2
EOF
find . | grep -Eq '\.gz$' && echo "insmod gzio"
find . | grep -Eq '\.xz$' && echo "insmod xzio"

cat << EOF
insmod all_video
if loadfont unicode; then
  insmod gfxterm
  terminal_output gfxterm
fi

set version="$version"
set machine="$machine"
set mount=$mount
if \$mount; then
  chain='-- /sbin/init --target=chain'
else
  chain=
fi
set base_menu_title="Sortix \$version for \$machine"
set menu_title="\$base_menu_title"
set timeout=10
set default="0"
if [ -e /boot/random.seed ]; then
  no_random_seed=
else
  no_random_seed=--no-random-seed
fi
set enable_src=true
set enable_network_drivers=
set enable_dhclient=true
set enable_gui=true
set enable_ntpd=false
set enable_sshd=false

export version
export machine
export mount
export chain
export base_menu_title
export menu_title
export timeout
export default
export no_random_seed
export enable_src
export enable_network_drivers
export enable_dhclient
export enable_gui
export enable_ntpd
export enable_sshd
EOF

if [ -n "$ports" ]; then
  echo
  for port in $ports; do
    printf 'port_%s=true\n' "$(portvar "$port")"
  done
  for port in $ports; do
    printf 'tix_%s=false\n' "$(portvar "$port")"
  done
  echo
  for port in $ports; do
    printf 'export port_%s\n' "$(portvar "$port")"
  done
  for port in $ports; do
    printf 'export tix_%s\n' "$(portvar "$port")"
  done
fi

echo
echo 'function select_ports_set_no {'
for port in $ports; do
  printf "  port_%s=false\n" "$(portvar "$port")"
done
printf "  hook_port_set_no\n"
echo "}"
for set in $sets; do
  echo
  set_content=$(eval echo \$set_$set)
  echo "function select_ports_set_$set {"
  for port in $ports; do
    printf "  port_%s=%s\n" "$(portvar "$port")" "$(isinset "$port" "$set_content")"
  done
  printf "  hook_port_set_%s\n" "$set"
  echo "}"
done
echo
echo 'function select_ports_set_all {'
for port in $ports; do
  printf "  port_%s=true\n" "$(portvar "$port")"
done
printf "  hook_port_set_all\n"
echo "}"

echo
echo 'function select_tix_set_no {'
for port in $ports; do
  printf "  tix_%s=false\n" "$(portvar "$port")"
done
printf "  hook_tix_set_no\n"
echo "}"
for set in $sets; do
  echo
  set_content=$(eval echo \$set_$set)
  echo "function select_tix_set_$set {"
  for port in $ports; do
    printf "  tix_%s=%s\n" "$(portvar "$port")" "$(isinset "$port" "$set_content")"
  done
  printf "  hook_tix_set_%s\n" "$set"
  echo "}"
done
echo
echo 'function select_tix_set_all {'
for port in $ports; do
  printf "  tix_%s=true\n" "$(portvar "$port")"
done
printf "  hook_tix_set_all\n"
echo "}"
echo

printf "function load_base {\n"
case $platform in
x86_64-*)
  cat << EOF
  if ! cpuid -l; then
    echo "Error: You cannot run this 64-bit operating system because this" \
         "computer has no 64-bit mode."
    read
    exit
  fi
EOF
  ;;
esac
cat << EOF
  hook_kernel_pre
  echo -n "Loading /$kernel ($(human_size $kernel)) ... "
  multiboot /$kernel \$no_random_seed \$enable_network_drivers \$chain "\$@"
  echo done
  hook_kernel_post
  # TODO: Injecting configuration doesn't work for mounted cdroms.
  if ! \$enable_dhclient; then
    echo -n "Disabling dhclient ... "
    module /boot/grub/init/furthermore --create-to /etc/init/network
    module /boot/grub/init/network-no-dhclient --append-to /etc/init/network
    echo done
  fi
  if \$enable_ntpd; then
    echo -n "Enabling ntpd ... "
    module /boot/grub/init/local-ntpd --append-to /etc/init/local
    module /boot/grub/init/furthermore --create-to /etc/init/time
    module /boot/grub/init/local-ntpd --append-to /etc/init/time
    echo done
  fi
  if \$enable_sshd; then
    echo -n "Enabling sshd ... "
    module /boot/grub/init/local-sshd --append-to /etc/init/local
    echo done
  fi
  if [ \$no_random_seed != --no-random-seed ]; then
    echo -n "Loading /boot/random.seed (256) ... "
    module /boot/random.seed --random-seed
    echo done
  fi
EOF
for initrd in $initrds; do
  if [ "$initrd" = "$src_initrd" ]; then
    cat << EOF
  if \$enable_src; then
    echo -n "Loading /$initrd ($(human_size $initrd)) ... "
    module /$initrd
    echo done
  fi
EOF
  else
    cat << EOF
  echo -n "Loading /$initrd ($(human_size $initrd)) ... "
  module /$initrd
  echo done
EOF
  fi
done
cat << EOF
  hook_initrd_post
}
EOF

echo
cat << EOF
function load_ports {
  hook_ports_pre
EOF
if [ -z "$ports" ]; then
  printf "  true\n"
fi
for port in $ports; do
  tix=repository/$port.tix.tar.xz
  cat << EOF
  if \$tix_$(portvar "$port"); then
    echo -n "Loading /$tix ($(human_size $tix)) ... "
    module --nounzip /$tix --to /$tix
    echo done
  fi
  if \$port_$(portvar "$port"); then
    echo -n "Loading /$tix ($(human_size $tix)) ... "
    module /$tix --tix
    echo done
  fi
EOF
done
cat << EOF
  hook_ports_post
}
EOF

echo
cat << EOF
function load_sortix {
  load_base "\$@"
  load_ports
}
EOF

cat << EOF

if [ -e /boot/grub/hooks.cfg ]; then
  . /boot/grub/hooks.cfg
fi

. /boot/grub/main.cfg
EOF

exec > boot/grub/main.cfg

menuentry() {
  echo
  printf "menuentry \"Sortix (%s)\" {\n" "$1"
  if [ -n "$2" ]; then
    printf "  if \$enable_gui; then\n"
    printf "    load_sortix %s-gui\n" "$2"
    printf "  else\n"
    printf "    load_sortix %s\n" "$2"
    printf "  fi\n"
  else
    printf "  load_sortix\n"
  fi
  printf "}\n"
}

cat << EOF
menu_title="\$base_menu_title"

hook_menu_pre
EOF

menuentry "live environment" '-- /sbin/init --target=single-user'
menuentry "new installation" '-- /sbin/init --target=sysinstall'
menuentry "upgrade existing installation" '-- /sbin/init --target=sysupgrade'

cat << EOF

if ! $mount; then
menuentry "Select ports..." {
  configfile /boot/grub/ports.cfg
}
fi

menuentry "Advanced..." {
  configfile /boot/grub/advanced.cfg
}

hook_menu_post
EOF

exec > boot/grub/advanced.cfg

cat << EOF
menuentry "Back..." {
  configfile /boot/grub/main.cfg
}

menu_title="\$base_menu_title - Advanced Options"

hook_advanced_menu_pre

if "\$enable_gui"; then
  menuentry "Disable GUI" {
    enable_gui=false
    configfile /boot/grub/advanced.cfg
  }
else
  menuentry "Enable GUI" {
    enable_gui=true
    configfile /boot/grub/advanced.cfg
  }
fi

if ! $mount; then
if "\$enable_src"; then
  menuentry "Disable loading source code" {
    enable_src=false
    configfile /boot/grub/advanced.cfg
  }
else
  menuentry "Enable loading source code" {
    enable_src=true
    configfile /boot/grub/advanced.cfg
  }
fi
fi

if [ "\$enable_network_drivers" = --disable-network-drivers ]; then
  menuentry "Enable networking drivers" {
    enable_network_drivers=
    configfile /boot/grub/advanced.cfg
  }
else
  menuentry "Disable networking drivers" {
    enable_network_drivers=--disable-network-drivers
    configfile /boot/grub/advanced.cfg
  }
fi

if ! $mount; then
if \$enable_dhclient; then
  menuentry "Disable DHCP client" {
    enable_dhclient=false
    configfile /boot/grub/advanced.cfg
  }
else
  menuentry "Enable DHCP client" {
    enable_dhclient=true
    configfile /boot/grub/advanced.cfg
  }
fi

if \$enable_ntpd; then
  menuentry "Disable NTP client" {
    enable_ntpd=false
    configfile /boot/grub/advanced.cfg
  }
else
  menuentry "Enable NTP client" {
    enable_ntpd=true
    configfile /boot/grub/advanced.cfg
  }
fi

if \$enable_sshd; then
  menuentry "Disable SSH server" {
    enable_sshd=false
    configfile /boot/grub/advanced.cfg
  }
else
  menuentry "Enable SSH server" {
    enable_sshd=true
    configfile /boot/grub/advanced.cfg
  }
fi

menuentry "Select binary packages..." {
  configfile /boot/grub/tix.cfg
}
fi

hook_advanced_menu_post
EOF

exec > boot/grub/ports.cfg

cat << EOF
menuentry "Back..." {
  configfile /boot/grub/main.cfg
}

menu_title="\$base_menu_title - Ports"

hook_ports_menu_pre

menuentry "Load all ports" {
  select_ports_set_all
  configfile /boot/grub/ports.cfg
}

hook_ports_menu_sets
EOF

for set in $sets; do
  echo
  set_content=$(eval echo \$set_$set)
  printf 'menuentry "Load only '"$set"' ports" {\n'
  printf "  select_ports_set_%s\n" "$set"
  printf '  configfile /boot/grub/ports.cfg\n'
  printf '}\n'
done

cat << EOF

menuentry "Load no ports" {
  select_ports_set_no
  configfile /boot/grub/ports.cfg
}

hook_ports_menu

EOF

for port in $ports; do
  cat << EOF
if \$port_$(portvar "$port"); then
  menuentry "$port = true" {
    port_$(portvar "$port")=false
    configfile /boot/grub/ports.cfg
  }
else
  menuentry "$port = false" {
    port_$(portvar "$port")=true
    configfile /boot/grub/ports.cfg
  }
fi
EOF
done

cat << EOF

hook_ports_menu_post
EOF

exec > boot/grub/tix.cfg

cat << EOF
menuentry "Back..." {
  configfile /boot/grub/advanced.cfg
}

menu_title="\$base_menu_title - Binary Packages"

hook_tix_menu_pre

menuentry "Load all binary packages" {
  select_tix_set_all
  configfile /boot/grub/tix.cfg
}

hook_tix_menu_sets
EOF

for set in $sets; do
  echo
  set_content=$(eval echo \$set_$set)
  printf 'menuentry "Load only '"$set"' binary packages" {\n'
  printf "  select_tix_set_%s\n" "$set"
  printf '  configfile /boot/grub/tix.cfg\n'
  printf '}\n'
done

cat << EOF

menuentry "Load no binary packages" {
  select_tix_set_no
  configfile /boot/grub/tix.cfg
}

hook_tix_menu

EOF

for port in $ports; do
  cat << EOF
if \$tix_$(portvar "$port"); then
  menuentry "$port = true" {
    tix_$(portvar "$port")=false
    configfile /boot/grub/tix.cfg
  }
else
  menuentry "$port = false" {
    tix_$(portvar "$port")=true
    configfile /boot/grub/tix.cfg
  }
fi
EOF
done

cat << EOF

hook_tix_menu_post
EOF
