#!/bin/sh
#
# Runs on the radio, started by tools/base_update/flash.sh apart from its SSH session (setsid), so that a lost
# connection does not stop it: the GUI stopped, the base written by base_flash, the GUI started again unless the base
# was left without its application (exit 3). Do not start the GUI meanwhile: it would open the base's port and bus in
# the middle of the write. Its output in DIR/flash.log, appended to /mnt/base_update.log at the end.
#
#     run_on_radio.sh DIR FILE [--recover]
#
D=$1
F=$2
shift 2
exec > "$D/flash.log" 2>&1 < /dev/null
echo "$(date -u) base_flash --flash $F --yes $*"
/etc/init.d/S95gui stop
sleep 2
if pidof x6200_gui x6200_daemon > /dev/null; then
    echo "the GUI is still running ($(pidof x6200_gui x6200_daemon)): the base is not written"
    rc=1
else
    "$D/base_flash" --flash "$F" --yes "$@"
    rc=$?
fi
echo "base_flash exit $rc"
if [ "$rc" -ne 3 ]; then
    /etc/init.d/S95gui start
else
    echo "the GUI is left off (the base has no application): run tools/base_update/flash.sh --recover"
fi
cat "$D/flash.log" >> /mnt/base_update.log
echo "END $rc"
