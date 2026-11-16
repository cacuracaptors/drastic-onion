# DraStic for OnionOS, faster (libdsfast)

This fork adds **libdsfast**, a small library loaded by the DraStic launcher on OnionOS / RedOnion
(Miyoo Mini, Mini Plus, Mini v4, Mini Flip). DraStic r2.5.0.4 and steward-fu's SDL2 layer v1.8 (the build that
ships with Onion) are **not changed**: the library only replaces the frame limiter, the page flip and a few
timing details, and places the threads on the two CPU cores.

Result: full speed with **every frame drawn** in normal scenes and smooth frame pacing.

## Status: pre-release, please test

This is a **pre-release**. Both CPU cores now work close to 100% of the time while a game runs, so it needs
long play sessions on many devices before a final release. It was developed and measured on a **Miyoo Mini Plus**;
the **Mini v2/v3, Mini v4 and Mini Flip are untested**.

Please open an issue with your device, OnionOS/RedOnion version, the games you played and for how long, and
anything wrong: crashes, freezes, the device turning off, audio problems, slowdowns or stutters.

## Install

1. Download `DraStic-dsfast-Onion.zip` from the Releases page.
2. Unzip it to the root of the SD card, replacing files when asked. It contains only:
   - `Emu/NDS/launch.sh` (the Onion launcher with the changes below)
   - `Emu/NDS/libs/libdsfast.so`
3. Play. Saves, save states, layouts and settings are kept.

To go back, put Onion's original `Emu/NDS/launch.sh` back (or reinstall the Onion package) and delete
`Emu/NDS/libs/libdsfast.so`.

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
2. **Page flip without blocking** (3 framebuffer pages; the flip waits for the vsync in its own thread).
3. **Video thread woken as soon as a frame is ready** (instead of polling every 10 ms).
4. **Limiter follows the screen**: the panel refreshes at ~59.67 Hz, not 60.
5. **Sound stretched by the same 0.55%** so the sound chip never runs dry (no clicks).
6. **Threads placed on the two cores**: emulation, 2D and flip on core 0; 3D, audio and video on core 1.
7. **SDL video thread at a higher priority** than the 3D thread on core 1.
8. **Hidden screen not drawn** in single-screen layouts (and its 3D not rendered when nothing reads it back).
9. **DraStic menus without lag**.
11. **3D split over both cores** only while the emulation keeps waiting for the 3D (3D on both screens).
12. **Frameskip only for real peaks**: a frame is skipped only when the emulation is more than 6 ms late
    (DraStic: 3 ms). Spirit Tracks' opening at 1600 MHz: frames shown per second 51.7 -> 54.7, no slowdown.

The launcher (`launch.sh`) also:
- sets the CPU clock by model: 1800 MHz on the Mini Plus, 1600 MHz on the Mini v2/v3/v4 and Flip;
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
clang --target=armv7a-linux-gnueabihf -mfloat-abi=hard -O2 -fPIC -shared -nostdlib -fuse-ld=lld \
      -o libdsfast.so dsfast.c
```

## Credits

- DraStic by Exophase.
- Miyoo port of DraStic (SDL2 and ALSA layer) by [steward-fu](https://github.com/steward-fu/nds). This repository is
  a fork of it; the original README (all devices, building the SDL2 layer) is in
  [README_steward-fu.md](README_steward-fu.md).
- Onion integration by Sichroteph and schmurtzm ([OnionUI/Onion](https://github.com/OnionUI/Onion)).
- libdsfast by Régis ([cacuracaptors](https://github.com/cacuracaptors)), with Claude.

## License

GPL-3.0, like the rest of this repository.

## AI-Assisted

Claude Opus 5.5 was used to write most part of the code and to speed up debugging and optimization. Even though I'm no software engineer, I have a background in the modding community, so I understand how basic rendering tricks work. I don't write complex code myself (that's why I use Claude) but I understand how it's logic works and what's going on most of the time, so I can control what Claude is doing more tightly.
