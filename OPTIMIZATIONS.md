# DraStic on Miyoo Mini Plus — Optimization Roadmap

**Primary Goal:** Increase game rendering resolution (e.g. `hires_3d = 1` -> `512x384` internal 3D rendering and sharper `MI_GFX` scaling) **while staying within the frame-pacing / fps budget** (judged by `FBIOPAN_DISPLAY` mean fps, `p99`, `max`, and frames `>50 ms`).

> [!IMPORTANT]
> **Bug found in `SDL_video_mmiyoo.c` + `dsfast.c` when `hires_3d = 1` and `screen_swap = 1`:**
> In `sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c` (L1562–1590, L1789–1791), when `hres_mode` is set (`core/video.c` L9286–9287 sets it on both screens), the loop blits `idx = 0` (`DS_screens[0]`) and immediately `break`s. With `screen_swap = 1` (Miyoo default in `drastic.cfg`), `DS_screens[0]` holds the **bottom screen** (e.g. MKDS map) while `DS_screens[1]` holds the **3D top screen**. Meanwhile `dsfast.c` `hidden_screen()` (L581–582) unconditionally returns `1` when `hires_mode` is active, so `hide3d_start_rendering()` disables 3D rendering after 8 frames! For valid `hires_3d = 1` tests/play on the top 3D screen, either `screen_swap = 0` is required OR `SDL_video_mmiyoo.c` + `dsfast.c` must honor `screen_swap` (and/or let the user swap which screen is shown in hires mode).

---

## Tier 1 — Measurable & Actionable Now (`LD_PRELOAD` / `sdl2` / `launch.sh`)

### 1. ~~Eliminate 192 KB / 768 KB `memcpy` on Repeated or Skipped `threaded_3d` Frames~~ — **DONE (Tier 1, `42e8904b`)**
- **Where:** `drastic-decompile/modules/core/video_3d_common.c` (`update_frame_3d_1x` L22784–22789, `update_frame_3d_4x` L25525–25530).
- **Root cause & fix:** On skipped/unchanged `hires_3d = 1` frames (every other frame in 30 fps DS games like *GTA*), `onion/dsfast.c` (`copy3d_setup`, `0x08078458`) sets `color_buffer_render = color_buffer_last_rendered` and skips the 768 KB (`0xc0000`) `memcpy`.

### 2. ~~Native RGB565 (`--color-depth 16`) vs Default 32 bpp (`ARGB8888`)~~ — **REJECTED (2026-11-17)**
- **Where:** `core/system.c` L446–456 & L588, `core/SDL/SDL_screen.c` L184–190, `core/video_2d.c` L8319–8354, `sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c` L4322.
- **Measured & inspected:** Saved ~1.0–1.1 ms/frame in *Mario Kart DS* `512x384` (`24.1 -> 23.0 ms`), **but visual inspection of `shot_C_r1.png` vs `shot_B_r1.png` showed greenish RGB565 color banding on dark textures — rejected by the owner. Stay on 32 bpp (`ARGB8888`).**

### 3. ~~Asynchronous `MI_GFX_WaitAllDone` & Cutting SDL2 Blit Overhead (`sdl2`)~~ — **DONE (Tier 2, `ed50aabc`)**
- **Where:** `sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c` (`GFX_Flip` / `GFX_Copy`) and `onion/dsfast.c` (`flip_thread`).
- **Fix applied:** Zero-copy CMA ping-pong slots (`gfx.dup` `2x768 KB` direct render target) + `MI_GFX_WaitAllDone` offloaded via `mmiyoo_gfx_wait()` to `flip_thread` (`CORE0`). `libSDL2-2.0.so.0` dropped to `0.02%` (`1/5237` samples) in `mkds_samp.bin`.

### 4. ~~Tune 3D Thread Count (`batch_threads_3d_count = 2` vs `3`) on Dual-Core Cortex-A7~~ — **TESTED (Tier 2, `batch3d = 3` won)**
- **Measured:** `batch3d = 3` beat `2` on both *MKDS* (`22.44` vs `23.97 ms`) and *GTA* (`16.98` vs `18.55 ms`). Wait/preemption remains the bottleneck (see #8 below).

### 5. ~~Stop Waking `video_render_thread` Needlessly in Single-Screen Layouts~~ — **DONE (Tier 3b, `1628746e`)**
- **Where:** `core/video.c` L9473–9497 (`video_render_scanlines`, `0x08035fac`) and `onion/dsfast_neon.S` (`dsfast_engine_b_tramp`).
- **Root cause & fix:** `callprof_gta1.txt` confirmed Main Thread spent `6.00%` (`24/400` steady samples) at `libpthread:0xd708` (`FUTEX_WAKE`) waking `Engine B` + `3D` on `Core 0`. `dsfast_engine_b_tramp` skips the `mutex`/`cond_signal`/`cond_wait` handshake whenever Engine B is hidden, and `dsfast_init` raises Main Thread `setpriority(-5)` over `w0` (`nice 0`).

### 6. Large-ROM Memory Pressure & `madvise(..., MADV_RANDOM)` (Still Open for Initial Load / Long Play)
- **Where:** `core/nds_file.c` L32/L125, `core/memory.c` L9646–9765, `core/gamecard.c` L896.
- **Root cause:** DraStic `mmap`s `128 MB` unzipped ROMs (*GTA*) with `MAP_SHARED` and no `madvise` (steady `gamecard_read` on bridge was `0%` once warm, though ROM load/savestate unpacking dominated `[0–45%]` of startup).
- **Fix idea (`LD_PRELOAD`):** Apply `madvise(addr, len, MADV_RANDOM)` on ROM `mmap` to prevent speculative readahead alongside `read_ahead_kb=32` + `page-cluster=0`.

---

## Tier 3 (NEON + PLD Binary Patching in `onion/dsfast_neon.S`)

### 6b. **DONE (Tier 3a + 3b, `1628746e`):** 9 Bit-Exact NEON + PLD Kernels
- **3D 4x kernels:** `dsfast_resolve_bin_4x` (`0x080c50fc`), `dsfast_writeback_4x` (`0x080c4a90`, scalar ARM -> NEON), `dsfast_load_depth_4x` (`0x080c49d0`), `dsfast_load_depth_colors_id_4x` (`0x080c4a18`), `dsfast_writeback_all_pass_4x` (`0x080c4b08`), `dsfast_edge_mark_4x` (`0x080c55a0`), `dsfast_edge_identify_4x` (`0x080c5658` — edge marking took `10.2%` active 3D CPU in *GTA*).
- **2D Main-Thread kernels:** `dsfast_render_scanline_set_3d_visibility` (`0x08048c50`, replaced 240-insn spilled GCC function + `gather_3d_alpha` + 256B stack temp buffer with ~55 zero-spill NEON insns), `dsfast_select_pixels_binary32` (`0x080bef80`, fixed `+1024` `pld` overshoot).
- **Measured A/B vs Tier 2 (`1800 MHz`, `hires_3d=1`, `32 bpp`, `SHOT MD5 MATCH=True`):**
  - *MKDS (`slot 0`)*: `dsemu` `22.64 -> 21.16 ms` (`44.2 -> 47.3 fps`, `-1.48 ms`), `3D 10.38 -> 9.52 ms`, `2D 1.02 -> 0.86 ms`.
  - *GTA bridge (`slot 1`)*: `--benchmark` `2D 3.53 -> 3.05 ms` (`-13.6%`), `3D 7.09 -> 6.75 ms`, `Full 16.14 -> 15.61 ms`; `play` `dsemu` `21.57 -> 21.24 ms` (`p50 32.93 -> 32.30 ms`).

---

## Tier 2 & Tier 3c — Remaining Verified Bottlenecks

7. **Modern Toolchain (`GCC 13.3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` + LTO + PGO):** Original `drastic` r2.5.0.4 was built with GCC 4.8.x generic ARM32.
8. **Dynamic 3D Bin Work-Stealing (`video_3d_render_bins_4x`):** `gta1_samp.bin` showed Main Thread blocked `16.75%` (`67/400`) in `pthread_cond_wait`; with Main Thread at `nice -5` on `CORE0`, `w0` (`bins 1,4,7,10`) is starved until 2D finishes while `CORE1` (`w1 + 3d_run`) finishes early and sleeps. Replacing `bin % count` with atomic work-stealing eliminates idle stalls at `video_3d_finish_rendering()`.
9. **Remaining 2D Scanlines on Main Thread (~`3.05 ms` in GTA):** `render_scanline_color_convert_direct_32_2x` (`0x080bf1c4`, `2.25%` steady GTA, zero `pld`s, `6/10` stalled on `vst4.8`), `render_scanline_palette_lookup_8bpp` (`0x080bcab0`, `1.50%`), `render_scanline_2d` (`0x0804b700` C `0x12f0`).
10. **Native 752x560 (Mini v4 / Flip) Triple-Buffering & Direct 48 kHz `audioserver` FIFO:** Remove `PAGE_H = 480` restriction in `dsfast.c` and replace `stop_audioserver.sh` + `MI_AO` hack (also causes speaker startup/exit DC-bias pop in `libasound.so.2`) with clean 48 kHz resampling to `/tmp/audio_fifo_server`.
11. **Shared 3D/Geometry Hotspots (`gta1_samp` + `mkds_samp`):** `render_polygon_setup_perspective_steps` (`8.2%` GTA / `10.3%` MKDS), `setup_perspective_coefficients` (`6.4%` / `5.6%`), `interpolate_w` (`4.6%` / `4.2%`), `modulate` (`4.4%` / `5.3%`), `process_geometry_commands` (`0x0807ecd0`, `3.5%` Main Thread).

---

## Next Measurement Queue
1. Capture post-Tier-3b `ptrace` sample on `GTA bridge (slot 1)` to measure `Core 0`/`Core 1` wait & test **3D atomic bin work-stealing** in `video_3d_render_bins_4x`.
2. Benchmark **Tier 3c** NEON+PLD `render_scanline_color_convert_direct_32_2x` + `render_scanline_palette_lookup_8bpp` (2D Main Thread critical path in GTA).
