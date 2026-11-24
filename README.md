# DraStic for OnionOS, faster (libdsfast)

This fork adds **libdsfast**, a small library loaded by the DraStic launcher on OnionOS / RedOnion
(Miyoo Mini, Mini Plus, Mini v4, Mini Flip). DraStic r2.5.0.4 itself is **not changed**: the library replaces the
frame limiter, the page flip and a few timing and drawing details, and places the threads on the two CPU cores.
Since Beta 5.1 the zip also ships an updated build of steward-fu's SDL2 layer (by ksielick) and of the ALSA layer.

Result: full speed with **every frame drawn** in normal scenes and smooth frame pacing.

## Status: pre-release, please test

This is a **pre-release**. Both CPU cores now work close to 100% of the time while a game runs, so it needs
long play sessions on many devices before a final release. It was developed and measured on a **Miyoo Mini Plus**;
the **Mini v2/v3, Mini v4 and Mini Flip are untested**.

Please open an issue with your device, OnionOS/RedOnion version, the games you played and for how long, and
anything wrong: crashes, freezes, the device turning off, audio problems, slowdowns or stutters. Please attach
`Emu/NDS/dsfast.log` (written when you leave a game: screen mode, flip mode, measured refresh rate, frames per second,
device model, real CPU clock, emulation time per frame and frames skipped).

## Install

1. Download the zip of the latest release (`DraStic-dsfast-Onion-<version>.zip`) from the Releases page.
2. Unzip it to the root of the SD card, replacing files when asked. It contains only:
   - `Emu/NDS/launch.sh` (the Onion launcher with the changes below)
   - `Emu/NDS/libs/libdsfast.so`
   - `Emu/NDS/libs/libasound.so.2` (sound library with ksielick's fixes: no pop when a game opens or closes)
   - `Emu/NDS/libs/libSDL2-2.0.so.0` (DraStic's SDL2 layer with ksielick's changes: no screen copy per frame,
     high resolution with swapped screens fixed)
3. Play. Saves, save states, layouts and settings are kept.

To go back, reinstall the Onion package (it puts back the original `launch.sh`, `libSDL2-2.0.so.0` and
`libasound.so.2`) and delete `Emu/NDS/libs/libdsfast.so`.

## Measured (Mario Kart DS, recorded race replayed with DraStic's own input recording)

| | Onion original | with libdsfast |
|---|---|---|
| Frames drawn | ~70% (auto frameskip) | 100% in normal scenes |
| Frames on screen per second | ~42 | ~59.6 (the panel's refresh rate) |
| Screen refreshes showing exactly the next frame | - | 99.8-99.9% (also at 1600 MHz, the Mini/Flip clock) |

Other games measured: Zelda Phantom Hourglass and Spirit Tracks, Super Mario 64 DS, GTA Chinatown Wars, Metroid Prime
Pinball. Heavy scenes with 3D on both screens (e.g. Spirit Tracks' title screen) are still below full speed; there
the 3D split (item 11) raised exact frames from 84% to 93% on the Mini Plus.

## What it does

All details, with the measurements behind each one, are in the comments at the top of [`onion/dsfast.c`](onion/dsfast.c).

1. **Frame limiter** rewritten without the 10 ms kernel tick: the Miyoo's kernel wakes sleeping threads only
   every 10 ms, so DraStic's waits were too long. Waits now sleep only the safe part and spin for the rest.
2. **Page flip without blocking** (3 framebuffer pages; the flip waits for the vsync in its own thread), on the
   640x480 screens and, since Beta 3, on the 752x560 screen of the Mini v4 and Flip.
3. **Video thread woken as soon as a frame is ready** (instead of polling every 10 ms).
4. **Limiter follows the screen**: the panel refreshes at ~59.67 Hz, not 60.
5. **Sound stretched by the same 0.55%** so the sound chip never runs dry (no clicks).
6. **Threads placed on the two cores**: emulation, 2D and flip on core 0; 3D, audio and video on core 1.
7. **SDL video thread at a higher priority** than the 3D thread on core 1.
8. **Hidden screen not drawn** in single-screen layouts (and its 3D not rendered when nothing reads it back).
9. **DraStic menus with less lag**.
11. **3D split over both cores** only while the emulation keeps waiting for the 3D (3D on both screens).
12. **Frameskip only for real peaks**: a frame is skipped only when the emulation is more than 6 ms late
    (DraStic: 3 ms). Spirit Tracks' opening at 1600 MHz: frames shown per second 51.7 -> 54.7, no slowdown.
13. **A clock that never jumps back**: DraStic's own clock jumped back ~71 minutes every ~71.6 minutes of play
    and whenever the system time changed, which froze the game (fixed in Beta 2, reported and fixed by
    Kmieciu4ever).
14. **Blur remembered**: the Blur/Pixel choice of the screen (MENU+B) is kept between sessions (Beta 3).
15. **Menu settings remembered**: the CPU clock chosen in the menu (MENU+START) and the screen the stylus is on
    are kept between sessions (Beta 4).
16. **Screen copy off the emulation**: the copy of each finished frame for the video thread runs on the second
    core, so the emulation goes on at once: fewer stutters (Beta 4).
17. **Faster high-resolution 3D** (`hires_3d = 1`, Beta 5): the 4x polygon drawing and the vertex lighting
    redone in NEON (bit-exact), the hidden Engine B skipped, the 3D shared between both cores and thread
    priorities that apply only in high resolution (ksielick's PRs 6 and 7). GTA Chinatown Wars, recorded drive:
    2315 -> 3359 frames on screen in 75 s. A delay over 100 ms is dropped instead of being made up by running
    fast, and the sound resyncs after an 80 ms gap (ksielick's PR 5).
18. **No screen copy at all** (Beta 5.1, ksielick): the SDL2 layer hands each finished frame straight to the 2D
    accelerator (MI_GFX) from two alternating buffers, in low and high resolution. High resolution with swapped
    screens (`screen_swap = 1`) showed a frozen picture with the old layer; now it is drawn, full screen.
    Mario Kart DS in high resolution, same recorded race: frames on screen 4368 -> 4557 in 80 s, frames shown
    for two refreshes or more halved; GTA Chinatown Wars in high resolution 3613 -> 3785; low resolution the same
    or slightly lighter (9.2 -> 8.0 ms per frame in Mario Kart DS).
19. **Steady 30 fps pacing** (Beta 5.2, ksielick's PR 4 adapted): when a game alternates a light and a heavy
    frame (30-fps games, or manual frameskip 1 in high resolution), the light frame lends its wait to the heavy
    one, so each picture stays two refreshes on screen more evenly. `DSFAST_NO_PAIR30=1` turns it off. No effect
    on 60-fps games (Mario Kart DS at 640x480: no frame affected).
20. **Screen layout kept after high resolution** (Beta 5.3, reported by Successful-Engine-11): closing a game
    while in high resolution used to save the default layout (display 8) in `settings.json`, so the next game in
    low resolution lost the layout chosen in the menu. The SDL2 layer now saves the low-resolution layout that was
    in use before high resolution.

The launcher (`launch.sh`) also:
- sets the CPU clock by model: 1800 MHz on the Mini Plus, 1600 MHz on the Mini v2/v3/v4 and Flip, and puts it back
  to Onion's default (1200 MHz) and the previous CPU governor on exit, so the menu and other apps don't keep running
  at the high clock (Beta 3.1);
- reads the SD card 32 KB at a time and brings back one page per swap fault while DraStic runs (fewer stalls in
  big games such as GTA Chinatown Wars), restoring the system values on exit;
- on the first start, sets in `config/drastic.cfg`: DraStic's 3D thread on (`threaded_3d = 1`) and automatic
  frameskip with at most 1 frame skipped in a row (`frameskip_type = 2`, `frameskip_value = 1`): every frame is
  drawn in normal scenes, and the heaviest moments skip a frame instead of slowing down. This is done once; later
  changes in DraStic's menu are kept.

The library acts only inside the `drastic` executable and only if it is the exact DraStic r2.5.0.4 build
(it checks the code at the patched addresses); otherwise it does nothing.

## Build

The release library is built with clang (no libc headers needed):

```
clang --target=armv7a-linux-gnueabihf -mfloat-abi=hard -mcpu=cortex-a7 -mfpu=neon-vfpv4 -ffreestanding -O2 -fPIC -shared -nostdlib -fuse-ld=lld \
      -o libdsfast.so dsfast.c dsfast_neon.S
```

The SDL2 layer (`sdl2/`) is built with the Miyoo Mini GCC 8.3 toolchain
([shauninman v0.0.3](https://github.com/shauninman/miyoomini-toolchain-buildroot/releases/tag/v0.0.3)):
`make -C detour MOD=mmiyoo`, then in `sdl2/` `./autogen.sh`, `MOD=mmiyoo ./configure --host=arm-linux`
with the video, audio and input backends other than mmiyoo disabled, and `make`. libdsfast finds the layer's data
through the `mmiyoo_get_nds`, `mmiyoo_get_dis_mode` and `mmiyoo_get_down_scale` exports (with the stock v1.8 layer
it falls back to fixed offsets).

## Credits

- DraStic by Exophase.
- Miyoo port of DraStic (SDL2 and ALSA layer) by [steward-fu](https://github.com/steward-fu/nds). This repository is
  a fork of it; the original README (all devices, building the SDL2 layer) is in
  [README_steward-fu.md](README_steward-fu.md).
- Onion integration by Sichroteph and schmurtzm ([OnionUI/Onion](https://github.com/OnionUI/Onion)).
- libdsfast by Régis ([cacuracaptors](https://github.com/cacuracaptors)), with Claude.
- Long-session freeze fix (Beta 2) by Kmieciu4ever.
- CPU clock restored on exit (Beta 3.1) by [ksielick](https://github.com/ksielick).
- Sound fixes, NEON kernels, high-resolution thread priorities and work-stealing (Beta 5), and the zero-copy SDL2
  layer (Beta 5.1) by [ksielick](https://github.com/ksielick).

## License

GPL-3.0, like the rest of this repository.

## AI-Assisted

Claude Opus 5.5 was used to write most of the code and to speed up debugging and optimization. Even though I'm no software engineer, I have a background in the modding community, so I understand how basic rendering tricks work. I don't write complex code myself (that's why I use Claude) but I understand how its logic works and what's going on most of the time, so I can control what Claude is doing more tightly.
