#!/bin/sh
# 2D & dual-screen regression check: compares final framebuffer MD5 between stock (DSFAST=0)
# and libdsfast (DSFAST=1) across SWAP=1 (3D top), SWAP=0 (2D bottom), and LAYOUT=8 (dual).
# Usage on device (from /tmp/cmd_to_run.sh): sh /tmp/verify_2d.sh

ROM=${ROM:-Grand Theft Auto - Chinatown Wars (USA) (En,Fr,De,Es,It).zip}
SLOT=${SLOT:-2}
FRAMES=${FRAMES:-180}
HOLD_MASK=${HOLD_MASK:-32}
OUT=/tmp/verify_2d.txt
: > "$OUT"

fail=0
check_case() {
    name=$1; swap=$2; layout=$3
    ROM="$ROM" SLOT="$SLOT" FRAMES="$FRAMES" MODE=play DSFAST=0 HIRES3D=1 FRAMESKIP=0 THREADED3D=1 \
        SWAP="$swap" LAYOUT="$layout" HOLD_MASK="$HOLD_MASK" SHOT=1 TAG="v2d_${name}_A" sh /tmp/bench.sh
    ROM="$ROM" SLOT="$SLOT" FRAMES="$FRAMES" MODE=play DSFAST=1 HIRES3D=1 FRAMESKIP=0 THREADED3D=1 \
        SWAP="$swap" LAYOUT="$layout" HOLD_MASK="$HOLD_MASK" SHOT=1 TAG="v2d_${name}_B" sh /tmp/bench.sh
    mA=$(md5sum "/tmp/dsshot_v2d_${name}_A.bgra" | awk '{print $1}')
    mB=$(md5sum "/tmp/dsshot_v2d_${name}_B.bgra" | awk '{print $1}')
    if [ "$mA" = "$mB" ]; then
        echo "PASS $name (swap=${swap:-cfg} layout=${layout:-cfg}): $mA" | tee -a "$OUT"
    else
        echo "FAIL $name (swap=${swap:-cfg} layout=${layout:-cfg}): A=$mA B=$mB" | tee -a "$OUT"
        fail=1
    fi
}

check_case top_3d   1 ""
check_case bot_2d   0 ""
check_case dual_8   1 8

exit "$fail"
