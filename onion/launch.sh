#!/bin/sh
mydir=$(dirname "$0")
cd $mydir

export HOME=$mydir
export PATH=$mydir:$PATH
export LD_LIBRARY_PATH=$mydir/libs:$LD_LIBRARY_PATH
export SDL_VIDEODRIVER=mmiyoo
export SDL_AUDIODRIVER=mmiyoo
export EGL_VIDEODRIVER=mmiyoo

. /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh

CUST_LOGO=0
CUST_CPUCLOCK=1

# Uncomment this section if you need to customize the Drastic menu logo :
# if [ "$CUST_LOGO" == "1" ]; then
# echo "convert resources/logo/0.png to drastic_logo_0.raw"
# echo "convert resources/logo/1.png to drastic_logo_1.raw"
# ./png2raw
# fi

# True when the framebuffer is 752x560, two pages, 32 bpp (what DraStic
# expects on the Mini v4 and Flip).
fb_is_752x560() {
    fbset 2> /dev/null | grep -q "geometry 752 560 752 1120 32"
}

ensure_752x560_mode() {
    fb_is_752x560 && return 0
    before=$(fbset 2> /dev/null | grep -o "geometry.*")
    fbmode=/mnt/SDCARD/.tmp_update/bin/fbmode
    if [ ! -x "$fbmode" ] || ! "$fbmode" 752x560 --pages 2 --preclear --no-clear --timeout 500; then
        fbset -g 752 560 752 1120 32
    fi
    tries=0
    while ! fb_is_752x560 && [ $tries -lt 20 ]; do
        sleep 0.05
        tries=$((tries + 1))
    done
    # Kept small: one line each time the mode had to be fixed.
    echo "$(date '+%F %T') DraStic start: was '${before:-unknown}', now '$(fbset 2> /dev/null | grep -o "geometry.*")' after $tries waits" \
        >> /mnt/SDCARD/.tmp_update/logs/drastic_mode.log 2> /dev/null
}

resolution=$(head -n 1 /tmp/screen_resolution)
if [ "$resolution" = "752x560" ]; then
    USE_752x560_RES=1
else
    USE_752x560_RES=0
fi

sv=$(cat /proc/sys/vm/swappiness)

echo 10 >/proc/sys/vm/swappiness # 60 by default

# Big ROMs (e.g. GTA Chinatown Wars, 128 MB) do not fit in the 103 MB of RAM: DraStic maps the unzipped ROM and
# the game waits for the SD card whenever it reaches a part that is not in memory. Reading 32 KB at a time
# instead of 128 KB makes each wait shorter (measured: frames over 20 ms 130-173 -> 95-103, worst 104 -> 80-87 ms).
ra_file=/sys/block/mmcblk0/queue/read_ahead_kb
ra=$(cat $ra_file 2> /dev/null)
echo 32 > $ra_file 2> /dev/null
# The rest of the waits are swap-ins: with the RAM full, idle parts of DraStic's memory go to the swap file on
# the SD card and come back when the game touches them again. Bringing back one page per fault instead of 8
# shortens each wait (GTA, back to back: exact frames 98.99 -> 99.23%, frames over 20 ms 101 -> 80).
pc=$(cat /proc/sys/vm/page-cluster 2> /dev/null)
echo 0 > /proc/sys/vm/page-cluster 2> /dev/null

cd $mydir

# First start with libdsfast: DraStic's 3D thread on, automatic frameskip allowing at most 1 frame in a row
# (frames are only skipped in the heaviest moments, to avoid slowdowns). Done once, so later changes made in
# DraStic's menu are kept.
cfg=config/drastic.cfg
if [ -f $cfg ] && [ ! -f config/.dsfast_defaults ]; then
    sed -i 's/^frameskip_type = .*/frameskip_type = 2/; s/^frameskip_value = .*/frameskip_value = 1/; s/^threaded_3d = .*/threaded_3d = 1/' $cfg &&
        touch config/.dsfast_defaults
fi

# Settings of the SDL menu that it does not save itself, kept by libdsfast when DraStic exits: the screen the
# stylus is on ("touchpad" in settings.json is read at start but never written back) and the CPU clock below.
settings=resources/settings.json
tp=$(cat config/.dsfast_touchpad 2> /dev/null)
case "$tp" in
    0 | 1) sed -i "s/\"touchpad\":[0-9]*/\"touchpad\":$tp/" $settings ;;
esac

orig_gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null)
if [ "$CUST_CPUCLOCK" == "1" ]; then
    # Mini Plus (354) runs at 1800 MHz; Mini v2/v3/v4 (283) and Flip (285): 1600.
    if [ "$(cat /tmp/deviceModel 2> /dev/null)" = "354" ]; then
        mhz=1800
        # the menu's CPU option stops at maxcpu (1600 in Onion's settings.json): let it reach 1800 here
        sed -i 's/"maxcpu":1[0-7][0-9][0-9],/"maxcpu":1800,/' $settings
    else
        mhz=1600
    fi
    # the clock chosen in the menu (CPU), kept by libdsfast in config/.dsfast_cpuclock
    saved=$(cat config/.dsfast_cpuclock 2> /dev/null)
    case "$saved" in
        [4-9][05]0 | 1[0-9][05]0 | 2[0-3][05]0) mhz=$saved ;;
    esac
    cpuclock $mhz
fi

restore_system() {
    echo $sv >/proc/sys/vm/swappiness
    [ -n "$ra" ] && echo $ra > $ra_file 2> /dev/null
    [ -n "$pc" ] && echo $pc > /proc/sys/vm/page-cluster 2> /dev/null
    if [ "$CUST_CPUCLOCK" == "1" ]; then
        cpuclock 1200 > /dev/null 2>&1 || /mnt/SDCARD/.tmp_update/bin/cpuclock 1200 > /dev/null 2>&1 || true
        [ -n "$orig_gov" ] && echo "$orig_gov" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null || true
    fi
}
trap restore_system EXIT INT TERM

l_triggers_swapped=$(jq ".swap_l1l2" "/mnt/SDCARD/Emu/NDS/resources/settings.json")

if [ $l_triggers_swapped -eq 1 ]; then
    touch /tmp/drastic_swap_l1l2
else
    rm -f /tmp/drastic_swap_l1l2
fi

# DraStic's SDL picks its screen layout once, at start, from `fbset`: the
# 752x560 layout only when the framebuffer already reports 752. On the Mini
# v4 and Flip, if the mode is not (or not yet) 752x560 with two pages at
# that moment, DraStic draws its 640x480 layout into it and the screen is
# garbled until it is restarted. Check the mode here and set it if needed.
if [ "$USE_752x560_RES" = "1" ]; then
    ensure_752x560_mode
fi

DSFAST_FAST3D="${DSFAST_FAST3D:-3}" LD_PRELOAD="$(pwd)/libs/libdsfast.so${LD_PRELOAD:+:$LD_PRELOAD}" ./drastic "$1"

sync
restore_system
trap - EXIT INT TERM

# The Mini v4 and Flip go back to 640x480 in runtime.sh (fbmode --preclear),
# which clears the screen first. An fbset here switched the mode before that
# and showed DraStic's last 752x560 frame garbled on exit.
