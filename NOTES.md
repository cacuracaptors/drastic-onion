## Goal (owner, 2026-11-17)

- **Primary goal:** Increase game rendering resolution (`hires_3d = 1` -> `512x384` **fullscreen-scaled to `640x480` (`NDS_DIS_MODE_HRES1`)** per owner preference), **only if we stay within the fps / frame-pacing budget** (measured by both average fps and frame-time spread: p99, longest frame, frames >50 ms). **32 bpp (`ARGB8888`) required: 16 bpp (`RGB565`) is rejected by owner due to unacceptable color banding.** See [OPTIMIZATIONS.md](file:///G:/Emulacja/drastic-onion/OPTIMIZATIONS.md) for the full optimization roadmap.

## Handoff

- **Active branch (`2026-11-17`):** `onion-dsfast` (`c4b1fc2f` committed; local uncommitted Tier 3b in `onion/dsfast.c` & `onion/dsfast_neon.S`, built to `scratch/libdsfast_t3.so` — NOT yet deployed to `/mnt/SDCARD/Emu/NDS/libs/libdsfast.so`). `mlock` is free.
- **Tier 3b verified (`qemu-arm -B 0x100000000` all 9 kernels bit-exact + Miyoo 1800 MHz A/B vs Tier 2 `libdsfast.so`, `hires_3d=1`, `640x480` `32 bpp`):**
  - `MKDS` (`slot 0`, `SHOT MD5 MATCH=True`): `dsemu mean 22.22/23.06 -> 21.05/21.27 ms` (`-1.48 ms`, `+3.1 fps`), `p50 21.21/22.01 -> 19.99/20.20 ms`, `p99 32.87/35.05 -> 31.33/31.02 ms`, `flip mean 22.16/22.98 -> 20.94/21.20 ms`; `--benchmark 300`: `Full 19.25 -> 18.13 ms`, `3D 10.38 -> 9.52 ms`, `2D 1.02 -> 0.86 ms`.
  - `GTA_BRIDGE` (`slot 1`, `SHOT MD5 MATCH=True`): `--benchmark 300`: `Full 16.14 -> 15.61 ms`, `2D 3.53 -> 3.05 ms` (`-13.6%`), `3D 7.09 -> 6.75 ms`; `play`: `dsemu mean 21.59/21.55 -> 21.15/21.33 ms` (`p50 33.01/32.85 -> 32.33/32.27 ms`), `flip mean 22.80/22.81 -> 22.48/22.51 ms`.
  - `GTA_OLD` (`slot 0`): `--benchmark 300`: `2D 3.04 -> 2.83 ms`; `play`: `dsemu mean 17.14/17.17 -> 16.84/17.04 ms`.

### Earlier handoff (`bench`)
- **Repo / branch:** `G:\Emulacja\drastic-onion`, branch `bench` (uncommitted `onion/bench/`, `.gitignore`, `.gitattributes`, `NOTES.md`).
- **Benchmark harness (`onion/bench/`):** `dsbench.c` -> `build/libdsbench.so`, `bench.sh`, `bench_ab.sh` (all LF, `bench.sh` protects `${ROM_BASE}_10.dss` / `.dsv` and restores `cpuclock 1200` + governor on exit; `dsbench.c` uses `__NR_ioctl` syscall 54 so no `dlsym` dependency).
- **ROMs & saves backed up on PC:** 6 ROM `.zip`s in `G:\Emulacja\NDS\Roms\`, all `.dsv`/`.dss` (including GTA `_0.dss`, `_10.dss`, `.dsv`) in `G:\Emulacja\NDS\Saves\Drastic\`. `mlock` is free.
- **Baselines measured on Miyoo (`cpuclock=1800`, `frameskip=0`, `threaded_3d=1`, `FRAMES=600`, `--skip 50`):**
  - `Mario Kart DS` (`SLOT=0`):
    - Stock (`DSFAST=0, hires=0, swap=1`): flips `387`/`388` (337/338 after skip), panel mean `29.6`/`26.9` ms, `p99=50.3` ms, `max=50.3`/`59.9` ms, `>50ms = 19/26`.
    - `libdsfast.so` (`DSFAST=1, hires=0, swap=1`, `shot_A_r1.png` verified 3D race): flips `593`/`593` (543/543 after skip), panel mean `16.8`/`16.8` ms, `p99=16.8`/`16.9` ms, `max=33.5` ms, `>50ms = 0`; `dsemu` mean `16.7`/`16.7` ms.
    - `libdsfast.so` (`DSFAST=1, hires=1, swap=0`, 32 bpp): `dsemu` mean `23.5`/`23.6` ms (`p90=29.9`/`30.3`, `p99=36.8`/`39.4`), panel `23.4`/`23.6` ms (`>50ms = 7/2`); **`shot_B_r1.png` revealed `SDL_video_mmiyoo.c` bug**: top half corrupted + bottom screen (map) displayed instead of 3D screen.
    - `libdsfast.so` (`DSFAST=1, hires=1, swap=0, --color-depth 16`): `dsemu` mean `22.5`/`22.5` ms (**-1.0 ms/frame vs 32 bpp**, `p90=28.9`/`28.5`, `p99=36.2`/`35.3`), panel `22.4`/`22.5` ms (`>50ms = 3/3`); `shot_C_r1.png` clean (no 32 bpp buffer collision) but still displayed map (`dma.phyAddr[0]` vs `sdl_malloc` parity).
  - **Fix applied & verified (`sdl2/src/video/mmiyoo/SDL_video_mmiyoo.c`):** removed `FUN_MALLOC`/`FUN_REALLOC`/`FUN_FREE` hooks and `nds.screen.dma` allocs (-1.5 MB RAM), copy active screen (`VAR_SDL_SCREEN1_PIXELS` when `hres=1`, matching `S0`/`S1` and `screen_swap`) via `neon_memcpy` into `gfx.dup.virAddr`, skip `idx=0` in `process_screen()` when `hres=1`, and compute dynamic `h*pitch` offset for `pixels[1]`. Rebuilt in WSL1 (`/opt/miyoomini-toolchain`, max `GLIBC_2.27`) -> tested via `SDL2_DIR=/tmp/sdl2_test` on `Mario Kart DS` (`SLOT=0`, `FRAMES=600`, `ROUNDS=1`):
    - `shot_B_r1.png` (32 bpp) and `shot_C_r1.png` (16 bpp): clean, sharp `512x384` 3D race in `HRES0` (user noted border = `HRES0` 1:1 `512x384`; `HRES1` scales to `640x480`). `shot_C_r1.png` shows minor RGB565 greenish banding on dark brown wall tiles vs `shot_B_r1.png`.
    - `A` (`256x192` stock SDL2): `dsemu` mean `16.7` ms, panel `16.8` ms (`>50=0`).
    - `B` (`512x384` fixed SDL2, 32 bpp): `dsemu` mean `24.1` ms (`p90=30.1`, `p99=38.9`), panel `24.1` ms (`>50=7`).
    - `C` (`512x384` fixed SDL2, `--color-depth 16`): `dsemu` mean `23.0` ms (**-1.1 ms/frame**, `p90=29.3`, `p99=36.4`), panel `23.0` ms (`>50=4`).
    - `HRES1` default (`512x384 -> 640x480` fullscreen, 32 bpp, `ROUNDS=2`, `shot_A_r1.png` vs `shot_B_r1.png` in `scratch/logs_mkds_hres1/`):
      - `A` (`256x192` stock): `dsemu` mean `16.7`/`16.7` ms, panel `16.8`/`16.8` ms, `max=17.0`/`33.6`, `>50=0/0`.
      - `B` (`512x384 -> 640x480` `HRES1`, 32 bpp): `dsemu` mean `24.4`/`24.4` ms (`p90=30.2`/`30.0`, `p99=36.9`/`38.2`, `max=41.1`/`42.7`), panel `24.3`/`24.3` ms (`p99=33.6`/`50.3`, `max=50.4`, `>50=2/6`). Clean sharp fullscreen 3D verified visually!
  - `GTA: Chinatown Wars` (`SLOT=0`, 128 MB unzipped ROM):
    - Stock + raw VM (`DSFAST=0, RAW_VM=1`): flips `322`/`321`, panel mean `30.6`/`31.0` ms, `max=50.3`/`67.1` ms, `>50ms = 21/30`; `dsemu` mean `16.6`/`17.0` ms (`p90=24.7`/`25.8` ms).
    - Stock + tuned VM (`DSFAST=0, RAW_VM=0`): flips `323`/`300`, panel mean `32.4`/`32.8` ms, `max=50.3` ms, `>50ms = 30/15`; `dsemu` mean `17.7`/`16.5` ms (`p90=27.3`/`26.7` ms).
    - `libdsfast.so` + tuned VM (`DSFAST=1, RAW_VM=0`): flips `556`/`585`, panel mean `17.7`/`16.9` ms, `p90=16.8` ms, `p99=33.5`/`16.9` ms, `max=33.7`/`33.6` ms, `>50ms = 0`; `dsemu` mean `16.7`/`16.7` ms (`p90=25.3`/`24.2` ms).
    - `HRES1` (`512x384 -> 640x480` fullscreen, 32 bpp, `ROUNDS=2`, `scratch/logs_gta_hres1/`):
      - `A` (`256x192` stock): `dsemu` mean `16.7`/`16.7` ms (`p90=24.1`/`24.2`, `p99=26.0`/`26.7`), panel `16.8`/`16.9` ms (`max=33.6`, `>50=0`).
      - `B` (`512x384 -> 640x480` `HRES1` fixed SDL2): `dsemu` mean `21.1`/`21.3` ms (`p50=27.8`/`28.0`, `p90=34.5`/`34.3`, `p99=43.9`/`43.7`, `max=50.5`/`52.3`), panel mean `21.0`/`21.3` ms (`p99=33.8`/`33.6`, `max=34.3`/`33.8`, `>50=0/0`). Verified visually (`shot_B_r1.png` vs `shot_A_r1.png`): Liberty City 3D in crisp 512x384 (`SUM YUNG GAI` sign legible!). Note: `dsfast` `hide_setup()` skipped on custom SDL2 + double 768 KB copy still active.
- **Subsystem breakdown (`--benchmark 300`, `SLOT=0`, `1800` MHz, `DSFAST=1`, updates skipped, `scratch/logs_subsys/`):**
  - `GTA: Chinatown Wars`:
    - `256x192`: Full `7.54` ms (2D `0.83`, 3D `0.53`, Geom `0.85`, Audio `0.09`, base `NoGeom` `6.16`).
    - `512x384`: Full `17.88` ms (2D `5.19` [+4.36 ms], 3D `7.03` [+6.50 ms], Geom `0.48`, Audio `0.25`, base `NoGeom` `10.38` [+4.22 ms]).
  - `Mario Kart DS`:
    - `256x192`: Full `9.33` ms (2D `0.69`, 3D `1.96`, Geom `1.82`, Audio `0.09`, base `NoGeom` `5.54`).
    - `512x384`: Full `20.20` ms (2D `1.63` [+0.94 ms], 3D `9.40` [+7.44 ms], Geom `1.95`, Audio `0.01`, base `NoGeom` `8.85` [+3.31 ms]).
- **Tier-1 implemented & A/B measured on Miyoo (`1800` MHz, `32 bpp`, `HRES1`, `SLOT=0`, `scratch/logs_tier1/`):**
  - `SDL_video_mmiyoo.c`: direct `gfx.dup.phyAddr` in `GFX_Copy` (eliminates 786 KB `neon_memcpy` -> `gfx.tmp` + duplicate `FlushInvCache`), `__sync_synchronize()` in `sdl_update_screen()`, exported `mmiyoo_get_dis_mode()`.
  - `scratch/dsfast_opt.c` -> `scratch/libdsfast_opt.so`: re-enabled `hide_setup` (`mmiyoo_get_dis_mode` + both `DS_SCREENS[0/1].hires_mode`), bypassed 768 KB (`0xc0000`) `memcpy` in `update_frame_3d_4x` (`0x08078458`: `color_buffer_render = color_buffer_last_rendered`).
  - `GTA: Chinatown Wars`: `--benchmark 300`: Full `17.88 -> 15.72` ms (`2D 5.19 -> 3.58`, `NoGeom 10.38 -> 8.01`). `MODE=play`: `dsemu` mean `21.1/21.3 -> 17.98/17.82` ms (`55.6/56.1` DS fps, `p50 27.8/28.0 -> 24.02/24.88`, `p90 34.5/34.3 -> 32.08/31.52`, `p99 43.9/43.7 -> 39.30/39.13`, `max 50.5/52.3 -> 48.25/48.15`, odd-frame pair drops `~34.5+12.3` -> `~27.1+8.3` ms). PNG `shot_B_r1.png` verified identical.
  - `Mario Kart DS`: `--benchmark 300`: Full `20.20 -> 19.21` ms (`2D 1.63 -> 1.11`, `NoGeom 8.85 -> 7.31`, `3D 10.35`). `MODE=play`: `dsemu` mean `24.4/24.4 -> 23.66/23.13` ms (`42.3/43.2` fps, `r2 p90=29.37`, `p99=34.93`, `max=40.43`), panel `23.55/23.08` ms (`>50=9/3`, `flips=595/596`). PNG `shot_B_r1.png` verified identical. `mlock` released (`miyoo: free`).
- **Tier-2 implemented & A/B measured on Miyoo (`1800` MHz, `32 bpp`, `HRES1`, `SLOT=0`, `scratch/logs_tier2/`):**
  - `SDL_video_mmiyoo.c` + `onion/dsfast.c`: zero-copy ping-pong CMA slots (`gfx.dup` `2x768 KB` swapped directly into `VAR_SDL_SCREEN0/1_PIXELS`, `FUN_FREE`/`FUN_REALLOC` guarded only on those 2 addresses), async `MI_GFX` fence drained in `flip_thread` (`CORE0`) via `mmiyoo_gfx_wait()`/`mmiyoo_set_async_gfx(1)`, plus `DSFAST_BATCH3D=3` vs `2` test.
  - `Mario Kart DS`: `batch3d=2` `--benchmark 300` slower (`20.28` vs `19.21` ms; `3D 11.63` vs `10.35`, `2D 2.38` vs `1.11`). Live `MODE=play`: **`batch3d=3` `dsemu 22.44/22.47` ms (`44.6/44.5` fps, `p90 28.77/28.33`, `p99 34.89/33.31`, `max 37.57/41.66`, `>50=0`), panel `22.43/22.37` ms (`flips=597/594`, `>50=2/2`)** vs `batch3d=2` (`23.97/24.18` ms). PNG `shot_b3_r1.png` identical.
  - `GTA: Chinatown Wars`: `MODE=play`: **`batch3d=3` `dsemu 16.98/17.50` ms (`58.9/57.1` DS fps, `p50 23.64/24.32`, `p90 29.98/31.72`, `p99 41.91/41.59`, `max 44.77/59.29`), panel `19.73/19.85` ms (`flips=496/506`, `>50=1/3`)** vs `batch3d=2` (`18.55/18.36` ms, `p50 27.70`). PNG `shot_b3_r1.png` identical. `mlock` released (`miyoo: free`).
- **Next:** Ask owner whether to make a local commit of Tier 2 (`SDL_video_mmiyoo.c`, `onion/dsfast.c`, `NOTES.md`) and investigate remaining bottlenecks toward MKDS `16.76 ms` (`video_render_thread` #5 wakeups / `3D 4x` NEON / `madvise`).
