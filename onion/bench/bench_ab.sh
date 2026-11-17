#!/bin/sh
# Alternating A/B runner for DraStic on Miyoo (copies results to /tmp/dsbench_ab.txt).
# Usage example (from /tmp/cmd_to_run.sh):
#   ROUNDS=2 SLOT=0 FRAMES=1200 A_DSFAST=0 B_DSFAST=1 sh /tmp/bench_ab.sh
#
# Per-variant overrides for A, B, C, D (only A and B run unless C/D are defined):
#   <V>_DSFAST, <V>_EXTRA_LIBS, <V>_EXTRA_ARGS, <V>_MHZ, <V>_RAW_VM,
#   <V>_LAYOUT, <V>_FRAMESKIP, <V>_THREADED3D, <V>_HIRES3D, <V>_SWAP, <V>_SDL2_DIR, <V>_MODE

OUT=/tmp/dsbench_ab.txt
: > "$OUT"
ROUNDS=${ROUNDS:-1}
r=0
while [ "$r" -lt "$ROUNDS" ]; do
    r=$((r + 1))
    for v in A B C D; do
        eval "has=\${${v}_DSFAST+\${${v}_DSFAST}}"
        eval "has2=\${${v}_EXTRA_LIBS+\${${v}_EXTRA_LIBS}}"
        eval "has3=\${${v}_EXTRA_ARGS+\${${v}_EXTRA_ARGS}}"
        eval "has4=\${${v}_RAW_VM+\${${v}_RAW_VM}}"
        eval "has5=\${${v}_MHZ+\${${v}_MHZ}}"
        eval "has6=\${${v}_HIRES3D+\${${v}_HIRES3D}}"
        eval "has7=\${${v}_SWAP+\${${v}_SWAP}}"
        eval "has8=\${${v}_SDL2_DIR+\${${v}_SDL2_DIR}}"
        [ -z "$has$has2$has3$has4$has5$has6$has7$has8" ] && continue

        eval "v_dsfast=\${${v}_DSFAST:-${DSFAST:-1}}"
        eval "v_libs=\${${v}_EXTRA_LIBS:-}"
        eval "v_args=\${${v}_EXTRA_ARGS:-}"
        eval "v_mhz=\${${v}_MHZ:-${MHZ:-1800}}"
        eval "v_rawvm=\${${v}_RAW_VM:-${RAW_VM:-0}}"
        eval "v_layout=\${${v}_LAYOUT:-${LAYOUT:-}}"
        eval "v_fs=\${${v}_FRAMESKIP:-${FRAMESKIP:-0}}"
        eval "v_t3d=\${${v}_THREADED3D:-${THREADED3D:-1}}"
        eval "v_h3d=\${${v}_HIRES3D:-${HIRES3D:-0}}"
        eval "v_swap=\${${v}_SWAP:-${SWAP:-}}"
        eval "v_sdl2=\${${v}_SDL2_DIR:-${SDL2_DIR:-}}"
        eval "v_mode=\${${v}_MODE:-${MODE:-play}}"
        if [ "$r" = "1" ]; then v_shot=${SHOT:-1}; else v_shot=0; fi

        tag="${v}_r${r}"
        TAG="$tag" DSFAST="$v_dsfast" EXTRA_LIBS="$v_libs" EXTRA_ARGS="$v_args" \
            MHZ="$v_mhz" RAW_VM="$v_rawvm" LAYOUT="$v_layout" FRAMESKIP="$v_fs" \
            THREADED3D="$v_t3d" HIRES3D="$v_h3d" SWAP="$v_swap" SDL2_DIR="$v_sdl2" SHOT="$v_shot" MODE="$v_mode" \
            sh /tmp/bench.sh

        {
            echo "## $v (round $r)"
            cat "/tmp/dsbench_${tag}.txt"
        } >> "$OUT"
    done
done
