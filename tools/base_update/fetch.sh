#!/bin/bash
#
# Fetches Xiegu's X6200 base firmware into build/base_update, by tools/base_update/base_fetch.py: Xiegu's update zip streamed from xiegu.eu and the base file taken out of it as it goes by (the
# zip's card image, its FAT partition, rootfs.tar, ./usr/firmware/*.xgf). Neither the zip (370 MB) nor the card image
# is kept; the download stops once the file is through (about two thirds of the zip). Each file is checked against
# tools/base_update/base_releases.txt (size, SHA-256) and kept with Xiegu's release note (FILE.xgf.txt).
#
#     tools/base_update/fetch.sh                 the latest in the list (1.0.8)
#     tools/base_update/fetch.sh 1.0.6           a release in the list: 1.0.3, 1.0.6, 1.0.7 (xiegu.eu's), 1.0.8
#     tools/base_update/fetch.sh all             every release in the list that is on xiegu.eu
#     tools/base_update/fetch.sh --discover      xiegu.eu's downloads page: each "X6200 Firmware Upgrade vX.Y.Z" the
#                                                list does not have fetched, kept as unverified (FILE.xgf.json), and
#                                                its line for base_releases.txt printed
#     tools/base_update/fetch.sh --url URL       an update zip by its address, unverified unless the list has it
#     tools/base_update/fetch.sh --zip FILE      the same from a zip here
#
# A file of your own: put it in build/base_update/ as NAME.xgf; flash.sh --file takes it (with --unknown-ok when it is
# not one the list knows). Needs Python 3 (its zlib module, or libz).
#
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/base_update
FETCH=(python3 "$TOP/tools/base_update/base_fetch.py")
mkdir -p "$B"

case "${1:-latest}" in
    --url | --zip)
        [ -n "${2:-}" ] || { echo "$1 needs an argument"; exit 1; }
        exec "${FETCH[@]}" get "$1" "$2" --out "$B" ;;
    --discover)
        # The downloads page, read by the same tool (check): the releases there that the list does not have
        CHECK=$B/discover
        mkdir -p "$CHECK"
        "${FETCH[@]}" check --out "$CHECK" --base 0
        NEW=$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1])); print(" ".join(c["links"][v] for v in c["not_in_list"]))' "$CHECK/check.json")
        [ -n "$NEW" ] || { echo "nothing on xiegu.eu that the list does not have"; exit 0; }
        rc=0
        for link in $NEW; do
            "${FETCH[@]}" get --url "$link" --out "$B" || rc=1
        done
        exit $rc ;;
    -*)
        sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'
        exit 1 ;;
    *)
        exec "${FETCH[@]}" get "${1:-latest}" --out "$B" ;;
esac
