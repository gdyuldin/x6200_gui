#!/bin/bash
#
# Builds base_flash (tools/base_update/base_flash.c: the X6200's base firmware written as Xiegu's own app writes it)
# for the radio, into build/x6200/base_update. It needs only the C library and the kernel's I2C headers, and is linked
# statically, so any ARM hard-float Linux cross compiler builds it and the result runs whatever the radio's C library
# is: arm-linux-gnueabihf-gcc by default (Homebrew's arm-linux-gnueabihf, Debian's gcc-arm-linux-gnueabihf), another
# prefix with CROSS_COMPILE.
#
#     tools/base_update/build.sh           then tools/base_update/flash.sh [root@radio] --check
#     CROSS_COMPILE=arm-buildroot-linux-gnueabihf- tools/base_update/build.sh
#
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/x6200/base_update
CROSS=${CROSS_COMPILE:-arm-linux-gnueabihf-}
command -v "${CROSS}gcc" > /dev/null || { echo "no ${CROSS}gcc: install an ARM hard-float cross compiler or set CROSS_COMPILE"; exit 1; }
mkdir -p "$B"
"${CROSS}gcc" -O2 -Wall -Wextra -static -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
    -o "$B/base_flash" "$TOP/tools/base_update/base_flash.c"
"${CROSS}strip" "$B/base_flash"
ls -la "$B/base_flash"
