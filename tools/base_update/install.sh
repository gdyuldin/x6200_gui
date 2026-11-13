#!/bin/bash
#
# Puts the base firmware tools/base_update/fetch.sh fetched where Xiegu's own system on the X6200's internal memory
# looks for it: /usr/firmware on /dev/mmcblk2p2, the list MENU > SYSTEM > UPGRADE offers (*.xgf there). Run while the
# radio runs this GUI's image from its card; the internal system is mounted, the file copied in (mode 600, as Xiegu's own),
# checked and the system unmounted again. Nothing else there changes, and the base itself is not touched: that is
# Xiegu's UPGRADE screen (switch off, take the card out, switch on), or tools/base_update/flash.sh from here.
#
#     tools/base_update/install.sh root@RADIO
#
set -euo pipefail
RADIO=${1:?give the radio: root@ADDRESS}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
XGF=x6200_mcu_20260820141208.xgf
XGF_SHA=9a19f97a5ad9e141a3663d222f984073bf0b18b38d4662604372a0036f9daa23
F=$TOP/build/base_update/$XGF
[ -f "$F" ] || { echo "no $F: run tools/base_update/fetch.sh first"; exit 1; }

scp -q "$F" "$RADIO:/tmp/$XGF"
ssh -o BatchMode=yes "$RADIO" XGF="$XGF" XGF_SHA="$XGF_SHA" sh -s << 'EOF'
set -e
P=/dev/mmcblk2p2
M=/tmp/x6x00-emmc
[ "$(sha256sum "/tmp/$XGF" | cut -d' ' -f1)" = "$XGF_SHA" ] || { echo "/tmp/$XGF: wrong sha256"; exit 1; }
[ -b "$P" ] || { echo "no $P: no internal memory here"; exit 1; }
if grep -q "^$P " /proc/mounts; then echo "$P is mounted already: $(grep "^$P " /proc/mounts)"; exit 1; fi
mkdir -p "$M"
mount -t ext4 "$P" "$M"
trap 'sync; umount "$M" && rmdir "$M"; rm -f "/tmp/$XGF"' EXIT
[ -x "$M/usr/app_qt/x6200_ui_v100" ] && [ -d "$M/usr/firmware" ] || { echo "$P is not Xiegu's X6200 system"; exit 1; }
echo "Xiegu's system on $P: /usr/firmware before:"
ls -l "$M/usr/firmware"
cp "/tmp/$XGF" "$M/usr/firmware/$XGF.part"
chmod 600 "$M/usr/firmware/$XGF.part"
mv "$M/usr/firmware/$XGF.part" "$M/usr/firmware/$XGF"
sync
[ "$(sha256sum "$M/usr/firmware/$XGF" | cut -d' ' -f1)" = "$XGF_SHA" ] || { echo "the copy reads back wrong"; exit 1; }
echo "after:"
ls -l "$M/usr/firmware"
echo "$XGF is in Xiegu's MENU > SYSTEM > UPGRADE list"
EOF
