#!/bin/bash
#
# Writes one of Xiegu's X6200 base firmware files into the base from the radio's own Linux, without Xiegu's system:
# base_flash (tools/base_update/base_flash.c) does what Xiegu's MENU > SYSTEM > UPGRADE does, step for step. The base
# file comes from tools/base_update/fetch.sh, the program from tools/base_update/build.sh; both are copied to the
# radio. More in tools/base_update/README.md.
#
#     tools/base_update/flash.sh root@RADIO           --check     reads only: the file, the base's version, who holds
#                                                                  its port and bus. Nothing is sent to the base.
#     tools/base_update/flash.sh root@RADIO           --flash     writes the base (asks first; --yes does not ask)
#     tools/base_update/flash.sh root@RADIO           --recover   after a write that failed past the erase
#     ... --to VERSION                                             another release than 1.0.8 (1.0.7, 1.0.6, 1.0.3):
#                                                                  its file in build/base_update, found by its SHA-256
#                                                                  in base_releases.txt whatever its name (fetch.sh
#                                                                  VERSION puts it there); the newest build of a version
#                                                                  that has two (1.0.7: the factory build, then xiegu.eu's)
#     ... --file FILE.xgf [--unknown-ok]                           a file of your own; one the list does not know only
#                                                                  with --unknown-ok, and typed confirmation
#
# As Xiegu asks for its own update: the radio on a 13.8 V supply of 3 A or more, the battery out, for the whole
# write (about half a minute). The GUI is stopped for it (/etc/init.d/S95gui) and started again after. The write
# runs on the radio by itself (run_on_radio.sh under
# setsid, its log in /tmp/base_update/flash.log, then appended to /mnt/base_update.log): a lost connection does not
# stop it, and this script follows the log until it ends. Do not switch the radio off while it runs. After it the
# base does not start the new firmware by itself: the radio switches itself off about three minutes later, and is
# switched on again by hand; then --check shows the new version.
#
set -euo pipefail
RADIO=
MODE=
YES=
TO=1.0.8
FILE=
UNKNOWN=
while [ $# -gt 0 ]; do
    case "$1" in
        --check | --flash | --recover) MODE=$1 ;;
        --yes) YES=1 ;;
        --to) TO=${2:-}; shift ;;
        --file) FILE=${2:-}; shift ;;
        --unknown-ok) UNKNOWN=--unknown-ok ;;
        -*) echo "unknown option $1"; exit 1 ;;
        *) RADIO=$1 ;;
    esac
    shift
done
[ -n "$MODE" ] && [ -n "$RADIO" ] || { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 1; }
TOP=$(cd "$(dirname "$0")/../.." && pwd)
LIST=$TOP/tools/base_update/base_releases.txt
sha() { shasum -a 256 "$1" 2> /dev/null | cut -d' ' -f1 || sha256sum "$1" | cut -d' ' -f1; }
if [ -n "$FILE" ]; then
    [ -f "$FILE" ] || { echo "no $FILE"; exit 1; }
    F=$FILE
    TO=$(awk -v s="$(sha "$F")" '!/^#/ && $5 == s { print $1; exit }' "$LIST")
    [ -n "$TO" ] || TO="unknown (not in base_releases.txt)"
else
    # The release's files in the list, newest build first; the first here, by its SHA-256
    F=
    for s in $(awk -v v="$TO" '!/^#/ && $1 == v { print $5 }' "$LIST"); do
        for c in "$TOP"/build/base_update/*.xgf; do
            [ -f "$c" ] && [ "$(sha "$c")" = "$s" ] && { F=$c; break 2; }
        done
    done
    [ -n "$F" ] || { echo "no file of base $TO in build/base_update: tools/base_update/fetch.sh $TO"; exit 1; }
fi
XGF=$(basename "$F")
BIN=$TOP/build/x6200/base_update/base_flash
[ -x "$BIN" ] || { echo "no $BIN: run tools/base_update/build.sh first"; exit 1; }
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$RADIO")
D=/tmp/base_update

"${SSH[@]}" "mkdir -p $D"
scp -q "$BIN" "$F" "$RADIO:$D/"

if [ "$MODE" = --check ]; then
    "${SSH[@]}" "$D/base_flash --check $D/$XGF $UNKNOWN"
    exit
fi

cat << EOF
This writes Xiegu's base firmware $TO into the radio's base on $RADIO.
  - Xiegu's conditions: a 13.8 V supply of 3 A or more, the battery out, for the whole write.
  - The base's application is erased and written again. A write that fails after the erase leaves the base without
    one until this is run again with --recover (its bootloader is never erased; not yet tried on an X6200).
  - Do not switch the radio off until this says it is done.
EOF
if [ -n "$UNKNOWN" ]; then
    echo "  - $XGF IS NOT ONE OF XIEGU'S FILES KNOWN HERE: it passes the checks an encrypted file allows, but what it"
    echo "    holds can't be told."
    if [ -z "$YES" ]; then
        read -r -p "Type UNKNOWN to write an unknown file: " answer
        [ "$answer" = UNKNOWN ] || { echo "not written"; exit 1; }
    fi
fi
if [ -z "$YES" ]; then
    read -r -p "Type FLASH to go on: " answer
    [ "$answer" = FLASH ] || { echo "not written"; exit 1; }
fi

# On the radio, apart from this session (tools/base_update/run_on_radio.sh): GUI off, the write, the GUI back unless
# the base was left without its application (exit 3), the log kept on the card
EXTRA=$UNKNOWN
[ "$MODE" = --recover ] && EXTRA="$EXTRA --recover"
scp -q "$TOP/tools/base_update/run_on_radio.sh" "$RADIO:$D/"
"${SSH[@]}" "rm -f $D/flash.log; setsid sh $D/run_on_radio.sh $D $D/$XGF $EXTRA > /dev/null 2>&1 &"

# Follow the log until its END line, through dropped connections; 20 minutes at most (a normal write takes about one)
shown=0
for _ in $(seq 600); do
    sleep 2
    out=$("${SSH[@]}" "cat $D/flash.log 2> /dev/null" 2> /dev/null) || { echo "(no connection to the radio, trying again)"; continue; }
    total=$(printf '%s\n' "$out" | wc -l)
    if [ "$total" -gt "$shown" ]; then
        printf '%s\n' "$out" | tail -n +"$((shown + 1))"
        shown=$total
    fi
    case "$out" in *"END "*) break ;; esac
done
rc=$(printf '%s\n' "$out" | sed -n 's/^END //p' | tail -1)
[ -n "$rc" ] || { echo "no end of the write seen in 20 minutes: its log is $D/flash.log on the radio"; exit 4; }
[ "$rc" = 0 ] && echo "Written. If the radio switches itself off, switch it on again; then $0 $RADIO --check shows the version."
exit "$rc"
