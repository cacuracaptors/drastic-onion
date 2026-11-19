# `ksielick/hires3d` — Changes & Optimization Roadmap

## 1. Overview & Relationship to Upstream (`Beta 4`)

This branch is a preview of our **`hires_3d = 1` (`512x384` -> `640x480`, 32 bpp `ARGB8888`)** performance, video, audio, and benchmarking work on Miyoo Mini Plus (`1800 MHz`), prepared on top of `origin/onion-dsfast` **Beta 3.1** (`f5bb90fe`).

It does **not** yet include upstream **Beta 4** (`c2704551`), which landed on `origin/onion-dsfast` right before this preview branch was pushed. Before opening clean PRs on top of Beta 4, the overlapping pieces need to be reconciled and A/B benchmarked:
- **Zero-copy CMA ping-pong in `sdl2`** (`SDL_video_mmiyoo.c` for both `1x` and `4x`, synchronous `MI_GFX_WaitAllDone`) vs Beta 4's `copy_thread` (`sdl_update_screen` on Core 1 inside `onion/dsfast.c`). Cross-thread async `MI_GFX` (`mmiyoo_gfx_wait()` in `flip_thread`) was benchmarked against synchronous `MI_GFX_WaitAllDone()` in both `1x` and `4x` and removed because it gave zero gain in `4x` while causing `<14 ms` phase stutters in `1x` dual-screen (`LAYOUT=8`).
- **`hidden_screen()` & Engine B events handling:** this branch enables hidden Engine B skipping in `hires_3d = 1` via `dsfast_engine_b_tramp` + `dsfast_eb_events_only()`, whereas Beta 4 disables `hidden_screen()` when `hires_3d` is active.

---

## 2. Implemented Changes (`1800 MHz`, `hires_3d = 1`, `32 bpp`)

1. **30-fps Pair Pacing (`onion/dsfast.c`, `pair30`, `DSFAST_NO_PAIR30=1` toggle):**
   - In 30-fps DS games (*GTA: Chinatown Wars*, alternating `~6 ms` light frames and `~27 ms` heavy 3D frames), `frame_sync` tracks pure `work_us` and lets light frames carry up to `1 * interval` (`~16.76 ms`) slack instead of sleeping `~10 ms` in `wait_us()`, allowing the heavy 3D frame to start immediately and make the `33.52 ms` pair deadline.
   - *GTA car (`slot 2`, `frameskip=2`)*: flips **`446 -> 542 / 600` (`+21.5%`)**, `flip mean 23.16 -> 18.68 ms`, `flip p99 50.27 -> 34.02 ms`, `emu p90 30.39 -> 25.07 ms`.
   - *GTA car (`slot 2`, `frameskip=0`)*: flips `482/466 -> 522/524`, `flip p90 33.51/33.52 -> 16.82/16.83 ms`.
   - *MKDS (`60 fps`)*: zero regression (`592 -> 594` flips).

2. **Dynamic 3D 4x Bin Work-Stealing & Child `nice` Hierarchy (`onion/dsfast.c`, `onion/dsfast_neon.S`):**
   - `dsfast_prep_bins_4x` (`0x080785ac`) resets atomic `dsfast_next_bin = 0` and `dsfast_render_bins_4x` (`0x08077b80`) claims bins `0..11` atomically with `bin_interleave = 12` (`DSFAST_NO_STEAL=1` toggle). Defaults `batch3d_max = 2` when stealing is active (1 Core-1 worker + idle-fill `w0` on Core 0, avoiding L1/L2 thrashing between two 144 KB render targets).
   - Explicit priorities in `tramp_run`: **`flip (-8) > Main (-5) == Engine B (-5) > w0 (+5)`** (fixes Linux `clone()` `-5` inheritance that let `w0` preempt Main / Engine B).
   - *MKDS (`slot 0`)*: `emu 21.39 -> 20.77 ms`, `>50 ms` `2 -> 0`.
   - *GTA bridge (`slot 1`)*: `emu 21.17 -> 20.65 ms`, `flip p99 54.49 -> 34.14 ms`, `>50 ms` `16 -> 5`.

3. **9 Bit-Exact NEON + `PLD` Kernels (`onion/dsfast_neon.S`):**
   - **7 3D 4x kernels:** `dsfast_resolve_bin_4x` (`0x080c50fc`), `dsfast_writeback_4x` (`0x080c4a90`), `dsfast_load_depth_4x` (`0x080c49d0`), `dsfast_load_depth_colors_id_4x` (`0x080c4a18`), `dsfast_writeback_all_pass_4x` (`0x080c4b08`), `dsfast_edge_mark_4x` (`0x080c55a0`), `dsfast_edge_identify_4x` (`0x080c5658`).
   - **2 2D Main-Thread kernels:** `dsfast_render_scanline_set_3d_visibility` (`0x08048c50`, ~55 zero-spill NEON insns) and `dsfast_select_pixels_binary32` (`0x080bef80`).
   - *MKDS (`slot 0`)*: `dsemu 22.64 -> 21.16 ms` (`-1.48 ms`); *GTA bridge* 2D `3.53 -> 3.05 ms` (`-13.6%`).

4. **Skip 768 KB 3D Color-Buffer Copy (`onion/dsfast.c`, `copy3d_setup` at `0x08078458`):**
   - On unchanged/skipped `threaded_3d` frames in `hires_3d = 1` (e.g., every other frame in 30-fps games), swaps `color_buffer_render` to `color_buffer_last_rendered` instead of copying `768 KB` (`0xc0000`).

5. **Hidden Engine B Skip + Deferred 2D Events Fix (`onion/dsfast_neon.S`, `onion/dsfast.c`, `66787f29`):**
   - `dsfast_engine_b_tramp` avoids waking `video_render_thread` (`mutex`/`cond`) when Engine B is hidden, and `dsfast_eb_events_only()` calls `video_2d_render_scanlines` (`0x0804f480`) with `screen_ptr` (`v2d + 0x1c`) cleared (preserving any mid-frame `0x305` `POWCNT1` swap) so deferred palette, OAM, and 2D register writes are applied without drawing. Fixes stale-palette glitches on the *GTA CTW* hot-wire dashboard and bottom-screen walking artifacts.

6. **`HRES1` (`512x384` -> `640x480`), `1x` + `4x` Zero-Copy CMA Ping-Pong, Synchronous `MI_GFX` & Stylus Scaling (`sdl2/src/video/mmiyoo/`):**
   - `SDL_video_mmiyoo.c`: fixes `HRES1` (`512x384` fullscreen scaled to `640x480`) + `screen_swap` buffer handling, points DraStic at alternating CMA slots (`gfx.dup`, guarded via `is_dup_ptr()`) in both `1x` and `4x` to skip `384 KB` / `768 KB` `neon_memcpy`, and keeps `MI_GFX_WaitAllDone` synchronous in `GFX_Copy()` (removing cross-thread async `MI_GFX` restores Beta 4-level `1x` pacing at `p99 16.91 ms`, `>35 ms = 0` on MKDS `LAYOUT=8`).
   - `SDL_event_mmiyoo.c`: fixes `640x480` stylus coordinate scaling.

7. **Audio Stability (`alsa/main.c`, `onion/dsfast.c`):**
   - Kept **`44.1 kHz MI_AO` (`SAMPLES=8192`)** after 3x interleaved moving *GTA* A/B tests (`1067.0` vs `1047.3 / 1200` flips against `48 kHz audioserver` FIFO with `SAMPLES=4096, PIPE_SZ=32768`; and `1050.3` vs `1033.3` against `MI_AO SAMPLES=2048`).
   - Added `mute_mi_ao()` (`-60 dB` + `MI_AO_SETMUTE=1` + `20 ms` settle), 1 silent warmup frame, and unconditional `set_volume_raw()` around `MI_AO_Enable`/`MI_AO_Disable` (eliminates sticky `0 dB` gain after re-enable), plus reduced `dsfast.c` audio pause reset threshold from `200 ms` to **`80 ms`** (fixes post-savestate audio stutter).

8. **Benchmark & Verification Harness (`onion/bench/`):**
   - `dsbench.c` (`libdsbench.so`): hooks `FBIOPAN_DISPLAY` and `update_input` with `getrusage` (`majflt`, `minflt`, `nvcsw`, `nivcsw`), framebuffer `SHOT` capture, and deterministic key injection (`DSBENCH_HOLD_MASK`, `HOLD_FROM`, `HOLD_LEN`).
   - `bench.sh`, `bench_ab.sh`, `verify_2d.sh`: automated A/B harness with player config/save-slot protection, `NO_MUTE=1`, and dual-screen 2D regression checks across `SWAP=1`, `SWAP=0`, and `LAYOUT=8`.

---

## 3. Tested & Reverted / Rejected Experiments

- **`--color-depth 16` (`RGB565`) vs `32 bpp` (`ARGB8888`):** Saved `~1.0–1.1 ms/frame` in *MKDS*, but introduced greenish color banding on dark textures — rejected visually.
- **`madvise(..., MADV_RANDOM)` via `mmap`/`mmap64` hooks:** Neutral (`majflt` unchanged on *GTA car*), confirming driving FPS dips are CPU-bound rather than SD page-fault bound.
- **`MI_SYS_FlushInvCache` Clean-only optimization:** Not exposed in `/config/modules/4.9.84/mi_sys.ko` (`MI_SYS_IOCTL_FlushInvCache` is the only op), and `768 KB` frames exceed the `256 KB` L2 cache anyway.
- **Tier 3c #2 / #3 / #4 (`logs_t3c2`, `logs_t3c3`, `logs_t3c4`):** Rewrites of `color_convert_direct_32_2x` + `palette_lookup_8bpp`, 4 hand-coded NEON 3D setup/modulate kernels, `matrix_multiply_4x4_by_3x3`, and `GCC 13.3 -O3` `process_geometry_commands` were all bit-exact (`qemu-arm` + `SHOT MD5`) but flat (`~0.00–0.12 ms`) because those paths are already Exophase NEON bounded by serial `vrecpe/vrecps` dependencies and L1/CMA traffic.
- **`48 kHz audioserver` FIFO (`SAMPLES=512/4096`) and `MI_AO SAMPLES=2048`:** Lower flips and higher `nivcsw` than `MI_AO SAMPLES=8192` in 3x interleaved moving *GTA* benchmarks.

---

## 4. Future Optimization Ideas

1. **Reconcile & Benchmark Against Upstream Beta 4 (`c2704551`):** Compare Beta 4's `copy_thread` (`sdl_update_screen` on Core 1) against `SDL_video_mmiyoo.c` CMA ping-pong in `hires_3d = 1`, and keep Beta 4's CPU clock / stylus screen persistence and `dsfast.log` percentiles.
2. **GCC 4.8 C Hotspots in 3D 4x Rasterizer & Geometry (Core 1 — ~30% active 3D time in `gta2_samp`):** Vectorize or selectively recompile with `GCC 13.3 -O3 -mcpu=cortex-a7`: `render_polygon_load_texels_paletted` (`4.5%`), `render_polygon_4x` (`2.7%`), `render_polygon_interpolate_uv` + `setup_uv_interpolants` (`4.2%`), `render_polygon_flush_4x` (`1.7%`, `0x08061f50`), `depth_compare_less_than` (`1.7%`), `geometry_flush_polygons` (`0x0807a238`), and `geometry_transform_vertexes` (`0x0807d72c`).
3. **2D Main-Thread Scanline Work (~26% active CPU in `gta2_samp`):** Skip redundant `color_convert_direct_32_2x` / `palette_lookup_8bpp` calls on scanlines without 3D pixels (`video_2d.c`), and optimize `render_scanline_expand_6bit_split` / `render_scanline_2d` (`0x0804b700`).
4. **`1.5x` (`384x288`) Internal 3D Resolution or Dynamic Resolution Scaling (`1x <-> 1.5x <-> 2x`):** Render 3D at `~2.25x` pixels instead of `4x` (or scale dynamically from frame-time headroom) so heavy 3D scenes maintain 30/60 fps pacing while light scenes stay sharp.
5. **Selective Recompilation of Hot C Modules:** Build larger pure-C DraStic subsystems with modern `GCC 13.3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` (+ LTO/PGO).

---

## 5. Proposed PR Split on Top of Beta 4

Tracking & isolated PRs opened on `cacuracaptors/drastic-onion` (`onion-dsfast`, see #3):
1. **PR A (`#4`, `pr-pair30-pacing`):** 30-fps pair pacing (`pair30`, `DSFAST_NO_PAIR30=1`).
2. **PR C (`#5`, `pr-audio-fixes`):** `mute_mi_ao()` + unconditional `set_volume_raw()` + `80 ms` audio pause reset.
3. **PR B1 (`#6`, `pr-b1-neon-copy3d`):** 9 bit-exact NEON+PLD kernels (`DSFAST_NO_NEON3D=1`) + 768 KB 3D copy skip (`DSFAST_NO_COPY3D=1`).
4. **PR B2 (next):** hidden Engine B skip + `dsfast_eb_events_only()`, dynamic 3D bin work-stealing (`batch3d_max = 2`), and child `nice` hierarchy.
5. **PR D (`sdl2`):** `HRES1` (`512x384` -> `640x480`) + `640x480` stylus scaling + `1x`/`4x` zero-copy CMA ping-pong (synchronous `MI_GFX`).
