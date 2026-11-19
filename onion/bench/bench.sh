#!/bin/sh
# On the Miyoo: benchmark DraStic in play mode (with screen/MI_GFX/VSYNC) or --bench-full.
# Copy bench.sh and libdsbench.so to /tmp first; run via /tmp/cmd_to_run.sh + killall MainUI.
# Keep /mnt/SDCARD/Roms out of /tmp/cmd_to_run.sh itself!
#
# Env options:
#   ROM         ROM filename under /mnt/SDCARD/Roms/NDS (or full path)
#   SLOT        Savestate slot to load after boot (0..10; empty = boot normally)
#   FRAMES      Frames to record in play mode (default 1200) or --bench-full (default 600)
#   MODE        "play" (default), "benchfull" (--bench-full), or "benchmark" (--benchmark subsystem breakdown)
#   DSFAST      1 (default, preload libdsfast.so) or 0 (stock DraStic)
#   MHZ         CPU clock (default 1800)
#   RAW_VM      1 to skip read_ahead_kb=32 / page-cluster=0 tuning
#   LAYOUT      Optional temporary resources/settings.json mode (e.g. 3=fullscreen, 8=dual)
#   FRAMESKIP   Override frameskip_type (default 0 in bench for raw pacing, or 2)
#   THREADED3D  Override threaded_3d (default 1)
#   HIRES3D     Override hires_3d (default 0; 1 = 512x384 3D)
#   SWAP        Optional override for screen_swap (0 or 1; default = drastic.cfg)
#   SHOT        1 (default) to dump 640x480 BGRA fb0 page to /tmp/dsshot_<TAG>.bgra on end
#   SDL2_DIR    Optional directory prepended to LD_LIBRARY_PATH (e.g. /tmp/sdl2_test)
#   HOLD_MASK   Optional DraStic button mask held while recording (bit 4=A 5=B 6=X 7=Y 8=L 9=R)
#   HOLD_FROM   Recorded frame at which the hold starts (default 0); HOLD_LEN frames to hold (0 = to the end)
#   IR          Optional .ir input-playback path
#   EXTRA_LIBS  Optional extra colon-separated .so paths for LD_PRELOAD
#   EXTRA_ARGS  Optional extra CLI args for ./drastic (e.g. "--color-depth 16")
#   TAG         Output file suffix (default "run")

NDS_DIR=/mnt/SDCARD/Emu/NDS
ROM_NAME=${ROM:-Mario Kart DS (USA) (En,Fr,De,Es,It).zip}
case "$ROM_NAME" in
    /*) ROM_PATH="$ROM_NAME" ;;
    *)  ROM_PATH="/mnt/SDCARD/Roms/NDS/$ROM_NAME" ;;
esac

MODE=${MODE:-play}
DSFAST=${DSFAST:-1}
MHZ=${MHZ:-1800}
TAG=${TAG:-run}
OUT=/tmp/dsbench_${TAG}.txt
FLIP_LOG=/tmp/dsframes_${TAG}.txt
EMU_LOG=/tmp/dsemu_${TAG}.txt
RAW_LOG=/tmp/dsraw_${TAG}.log

if [ "$MODE" = "benchfull" ] || [ "$MODE" = "benchmark" ]; then
    FRAMES=${FRAMES:-600}
else
    FRAMES=${FRAMES:-1200}
fi

cd "$NDS_DIR" || exit 1

export HOME="$NDS_DIR"
export PATH="$NDS_DIR:$PATH"
export LD_LIBRARY_PATH="${SDL2_DIR:+$SDL2_DIR:}$NDS_DIR/libs:$LD_LIBRARY_PATH"
export SDL_VIDEODRIVER=mmiyoo
export SDL_AUDIODRIVER=mmiyoo
export EGL_VIDEODRIVER=mmiyoo

. /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh 2>/dev/null || true

# Back up player config & settings; always restore on exit
CFG="$NDS_DIR/config/drastic.cfg"
SET="$NDS_DIR/resources/settings.json"
CFG_BAK=/tmp/drastic.cfg.bench_bak
SET_BAK=/tmp/settings.json.bench_bak
[ -f "$CFG" ] && cp "$CFG" "$CFG_BAK"
[ -f "$SET" ] && cp "$SET" "$SET_BAK"

SYS_JSON=/mnt/SDCARD/system.json
orig_vol=""
if [ "${NO_MUTE:-0}" != "1" ] && [ -f "$SYS_JSON" ]; then
    orig_vol=$(sed -n 's/^[[:space:]]*"vol":[[:space:]]*\([0-9]*\).*/\1/p' "$SYS_JSON" | head -n 1)
    if [ -n "$orig_vol" ] && [ "$orig_vol" != "0" ]; then
        sed 's/^\([[:space:]]*"vol":[[:space:]]*\)[0-9][0-9]*/\10/' "$SYS_JSON" > "$SYS_JSON.tmp" &&
            mv -f "$SYS_JSON.tmp" "$SYS_JSON" && touch /tmp/settings_changed
    fi
fi

sv=$(cat /proc/sys/vm/swappiness 2>/dev/null)
ra_file=/sys/block/mmcblk0/queue/read_ahead_kb
ra=$(cat "$ra_file" 2>/dev/null)
pc=$(cat /proc/sys/vm/page-cluster 2>/dev/null)
orig_gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)

STATES_DIR=/mnt/SDCARD/Saves/CurrentProfile/states/Drastic
ROM_BASE=$(basename "$ROM_PATH")
ROM_BASE=${ROM_BASE%.*}
AUTO_DSS="$STATES_DIR/${ROM_BASE}_10.dss"
AUTO_BAK="$STATES_DIR/.dsbench_bak_10.dss"
DSV_FILE="$STATES_DIR/${ROM_BASE}.dsv"
DSV_BAK="$STATES_DIR/.dsbench_bak.dsv"
had_auto=0
had_dsv=0
if [ -f "$AUTO_DSS" ]; then
    cp "$AUTO_DSS" "$AUTO_BAK" && had_auto=1
fi
if [ -f "$DSV_FILE" ]; then
    cp "$DSV_FILE" "$DSV_BAK" && had_dsv=1
fi

cleanup() {
    if [ -n "$orig_vol" ] && [ "$orig_vol" != "0" ] && [ -f "$SYS_JSON" ]; then
        sed "s/^\\([[:space:]]*\"vol\":[[:space:]]*\\)[0-9][0-9]*/\\1$orig_vol/" "$SYS_JSON" > "$SYS_JSON.tmp" &&
            mv -f "$SYS_JSON.tmp" "$SYS_JSON" && touch /tmp/settings_changed
    fi
    [ -f "$CFG_BAK" ] && cp "$CFG_BAK" "$CFG" && rm -f "$CFG_BAK"
    [ -f "$SET_BAK" ] && cp "$SET_BAK" "$SET" && rm -f "$SET_BAK"
    if [ "$had_auto" = "1" ] && [ -f "$AUTO_BAK" ]; then
        mv -f "$AUTO_BAK" "$AUTO_DSS"
    elif [ "$had_auto" = "0" ]; then
        rm -f "$AUTO_DSS"
    fi
    if [ "$had_dsv" = "1" ] && [ -f "$DSV_BAK" ]; then
        mv -f "$DSV_BAK" "$DSV_FILE"
    elif [ "$had_dsv" = "0" ]; then
        rm -f "$DSV_FILE"
    fi
    [ -n "$sv" ] && echo "$sv" > /proc/sys/vm/swappiness 2>/dev/null
    [ -n "$ra" ] && echo "$ra" > "$ra_file" 2>/dev/null
    [ -n "$pc" ] && echo "$pc" > /proc/sys/vm/page-cluster 2>/dev/null
    /mnt/SDCARD/.tmp_update/bin/cpuclock 1200 >/dev/null 2>&1 || true
    [ -n "$orig_gov" ] && { echo "$orig_gov" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
}
trap cleanup EXIT INT TERM

# Clean deterministic benchmark config
if [ -f "$CFG" ]; then
    FS_TYPE=${FRAMESKIP:-0}
    FS_VAL=${FRAMESKIP_VAL:-1}
    T3D=${THREADED3D:-1}
    H3D=${HIRES3D:-0}
    sed -i \
        -e "s/^frameskip_type = .*/frameskip_type = $FS_TYPE/" \
        -e "s/^frameskip_value = .*/frameskip_value = $FS_VAL/" \
        -e "s/^threaded_3d = .*/threaded_3d = $T3D/" \
        -e "s/^hires_3d = .*/hires_3d = $H3D/" \
        -e "s/^show_frame_counter = .*/show_frame_counter = 0/" \
        -e "s/^fast_forward = .*/fast_forward = 0/" \
        "$CFG"
    if [ -n "$SLOT" ]; then
        sed -i -e "s/^savestate_number = .*/savestate_number = $SLOT/" "$CFG"
    fi
    if [ -n "$SWAP" ]; then
        sed -i -e "s/^screen_swap = .*/screen_swap = $SWAP/" "$CFG"
    fi
fi

if [ -n "$LAYOUT" ] && [ -f "$SET" ]; then
    sed -i -e "s/\"mode\":[[:space:]]*[0-9]*/\"mode\":$LAYOUT/" "$SET"
fi

# System clocks & VM settings (cpuclock programs PLL directly and sets governor=userspace; sysfs stays capped at 1200000)
clk_set=$(/mnt/SDCARD/.tmp_update/bin/cpuclock "$MHZ" 2>/dev/null || cpuclock "$MHZ" 2>/dev/null || echo "err")
CPU1=/sys/devices/system/cpu/cpu1/online
[ -f "$CPU1" ] && [ "$(cat "$CPU1" 2>/dev/null)" = "0" ] && { echo 1 > "$CPU1"; } 2>/dev/null

echo 10 > /proc/sys/vm/swappiness 2>/dev/null
if [ "${RAW_VM:-0}" = "0" ]; then
    echo 32 > "$ra_file" 2>/dev/null
    echo 0 > /proc/sys/vm/page-cluster 2>/dev/null
fi

if [ -f "$SET" ]; then
    l_swap=$(jq ".swap_l1l2" "$SET" 2>/dev/null)
    if [ "$l_swap" = "1" ]; then touch /tmp/drastic_swap_l1l2; else rm -f /tmp/drastic_swap_l1l2; fi
fi

# Build LD_PRELOAD chain: libdsfast.so BEFORE libdsbench.so so bench times real hardware ioctl!
PRELOAD=""
if [ "$DSFAST" = "1" ] && [ -f "$NDS_DIR/libs/libdsfast.so" ]; then
    PRELOAD="$NDS_DIR/libs/libdsfast.so"
fi
if [ -n "$EXTRA_LIBS" ]; then
    PRELOAD="${PRELOAD:+$PRELOAD:}$EXTRA_LIBS"
fi
if [ "$MODE" = "play" ] && [ -f /tmp/libdsbench.so ]; then
    PRELOAD="${PRELOAD:+$PRELOAD:}/tmp/libdsbench.so"
fi

SHOT_OUT=/tmp/dsshot_${TAG}.bgra
rm -f "$FLIP_LOG" "$EMU_LOG" "$RAW_LOG" "$SHOT_OUT" /tmp/dsbench_hwm
{
    echo "# $(date '+%Y-%m-%d %H:%M:%S') up $(cut -d' ' -f1 /proc/uptime) s," \
        "cpuclock=${clk_set}MHz ($(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null))," \
        "$(cat /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)," \
        "bat $(/customer/app/axp_test 2>/dev/null)"
    echo "# mode=$MODE dsfast=$DSFAST mhz=$MHZ frames=$FRAMES slot=${SLOT:-boot} layout=${LAYOUT:-cfg} fs=${FRAMESKIP:-0} t3d=${THREADED3D:-1} hires=${HIRES3D:-0} swap=${SWAP:-cfg} sdl2=${SDL2_DIR:-stock} raw_vm=${RAW_VM:-0} rom=$ROM_NAME${EXTRA_ARGS:+ extra=$EXTRA_ARGS}"
} > "$OUT"

ARGS=""
[ -n "$EXTRA_ARGS" ] && ARGS="$EXTRA_ARGS"
[ -n "$IR" ] && ARGS="$ARGS --input-playback $IR"
if [ "$MODE" = "benchfull" ]; then
    ARGS="$ARGS --bench-full $FRAMES"
elif [ "$MODE" = "benchmark" ]; then
    ARGS="$ARGS --benchmark $FRAMES"
else
    export DSBENCH_LOG="$FLIP_LOG"
    export DSBENCH_EMU_LOG="$EMU_LOG"
    export DSBENCH_FRAMES="$FRAMES"
    [ -n "$SLOT" ] && export DSBENCH_LOAD_SLOT="$SLOT"
    [ -n "$HOLD_MASK" ] && export DSBENCH_HOLD_MASK="$HOLD_MASK"
    [ -n "$HOLD_FROM" ] && export DSBENCH_HOLD_FROM="$HOLD_FROM"
    [ -n "$HOLD_LEN" ] && export DSBENCH_HOLD_LEN="$HOLD_LEN"
    [ "${SHOT:-1}" = "1" ] && export DSBENCH_SHOT="$SHOT_OUT"
fi

# Watchdog (180 s)
( sleep 180; killall -9 drastic 2>/dev/null ) &
W=$!

if [ -n "$PRELOAD" ]; then
    LD_PRELOAD="$PRELOAD" ./drastic $ARGS "$ROM_PATH" > "$RAW_LOG" 2>&1 &
else
    ./drastic $ARGS "$ROM_PATH" > "$RAW_LOG" 2>&1 &
fi
P=$!

(
    h=""
    while kill -0 "$P" 2>/dev/null; do
        x=$(awk '/^VmHWM/ { print $2 }' /proc/"$P"/status 2>/dev/null)
        [ -n "$x" ] && h="$x"
        sleep 1
    done
    echo "$h" > /tmp/dsbench_hwm
) &
S=$!

wait "$P"
rc=$?
wait "$S"
kill "$W" 2>/dev/null || true

sync
echo "# exit=$rc peak_vmhwm=$(cat /tmp/dsbench_hwm 2>/dev/null) kB end_$(cat /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)" >> "$OUT"
if [ "$MODE" = "benchfull" ] || [ "$MODE" = "benchmark" ]; then
    grep -E "run time:|Benchmark phase|Benchmark complete" "$RAW_LOG" >> "$OUT" 2>/dev/null || true
else
    echo "flips=$(wc -l < "$FLIP_LOG" 2>/dev/null || echo 0) emu=$(wc -l < "$EMU_LOG" 2>/dev/null || echo 0)" >> "$OUT"
fi
