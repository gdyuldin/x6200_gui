#!/bin/bash
#
# base_flash's tests, on this computer (a Mac or a Linux PC; a C compiler and Python 3): the unit tests (units.c)
# and every scenario of the simulated base (sim.py), the slow ones side by side; about two minutes. Uses Xiegu's base
# files from build/base_update when they are there (tools/base_update/fetch.sh), else a stand-in of the same size
# for the transfers. Work files in build/base_update/test.
#
#     tools/base_update/test/run.sh
#
set -uo pipefail
T=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$T/../../.." && pwd)
W=$TOP/build/base_update/test
CC=${HOST_CC:-cc}
mkdir -p "$W"
cd "$W" || exit 1
X8=$TOP/build/base_update/x6200_mcu_20260820141208.xgf
X7=$TOP/build/base_update/x6200_mcu_20260116092100.xgf
KNOWN=()
[ -f "$X8" ] && KNOWN+=("$X8")
[ -f "$X7" ] && KNOWN+=("$X7")

"$CC" -O1 -w -I "$T/stub" -o units "$T/units.c" || exit 1
./units "$TOP/tools/base_update/base_releases.txt" ${KNOWN[@]+"${KNOWN[@]}"} || fails=1

# base_fetch.py on small update zips made as Xiegu's are (synth.py), through Python's zlib and through libz as on
# the radio: a file the list has comes out checked, one it does not comes out unverified with its line for the list
FETCH=$TOP/tools/base_update/base_fetch.py
head -c 32768 /dev/urandom > "$W/made_up.xgf"
python3 "$T/synth.py" "$W/synth_new.zip" x6200_mcu_20300101000000.xgf "$W/made_up.xgf"
[ -f "$X8" ] && python3 "$T/synth.py" "$W/synth_108.zip" x6200_mcu_20260820141208.xgf "$X8"
for way in zlib libz; do
    env=()
    [ "$way" = libz ] && env=(X6X00_LIBZ=1)
    rm -rf "$W/fetch_$way"
    if env "${env[@]+"${env[@]}"}" python3 "$FETCH" get --zip "$W/synth_new.zip" --out "$W/fetch_$way" > "$W/fetch_$way.log" 2>&1 \
        && cmp -s "$W/made_up.xgf" "$W/fetch_$way/x6200_mcu_20300101000000.xgf" \
        && grep -q "NOT in the list" "$W/fetch_$way.log" && [ -f "$W/fetch_$way/x6200_mcu_20300101000000.xgf.json" ]; then
        echo "pass  base_fetch.py ($way): a file not in the list comes out whole, unverified"
    else
        echo "FAIL  base_fetch.py ($way), unknown file: $W/fetch_$way.log"; fails=1
    fi
    if [ -f "$X8" ]; then
        if env "${env[@]+"${env[@]}"}" python3 "$FETCH" get --zip "$W/synth_108.zip" --out "$W/fetch_$way" >> "$W/fetch_$way.log" 2>&1 \
            && cmp -s "$X8" "$W/fetch_$way/x6200_mcu_20260820141208.xgf" && grep -q "as the list has it" "$W/fetch_$way.log"; then
            echo "pass  base_fetch.py ($way): Xiegu's 1.0.8 file comes out checked against the list"
        else
            echo "FAIL  base_fetch.py ($way), 1.0.8: $W/fetch_$way.log"; fails=1
        fi
    fi
done
# Told apart by SHA-256 alone: the same file under another name is not kept twice, another file under the same name
# is not overwritten
rm -rf "$W/dedupe" && mkdir -p "$W/dedupe" && cp "$W/made_up.xgf" "$W/dedupe/mine.xgf"
if python3 "$FETCH" get --zip "$W/synth_new.zip" --out "$W/dedupe" > "$W/dedupe.log" 2>&1; grep -q "here already" "$W/dedupe.log" \
    && [ "$(ls "$W/dedupe" | grep -c '\.xgf$')" = 1 ]; then
    echo "pass  base_fetch.py: a file here already under another name is not kept twice"
else
    echo "FAIL  base_fetch.py, same file twice: $W/dedupe.log"; fails=1
fi
rm -rf "$W/clobber" && mkdir -p "$W/clobber" && head -c 32768 /dev/urandom > "$W/clobber/x6200_mcu_20300101000000.xgf"
cp "$W/clobber/x6200_mcu_20300101000000.xgf" "$W/clobber_orig"
SUM=$(shasum -a 256 "$W/made_up.xgf" 2> /dev/null || sha256sum "$W/made_up.xgf")
if python3 "$FETCH" get --zip "$W/synth_new.zip" --out "$W/clobber" > "$W/clobber.log" 2>&1; \
    cmp -s "$W/clobber_orig" "$W/clobber/x6200_mcu_20300101000000.xgf" \
    && cmp -s "$W/made_up.xgf" "$W/clobber/x6200_mcu_20300101000000-$(echo "$SUM" | cut -c1-8).xgf"; then
    echo "pass  base_fetch.py: another file of the same name is kept, the new one beside it"
else
    echo "FAIL  base_fetch.py, same name: $W/clobber.log"; fails=1
fi

# The transfers: Xiegu's 1.0.8 file, or a stand-in of its size that only the simulated base sees (base_flash takes
# only Xiegu's files: the stand-in is let through by building the scenarios with its hash in place of 1.0.8's)
if [ -f "$X8" ]; then
    F=$X8
    SRC=$T/../base_flash.c
else
    F=$W/standin.xgf
    head -c 524288 /dev/urandom > "$F"
    H=$(shasum -a 256 "$F" 2> /dev/null || sha256sum "$F")
    sed "s/9a19f97a5ad9e141a3663d222f984073bf0b18b38d4662604372a0036f9daa23/${H%% *}/" "$T/../base_flash.c" > "$W/base_flash.c"
    SRC=$W/base_flash.c
    echo "no Xiegu file in build/base_update: the transfers use a stand-in"
fi

pids=()
for mode in ok silent stream dead noerase nostart nak can garbage eot x6100; do
    python3 "$T/sim.py" "$CC" "$SRC" "$T/stub" "$W" "$F" "$mode" > "$W/result_$mode" 2>&1 &
    pids+=($!)
done
[ -f "$X7" ] && { python3 "$T/sim.py" "$CC" "$T/../base_flash.c" "$T/stub" "$W" "$X7" ok ok107 > "$W/result_ok107" 2>&1 & pids+=($!); }
for p in "${pids[@]}"; do wait "$p" || fails=1; done
cat "$W"/result_*
[ -z "${fails:-}" ] && echo "all base_flash tests pass" || { echo "BASE_FLASH TESTS FAILED (logs in $W)"; exit 1; }
