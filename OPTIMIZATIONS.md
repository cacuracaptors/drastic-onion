# DraStic on Miyoo Mini Plus — Optimization Roadmap

**Primary Goal:** Increase game rendering resolution (e.g. `hires_3d = 1` -> `512x384` internal 3D rendering and sharper `MI_GFX` scaling) **while staying within the frame-pacing / fps budget** (judged by `FBIOPAN_DISPLAY` mean fps, `p99`, `max`, and frames `>50 ms`).

> [!IMPORTANT]
> **Bug found in `SDL_video_mmiyoo.c` + `dsfast.c` when `hires_3d = 1` and `screen_swap = 1`:**
> In `sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c` (L1562–1590, L1789–1791), when `hres_mode` is set (`core/video.c` L9286–9287 sets it on both screens), the loop blits `idx = 0` (`DS_screens[0]`) and immediately `break`s. With `screen_swap = 1` (Miyoo default in `drastic.cfg`), `DS_screens[0]` holds the **bottom screen** (e.g. MKDS map) while `DS_screens[1]` holds the **3D top screen**. Meanwhile `dsfast.c` `hidden_screen()` (L581–582) unconditionally returns `1` when `hires_mode` is active, so `hide3d_start_rendering()` disables 3D rendering after 8 frames! For valid `hires_3d = 1` tests/play on the top 3D screen, either `screen_swap = 0` is required OR `SDL_video_mmiyoo.c` + `dsfast.c` must honor `screen_swap` (and/or let the user swap which screen is shown in hires mode).

---

## Tier 1 — Measurable & Actionable Now (`LD_PRELOAD` / `sdl2` / `launch.sh`)

### 1. Eliminate 192 KB / 768 KB `memcpy` on Repeated or Skipped `threaded_3d` Frames
- **Where:** `drastic-decompile/modules/core/video_3d_common.c` (`update_frame_3d_1x` L22784–22789, `update_frame_3d_4x` L25525–25530).
- **Root cause:** When `threaded_3d = 1` and a 3D frame is skipped (`skip_frame != 0`) or unchanged (`!update_framebuffer && !texture_dirty` — i.e., **every other frame in 30 fps 3D games** like *GTA: Chinatown Wars*, *Zelda: PH/ST*, *SM64DS*), DraStic flips `color_buffer_render` to the other ping-pong buffer (`color_buffer_a` <-> `color_buffer_b`) and `memcpy`s **192 KB** (`0x30000` in 1x) or **768 KB** (`0xc0000` in `hires_3d = 1`!) just so `video_3d_finish_rendering()` can set `color_buffer_scan = color_buffer_render`.
- **Fix idea (testable via `LD_PRELOAD`):** Keep `color_buffer_render = color_buffer_last_rendered` on unchanged/skipped frames instead of copying; saves ~23 MB/s DRAM bandwidth in 30 fps `hires_3d` mode.

### 2. ~~Native RGB565 (`--color-depth 16`) vs Default 32 bpp (`ARGB8888`)~~ — **REJECTED (2026-11-17)**
- **Where:** `core/system.c` L446–456 & L588, `core/SDL/SDL_screen.c` L184–190, `core/video_2d.c` L8319–8354, `sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c` L4322.
- **Measured & inspected:** Saved ~1.0–1.1 ms/frame in *Mario Kart DS* `512x384` (`24.1 -> 23.0 ms`), **but visual inspection of `shot_C_r1.png` vs `shot_B_r1.png` showed greenish RGB565 color banding on dark textures — rejected by the owner. Stay on 32 bpp (`ARGB8888`).**

### 3. Asynchronous `MI_GFX_WaitAllDone` & Cutting SDL2 Blit Overhead (`sdl2`)
- **Where:** `sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c` (around L4315–4395 `GFX_Flip` / `GFX_Copy`).
- **Root cause:** `video_handler` on `CORE1` spends ~4 ms/frame composing textures, copying to MMA memory, calling `MI_GFX_BitBlit`, and **blocking synchronously on `MI_GFX_WaitAllDone`** (twice when an overlay is active).
- **Fix idea:**
  - Move `MI_GFX_WaitAllDone` into the pan/flip thread (pattern from `quake2-miyoomini`: cut main thread present overhead from 3.4 ms to 0.58 ms).
  - Write/upload `256x192` / `512x384` directly into MMA buffers and let `MI_GFX` do scale + 180° rotate + format conversion in one hardware pass. This frees `CORE1` milliseconds directly needed for `hires_3d` rasterization.

### 4. Tune 3D Thread Count (`batch_threads_3d_count = 2` vs `3`) on Dual-Core Cortex-A7
- **Where:** `core/video_3d_common.c` L22723–22761 and `onion/dsfast.c:635`.
- **Root cause:** In heavy scenes `dsfast.c` switches `batch_threads_3d_count` from `1` to `3`. On a 2-core CPU, two 3D threads (`render_targets[0]`, `[2]`) contend with each other and with `video_handler` (`nice -10`) on `CORE1`, while `render_targets[1]` contends with CPU/2D emulation on `CORE0`. Because `video_3d_render_bins_*` waits sequentially via `pthread_cond_wait` with no work-stealing, any preemption stalls the whole 3D frame.
- **Test plan:** A/B test `batch_threads_3d_count = 2` (1 thread on `CORE0`, 1 on `CORE1`) vs `3` under `hires_3d = 1`.

### 5. Stop Waking `video_render_thread` Needlessly in Single-Screen Layouts
- **Where:** `core/video.c` L3–29 (`video_render_thread`) & L9473–9497 (`video_render_scanlines`).
- **Root cause:** When `scanlines_rendered == 0 && scanline_end == 0xbf`, `video_render_scanlines` unconditionally wakes `video_render_thread` (`mutex` + `cond_signal` + `cond_wait`) even when only one DS screen is visible (`dsfast.c` skips inside `render_scanline`, after the wake-up already occurred). Meanwhile, mid-frame HDMA register changes (`event.c:242-269`) bypass the worker thread and render both 2D engines sequentially on `CORE0`.

### 6. Large-ROM Memory Pressure & `madvise(..., MADV_RANDOM)`
- **Where:** `core/nds_file.c` L32/L125, `core/memory.c` L9646–9765, `core/gamecard.c` L896.
- **Root cause:** DraStic allocates ~45 MB `.bss` (`nds_system`), ~2.3 MB `polygon_sort_list`, ~4.8 MB `/dev/shm` (`shm_open`), and `mmap`s unzipped ROMs (`128 MB` for *GTA: Chinatown Wars*) with `MAP_SHARED` and no `madvise`. Games missing from `game_database.xml` also run a full-file CRC32 on load.
- **Fix idea (testable via `LD_PRELOAD`):** Apply `madvise(addr, len, MADV_RANDOM)` on `mmap` of `.nds` / `unzipped_rom.nds` to prevent speculative kernel readahead polluting the ~103 MB RAM / swap budget alongside `read_ahead_kb=32` + `page-cluster=0`.

---

## Tier 2 — Deeper Engine / Build Changes (Requires Recompilation or Binary Replacement)

7. **Modern Toolchain (`GCC 13.3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` + LTO + PGO):** Original `drastic` r2.5.0.4 was built with GCC 4.8.x generic ARM32; LTO+PGO typically yields ~7–8.5% free speedup on Cortex-A7 (helps both baseline and `hires_3d`).
8. **3D Work-Stealing / Streaming Rasterization:** Replace static `bin % count` in `video_3d_render_bins_*` with dynamic bin stealing or early polygon dispatch (reducing idle stalls at `video_3d_finish_rendering()`).
9. **2D Engine A/B Split Across Cores:** In pure-2D games or low-3D scenes where `CORE1` is mostly idle, dispatch Engine B to `CORE1` instead of pinning `video_render_thread` to `CORE0`.
10. **Native 752x560 (Mini v4 / Flip) Triple-Buffering & Direct 48 kHz `audioserver` FIFO:** Remove `PAGE_H = 480` restriction in `dsfast.c` and replace `stop_audioserver.sh` + `MI_AO` hack with clean 48 kHz resampling to `/tmp/audio_fifo_server`.

---

## Next Measurement Queue
1. Baseline **`hires_3d = 1` vs `hires_3d = 0`** (strictly 32 bpp) on `GTA: Chinatown Wars` (plus `Super Mario 64 DS`) using `bench_ab.sh` (`FRAMESKIP=0`) with PNG screenshot verification.
2. Benchmark **`memcpy` bypass (768 KB in #1)**, **async `MI_GFX_WaitAllDone` / zero-copy MMA (#3)**, and **`batch_threads_3d_count = 2` vs `3` (#4)** under `hires_3d = 1` (32 bpp, `NDS_DIS_MODE_HRES1` 640x480 fullscreen).
