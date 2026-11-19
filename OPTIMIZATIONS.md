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

### 6. ~~Large-ROM Memory Pressure & `madvise(..., MADV_RANDOM)`~~ — **TESTED & REVERTED (`logs_madv`)**
- **Where:** `core/nds_file.c` L32/L125, `core/memory.c` L9646–9765, `core/gamecard.c` L896 (`mmap(NULL, size_trimmed, PROT_READ, MAP_SHARED, fd, 0)` on `128 MB` unzipped ROMs).
- **Measured A/B vs Tier 3c #1 (`GTA`, `1800 MHz`, `hires_3d=1`, `32 bpp`, `MODE=play` 600 frames, `read_ahead_kb=32` + `page-cluster=0`, `SHOT MD5 MATCH=True`):** Wrapped `mmap`/`mmap64` in `dsfast.c` (`__NR_madvise = 220`, `MADV_RANDOM = 1` for `PROT_READ|MAP_SHARED` >= 4 MB).
  - *GTA car (`slot 2`, driving scene)*: `emu 16.69 / 16.60 -> 16.66 / 16.70 ms` (`flips 475 / 479 -> 469 / 468`, `p99 33.43 / 33.13 -> 32.81 / 34.03 ms`, `>50ms` flip `4 / 2 -> 8 / 3`). Page faults on `play` frames `60..599` were identical: round 1 `maj=8 min=616` vs `maj=8 min=617` (`init 0..59: 1078.9 -> 1100.0 ms, maj=1`); round 2 `maj=0` in both; `peak_vmhwm` `126944–127020` vs `126900–126984 kB`.
  - *GTA bridge (`slot 1`)*: `emu 20.71 / 20.82 -> 20.61 / 20.52 ms` (`flips 544 / 559 -> 556 / 554`, `maj 2 / 0 -> 2 / 0`, `>33ms 213 / 223 -> 203 / 200`).
  - *GTA old (`slot 0`)*: `emu 16.50 -> 16.42 ms` (`flips 481 -> 488`, `maj 1 -> 2`).
- **Conclusion:** Confirms GTA driving FPS drops (`~46.5–48 fps`) are **CPU-bound (3D 4x rasterization + Main/ARM9 emulation)** rather than large-ROM SD page-fault / readahead bound; virtually every slow driving frame (`25–36 ms`, and the `75–81 ms` frame-58 max spike) happens with `majflt=0`. Reverted `mmap`/`mmap64` hooks from `dsfast.c` (only useful if opening uncached ROMs with `CRC32` fallback or stock `read_ahead_kb=128` across very long multi-minute drives).

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
   - *Reference check (`steward-fu/nds`, 2026-11-17):* Checked [steward-fu/nds](https://github.com/steward-fu/nds) — it does **not** contain a compilable decompilation of DraStic. The shipped emulator still runs proprietary binaries (`drastic16` / `drastic32` / `drastic64`, e.g., Pandora ELF) with `LD_LIBRARY_PATH` library overrides (`sdl2`, `alsa`) and runtime binary hooking (`detour/libdtr.so`). Its `re-drastic/{16,32,64}/` folders hold only raw Ghidra C/disasm dumps (`decom/`, `disasm/`) with no build system or module split, unlike `G:\Emulacja\drastic-decompile` (which maps `r2.5.0.4` via `addr2line` into `modules/core/*` and `modules/asm/*`).
### 8. ~~Dynamic 3D Bin Work-Stealing (`video_3d_render_bins_4x`) + Child `nice` Hierarchy~~ — **DONE (Tier 3c #1)**
- **Where:** `onion/dsfast_neon.S` (`dsfast_next_bin`, `dsfast_prep_bins_4x` at `0x080785ac`), `onion/dsfast.c` (`dsfast_render_bins_4x` at `0x08077b80`, `tramp_run` nice hierarchy, `batch3d_max = 2` default when stealing).
- **Root cause found:** In Tier 3b, `dsfast_init` set Main Thread `setpriority(-5)` before `pthread_create`, and Linux `clone()` inherited `-5` into `flip_thread`, `Engine B`, and `w0`. With work-stealing active, `w0` (`nice -5`) preempted `Engine B` (`2D 0.93 -> 2.06 ms` in MKDS) and stole 6/12 bins in `GTA_OLD` (`p50 23.4 -> 27.1 ms`).
- **Fix & measured A/B vs Tier 3b (`1800 MHz`, `hires_3d=1`, `32 bpp`, `SHOT MD5 MATCH=True` on all 4 games):**
  - Explicit priorities `flip (-8) > Main (-5) == Engine B (-5) > w0 (+5)` + atomic 0..11 bin claiming + `batch3d_max = 2` (eliminates Core 1 L1/L2 thrashing between two 144 KB render targets):
    - *MKDS (`slot 0`)*: `emu 21.39 -> 20.77 ms` (`p90 27.89 -> 26.09`, `p99 31.95 -> 30.91`), `flip >50ms 2 -> 0`.
    - *GTA bridge (`slot 1`)*: `emu 21.17 -> 20.65 ms` (`p50 32.98 -> 31.32`, `max 50.83 -> 49.46`), `flip p99 54.49 -> 34.14 ms`, `>50ms 16 -> 5`.
    - *GTA old (`slot 0`)*: `emu 16.84 -> 16.44 ms` (`p50 23.12 -> 22.47`, `p99 39.71 -> 32.87`).
    - *GTA car (`slot 2`)*: `p50 20.80 -> 19.73 ms`, `p99 33.66 -> 32.96 ms`.
9. **Remaining 2D Scanlines on Main Thread (~`3.05 ms` in GTA -> `2.14 ms` after Tier 3b/3c #1):**
   - *Tier 3c #2 tested & reverted (`~0.00–0.03 ms`, `logs_t3c2` + `logs_t3c2_nopld`):* Tested NEON/ARM rewrites for `render_scanline_color_convert_direct_32_2x` (`0x080bf1c4`) and `render_scanline_palette_lookup_8bpp` (`0x080bcab0`) both with `pld` and without `pld` on Miyoo (`1800 MHz`). Both matched `SHOT MD5` on GTA bridge/car (`14.49 -> 14.46 ms` Full, `2.14 -> 2.15 ms` Video 2D), i.e. within run-to-run noise: the 256B planar `src` buffers are already L1D-hot from `render_scanline_2d` on Core 0 and `dst` writes directly to `gfx.dup` CMA memory. Reverted to avoid keeping dead binary patches.
   - *Future `gfx.dup` / `2D` leads (`~0.1–0.3 ms` potential):*
     1. *`MI_SYS_FlushInvCache` Clean-only (`gfx.dup`)* — **CHECKED (not viable / negligible):** Inspected [mi_sys.h](file:///g:/Emulacja/drastic-onion/mmiyoo/inc/mi_sys.h) / `libmi_sys.so` and `/config/modules/4.9.84/mi_sys.ko` on Miyoo: `MI_SYS_IOCTL_FlushInvCache` (`0x4008691b` -> `v7_flush_kern_dcache_area` + `Chip_Flush_MIU_Pipe`) is the only cache op exposed (`DCCMVAC` is PL1-only in ARMv7 and `__ARM_NR_cacheflush` only cleans to PoU without MIU pipe flush). Moreover, a `512x384x4` frame is `768 KB` (ping-pong `1.5 MB`) vs `256 KB` L2 cache, so `gfx.dup` lines evict before the next frame regardless (~`0.00–0.05 ms` ceiling). Only avoiding duplicate `color_convert_direct_32_2x` calls on lines without 3D pixels (`video_2d.c` L8300, L8480) or `render_scanline_2d` (`0x0804b700`) remains worthwhile in 2D.
10. **~~Native 752x560 (Mini v4 / Flip) Triple-Buffering & Audio Backend (`MI_AO` Pop Fix)~~ — DONE:**
    - *Native `752x560` (Mini v4 / Flip):* Done upstream in `a742793e` (`Beta 3`: dynamic `page_h` for `480`/`560` flip & refresh rate measurement, Blur/Pixel persistence, `dsfast.log`).
    - *Audio backend (`2026-11-19`, measured 3x interleaved moving GTA A/B `HOLD_MASK=32`, `1200` frames):* `48 kHz audioserver (SAMPLES=4096, PIPE_SZ=32768)` averaged `1047.3 / 1200` flips (`p90 29.13 ms`) vs `1067.0 / 1200` (`p90 22.42 ms`) for `44.1 kHz MI_AO` (`+19.7` flips); reducing `MI_AO` to `SAMPLES=2048` also regressed (`1033.3` vs `1050.3` flips, `p99 39.21` vs `33.63 ms`, `nivcsw 6234` vs `5858`). Kept **`44.1 kHz MI_AO` (`SAMPLES=8192`)** with `mute_mi_ao()` + 1 silent frame + unconditional `set_volume_raw()` in `alsa/main.c` and `80 ms` pause reset in `dsfast.c` (post-savestate stutter fixed).
11. **Shared 3D/Geometry Hotspots (`gta1_samp` + `mkds_samp`):** `render_polygon_setup_perspective_steps` (`8.2%` GTA / `10.3%` MKDS), `setup_perspective_coefficients` (`6.4%` / `5.6%`), `interpolate_w` (`4.6%` / `4.2%`), `modulate` (`4.4%` / `5.3%`), `process_geometry_commands` (`0x0807ecd0`, `3.5%` Main Thread).
    - *Tier 3c #3 tested & reverted (`~0.00–0.12 ms`, `logs_t3c3`):* Rewrote the 4 hand-coded NEON 3D rasterizer functions (`setup_perspective_coefficients` `0x080c1c30`, `setup_perspective_steps` `0x080c1cd4`, `interpolate_w` `0x080c1dec`, `modulate` `0x080c2924`) with instruction rescheduling, `q`-register ops, 16px unrolling, `pld` on `ns`/`ds`, and eliminating 128B stack `vpush/vpop` in `modulate`. Passed 100% bit-exact in `qemu-arm` and `SHOT MD5 MATCH=True` across all 4 games on Miyoo (`1800 MHz`), **but net frame time stayed flat (`~0.00–0.12 ms`)** (`GTA bridge 3D 5.88 -> 5.76 ms`, `play` equal-temp delta `~0.03 ms`) because all 4 are already Exophase hand-written NEON loops limited by serial `vrecpe/vrecps` data dependencies and L1 cache traffic rather than poor scheduling. (Also note: `steward-fu/nds` `detour/hook.S` uses the same `vrecpe.f32` + 2x `vrecps.f32` algorithm as DraStic's own `0x080c1cd4`.)
    - *Tier 3c #4 tested & reverted (`~0.00–0.02 ms`, `logs_t3c4`):* Replaced scalar `matrix_multiply_4x4_by_3x3` (`0x08079230`) with a 31-instruction NEON implementation and replaced `process_geometry_commands` (`0x0807ecd0`) with a `GCC 13.3 -O3 -mcpu=cortex-a7` C recompilation extracted from `drastic-decompile/recomp/src/geometry.c` (512/512 and 8/8 full-struct rounds bit-exact in `qemu-arm`). Warm SoC A/B on Miyoo (`1800 MHz`, `71–78°C`, `SHOT MD5 MATCH=True` on GTA old/bridge & MKDS; GTA car B2 matched Tier 3c #1 reference `a9b6469017033593d66281110196314f`) showed `~0.00–0.02 ms` delta: `--benchmark 300` *GTA bridge* `Full 14.58 -> 14.53 ms` (`Geom 0.86 -> 0.89 ms`), *MKDS* `Full 18.42 -> 18.17 ms` (`Geom 1.60 -> 1.62 ms`), and `MODE=play` *GTA bridge* `20.74 -> 20.75 ms`, *MKDS* `21.06 -> 21.07 ms`, *GTA car* `16.70 -> 16.70 ms`. The heavy math inside `process_geometry_commands` (`geometry_matrix_multiply_4x4_by_4x4`, `4x4_by_4x3`, `geometry_transform_vertex_block`) was already hand-written NEON in DraStic, and `4x4_by_3x3` is rarely called. Reverted from `dsfast.c` and `dsfast_neon.S`.
    - Remaining true gains lie in **GCC 4.8 C functions** not yet NEONized (e.g. `geometry_flush_polygons` `0x0807a238`, `geometry_transform_vertexes` `0x0807d72c`, `render_polygon_flush_4x` `0x08061f50`, `render_scanline_2d` `0x0804b700`) recompiled with `GCC 13.3 -mcpu=cortex-a7` or vectorized into NEON.
12. **~~30-fps Pair Pacing in `frame_sync` (`onion/dsfast.c`)~~ — DONE (Tier 3c #5, deployed)**
    - **Root cause:** In 30-fps DS games (*GTA*, alternating `~6 ms` light frame + `~27 ms` heavy 3D frame), sleeping `~10.7 ms` at the end of the light frame to hit a `16.76 ms` per-frame target pushed the paired heavy frame to `~43.8 ms`, missing the `33.52 ms` pair deadline and triggering false `late = 1` frameskips.
    - **Fix & measured A/B vs Tier 3c #1 (`1800 MHz`, `hires_3d=1`, `32 bpp`, `logs_pair30/`, `SHOT MD5 MATCH=True`):** `dsfast_system_frame_sync` tracks pure `work_us` and enables pair pacing (`pair30_score >= 6` on `< 12 ms` ↔ `> 18 ms`; `DSFAST_NO_PAIR30=1` toggle), allowing light frames to carry up to `1 * interval` slack so the heavy frame starts immediately:
      - *GTA car (`slot 2`, `frameskip=2`)*: flips **`446 -> 542 / 600`** (`+21.5%`), `flip mean 23.16 -> 18.68 ms`, **`flip p99 50.27 -> 34.02 ms`**, `emu p90 30.39 -> 25.07 ms`.
      - *GTA car (`slot 2`, `frameskip=0`)*: flips **`482/466 -> 522/524`**, `flip p90 33.51/33.52 -> 16.82/16.83 ms`, `emu p90 29.86/29.45 -> 24.63/25.06 ms`.
      - *GTA bridge*, *GTA old*, *MKDS (60 fps)*: zero regression.
13. **Future Idea — 1.5x 3D Resolution or Dynamic Resolution Scaling (`1x / 1.5x / 2x`):**
    - Instead of a fixed `2x` (`512x384`) vs `1x` (`256x192`) toggle for `hires_3d`, investigate rendering 3D at **`1.5x`** (`384x288`, `~2.25x` pixel count vs `4x` for `2x`) or **dynamic scaling (`1x <-> 1.5x <-> 2x`)** driven by frame-time headroom so heavy scenes keep 30/60 fps pacing while light scenes stay sharp. (Requires checking DraStic's `hires_3d` 3D-to-2D scanline composition / `MI_GFX` scaling assumptions.)

---

## Next Measurement Queue
1. **`gta2_samp.bin` (Tier 3c #1 driving profile, `slot 2`, `3528` samples, `flips=371/500`) hotspots:**
   - **3D Core-1 worker (`66.7%` active, `~30%` of active time in GCC 4.8 C):** `render_polygon_load_texels_paletted` (`4.5%`), `render_polygon_4x` (`2.7%`), `render_polygon_interpolate_uv` + `setup_uv_interpolants` (`4.2%`), `render_polygon_flush_4x` (`1.7%`), `depth_compare_less_than` (`1.7%`).
   - **2D Main Thread (`~26%` of active CPU):** skip redundant `color_convert_direct_32_2x` / `palette_lookup_8bpp` on scanlines without 3D pixels (`video_2d.c`) and vectorize/recompile `render_scanline_expand_6bit_split` / `render_scanline_2d` (`0x0804b700`).
