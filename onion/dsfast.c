/* libdsfast: speed fixes for DraStic r2.5.0.4 with steward-fu's SDL2 layer (v1.8) on the Miyoo Mini,
 * loaded with LD_PRELOAD by the DraStic launcher. Two fixes, no measurement code:
 *
 * 1. Frame limiter (system_frame_sync, 0x08006850) rewritten in C with the original's behaviour, except
 *    for sleeping. The Miyoo's kernel timer ticks every 10 ms (no high resolution timers), so every
 *    sleep, even the SDL_Delay(0) DraStic uses to yield, lasts until a tick: ~5 ms on average, a third of
 *    a frame. Here a wait sleeps only the part that is safe (wait - 11 ms) and spins on the clock for the
 *    rest, and a zero wait does nothing. Measured: 48.8 -> 56.3 frames/s in Mario Kart DS with every frame
 *    drawn, and with threaded_3d the race stays at 60.
 *
 * 2. Page flip without blocking. The SDL layer's video thread composes the screen (~4 ms) and then waits
 *    in FBIOPAN_DISPLAY for the vsync (7-10 ms); frames DraStic finishes meanwhile are dropped. Here the
 *    framebuffer gets 3 pages and the flip is handed to our own thread, which waits for the vsync; the
 *    SDL layer goes on composing the next frame in the third page. At most one flip is pending, so the
 *    page being composed is never on screen. Measured: 34-44 -> 55-58 new frames on screen per second.
 *    On exit the screen goes back to page 0 and 2 pages, as the system expects.
 *
 * 3. Video thread woken on time. The SDL layer's video thread polls for a new frame with usleep(0), which
 *    here lasts until the next 10 ms tick: a finished frame waited 0-10 ms before being drawn, and the next
 *    frame was dropped if it came meanwhile. Here usleep(0) waits on a futex that the limiter signals as
 *    soon as DraStic finishes a frame. Drawing also moves out of the time DraStic is emulating, so the
 *    emulation itself got faster (10.1 instead of 11.3 ms per frame on average).
 *
 * 4. The limiter follows the screen. The panel refreshes at ~59.67 Hz, not 60: emulating at 60 left one
 *    frame every 3 s with nowhere to go. The flip thread measures the refresh period and the limiter uses
 *    it (3 x period, kept within 58-61.2 Hz), unless frame_interval is set in drastic.cfg.
 *    Measured with 3 and 4 together, recorded race: 95.9% -> 99.8-99.9% of screen refreshes show the next
 *    frame exactly (no frame lost or repeated), longest perfect stretch 3 s -> 24-37 s.
 *
 * 5. Sound stretched by the same 0.55% so that it keeps up with the sound chip (see below).
 *
 * 6. Threads placed on the two cores. Left to the scheduler, DraStic's 3D thread, the SDL layer's audio and
 *    video threads and the flip thread kept landing on the emulation's core and pushing it aside. Here the
 *    emulation, DraStic's 2D render thread (it hands the frame over and waits, so it must share the core)
 *    and the flip thread stay on core 0; the 3D thread(s) and every other thread go to core 1.
 *    Measured at 1600 MHz, recorded race: 12.34 -> 10.75 ms per frame on average, frames over 16.7 ms
 *    5.3% -> 0.2%, worst 26 -> 21.7 ms, exact frames 97.93% -> 99.83%, longest perfect stretch 8 s -> 91 s.
 *
 * 7. The SDL layer's video thread runs at nice -10, so that on core 1 it goes ahead of the 3D thread as soon
 *    as a frame is ready. Measured with runs taken back to back: exact frames Mario Kart (1600 MHz)
 *    99.20 -> 99.44%, Zelda Phantom Hourglass 99.41 -> 99.76%, GTA Chinatown Wars 98.3-98.6 -> 98.93%.
 *
 * 8. The hidden screen is not drawn. In the SDL layer's single-screen layouts (S0/S1 show only DraStic's
 *    screen 1) the other screen was drawn line by line for nothing. (Beta 4: never in the high-resolution 3D
 *    mode, where skipping a screen drew garbage on both.)
 *    render_scanline, the only per-line drawing call, now skips the 2D engine whose output is the hidden
 *    screen (compared by pointer, so it works whichever screen the player put where), unless that engine is
 *    doing a display capture, which the game reads back. Register events and the rest of the line loop
 *    still run, so nothing is off when the layout changes back. Only with the libSDL2 v1.8 build.
 *    Measured, Mario Kart DS at 1600 MHz in layout S0, runs back to back: 11.39 -> 10.82-10.94 ms per frame.
 *    When the hidden screen is engine A's (the 3D one) for 8 frames in a row without capture, the 3D of the
 *    frame is not rendered either (games with 3D on both screens alternate every frame and never qualify;
 *    checked with Zelda Spirit Tracks' opening). Mario Kart DS showing only the map: 10.23 -> 8.41 ms.
 *
 * 9. Menus without lag. DraStic's menus (MENU+SELECT, MENU+START) draw without going through the limiter, so
 *    the video thread's wait from 3 timed out every time (20 ms) and the menus answered late. When no frame
 *    has been emulated for 100 ms, usleep(0) only yields. Checked by Regis: menus answer at once.
 *
 * 11. 3D split over both cores only when the emulation keeps waiting for the 3D (see section 11 below).
 *
 * 12. Frameskip only for real peaks. With frameskip on, DraStic skips drawing the next frame as soon as the
 *     emulation is 3 ms late, which on this device happens on frames that would still make the vsync. Here
 *     the threshold is 6 ms. Zelda Spirit Tracks' opening (frames 300-5600, frameskip auto, value 1):
 *     1800 MHz: skipped frames 223-283 -> ~110, shown 57 -> 58.5/s, exact flips 97.3 -> 98.8%;
 *     1600 MHz: skipped 749 -> 470, shown 51.7 -> 54.7/s, exact flips 92.0 -> 94.3%. At 7 ms and above
 *     repeated frames (slowdown) appear at 1600 MHz, so 6 ms is the limit.
 *
 *
 * 13. A clock that never jumps back (see section 1): DraStic's own clock jumped back 4295 s every ~71.6 minutes
 *     of play and whenever the system time changed, and the limiter then waited for the whole jump (the game
 *     froze). Reproduced by moving the system clock back 20 s four times during Mario Kart DS: frozen 20 s every
 *     time before, 61-62 frames/s throughout after.
 *
 * 2b. (Beta 3) The page flip (2) and the panel measurement (4) also work with the 752x560 screen of the Mini v4
 *     and Flip; before, both were left out there (only 480 lines were accepted) and the limiter stayed at 60 Hz.
 *     Measured on a Mini Plus with the screen set to 752x560, Mario Kart DS, 70 s: frames lost on the way to the
 *     screen 103 of 4116 (old flip) -> 1 of 4113. With a plain flip the panel is now measured too.
 *
 * 14. (Beta 3) The SDL layer's Blur/Pixel choice (MENU+B) is remembered between sessions, and dsfast.log (next to
 *     launch.sh) gets a short summary on exit (screen mode, flip mode, panel refresh, frames per second).
 *
 * 15. (Beta 4) Settings of the SDL menu (MENU+START) that it never saved are kept: the CPU clock and the screen the
 *     stylus is on (config/.dsfast_cpuclock and .dsfast_touchpad, applied by launch.sh).
 *
 * 16. (Beta 4) The copy of the screens for the SDL video thread (sdl_update_screen: cache flush, 2D accelerator
 *     copy and its wait) runs on core 1 instead of the emulation thread (see section 16). Recorded Mario Kart DS
 *     race, 640x480, 1800 MHz: frames over 33 ms 10 -> 2-3, p99.9 29 -> 18-19 ms, frames skipped 14 -> 3-6,
 *     frames 2 or more refreshes on screen 20 -> 6-9 (average emulation time per frame 8.4 -> 9.0-9.2 ms).
 *
 * 17. DSFAST_PROF=1: per-frame timing of the 2D, the 3D and the waits in dsfast.log, for studies.
 *
 * All act only inside the drastic executable (programs it starts inherit LD_PRELOAD). */
typedef unsigned char u8;
typedef unsigned int u32;
typedef int s32;
typedef long long s64;
typedef unsigned long long u64;

extern long readlink(const char *path, char *buf, unsigned long len);
extern int mprotect(void *addr, unsigned long len, int prot);
extern void *dlsym(void *handle, const char *name);
typedef struct { int __size[6]; } pmutex_t;   /* glibc ARM pthread_mutex_t */
typedef struct { int __size[12]; } pcond_t;   /* glibc ARM pthread_cond_t */
extern int pthread_mutex_lock(pmutex_t *m);
extern int pthread_mutex_unlock(pmutex_t *m);
extern int pthread_cond_wait(pcond_t *c, pmutex_t *m);
extern int pthread_cond_broadcast(pcond_t *c);
extern int pthread_create(unsigned long *t, void *attr, void *(*fn)(void *), void *arg);
#define RTLD_NEXT ((void *)-1l)

static int active;
struct timespec { long tv_sec; long tv_nsec; };
extern int clock_gettime(int clk, struct timespec *ts);
static s64 mono_us(void) { struct timespec ts; clock_gettime(1, &ts); return (s64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000; }

/* ------------------------------------------------------------------ 3. video thread wake-up */

static volatile int frame_seq;
static long futex(volatile int *addr, int op, int val, struct timespec *ts)
{
    register long r0 __asm__("r0") = (long)addr;
    register long r1 __asm__("r1") = op;
    register long r2 __asm__("r2") = val;
    register long r3 __asm__("r3") = (long)ts;
    register long r7 __asm__("r7") = 240; /* futex */
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r7) : "memory");
    return r0;
}
static volatile s64 last_frame_us;
static u32 frames_n;
static void frame_ready(void)
{
    frames_n++;
    last_frame_us = mono_us();
    __sync_fetch_and_add(&frame_seq, 1);
    futex(&frame_seq, 1 | 128, 0x7fffffff, 0);   /* FUTEX_WAKE_PRIVATE */
}
static int (*real_usleep)(unsigned int);
static long sys3(long nr, long a, long b, long c);
int usleep(unsigned int us)
{
    static int seen;
    if (!real_usleep)
        real_usleep = (int (*)(unsigned int))dlsym(RTLD_NEXT, "usleep");
    if (us == 0 && active) {      /* only the SDL layer's video thread sleeps 0 */
        int s = frame_seq;
        static int raised;
        if (!raised) {
            /* 7. It shares core 1 with DraStic's 3D thread: let it go first when a frame is ready (nice -10). */
            raised = 1;
            sys3(97, 0, 0, -10);   /* setpriority(PRIO_PROCESS, this thread, -10) */
        }
        if ((mono_us() - last_frame_us > 100000)) {
            /* no emulated frame for 100 ms: a menu is open and draws without system_frame_sync, so nobody
               would wake us; yield instead of sleeping (a sleep here lasts up to the next 10 ms tick) */
            sys3(158, 0, 0, 0);   /* sched_yield */
            seen = frame_seq;
            return 0;
        }
        if (s == seen) {
            struct timespec ts = { 0, 20000000 };
            futex(&frame_seq, 0 | 128, s, &ts);    /* FUTEX_WAIT_PRIVATE */
        }
        seen = frame_seq;
        return 0;
    }
    return real_usleep(us);
}

/* refresh period measured by the flip thread, in 1/256 us; 0 until known */
static volatile u32 vsync_q8;

/* ------------------------------------------------------------------ 5. sound at the rate it is played
   DraStic's sound (44100 Hz stereo s16) goes through snd_pcm_writei of the SDL layer's own ALSA replacement.
   With the limiter at the screen's rate (59.67 Hz) the emulation makes 0.55% less sound than the sound chip
   plays, so the chip ran dry from time to time (a click). Here the sound is stretched (linear interpolation)
   so that 44100 frames per real second come out: the factor follows how far the written sound is ahead of
   real time, within +-1%. Measured: factor settles at 1.0053, level steady, no frame pacing cost. */
typedef long snd_sframes;

static snd_sframes (*real_writei)(void *pcm, const void *buf, unsigned long n);
static short a_out[2 * 8192];
static short a_last[2];
static u32 a_pos;          /* position between a_last and the next input frame, 1/65536 */
static s64 a_t0, a_tlast, a_written;
snd_sframes snd_pcm_writei(void *pcm, const void *buffer, unsigned long size)
{
    const short *in = (const short *)buffer;
    s64 now, level;
    u32 step, n = 0;
    s32 ratio_q16;
    if (!real_writei)
        real_writei = (snd_sframes (*)(void *, const void *, unsigned long))dlsym(RTLD_NEXT, "snd_pcm_writei");
    if (!active || size < 2 || size > 4096)
        return real_writei(pcm, buffer, size);
    now = mono_us();
    if (!a_t0 || now - a_tlast > 80000) {    /* start, or sound was stopped (menu, loading, savestate) */
        a_t0 = now;
        a_written = 0;
        a_last[0] = in[0];
        a_last[1] = in[1];
        a_pos = 0;
    }
    a_tlast = now;
    /* frames written ahead of what the chip has played since a_t0 */
    {
        u32 el = (u32)(now - a_t0);
        level = a_written - (s64)(el / 10000 * 441 + el % 10000 * 441 / 10000);
    }
    if (level > 44100 || level < -44100) {   /* far off (fast forward...): start over */
        a_t0 = now;
        a_written = 0;
        level = 0;
    }
    /* output frames per input frame: 1 + correction for the level, at most +-1% */
    ratio_q16 = 65536 - (s32)level * 743 / 1000;   /* corrects the level over ~2 s */
    if (ratio_q16 > 66191)
        ratio_q16 = 66191;
    if (ratio_q16 < 64881)
        ratio_q16 = 64881;
    step = 0xffffffffu / (u32)ratio_q16;   /* input advance per output frame, 1/65536 */
    /* linear interpolation between a_last (input index -1) and in[0..size-1] */
    for (;;) {
        u32 idx = a_pos >> 16, f = a_pos & 0xffff;
        s32 l0, r0, l1, r1;
        if (idx >= size)
            break;
        if (idx == 0) {
            l0 = a_last[0]; r0 = a_last[1];
        } else {
            l0 = in[2 * (idx - 1)]; r0 = in[2 * (idx - 1) + 1];
        }
        l1 = in[2 * idx]; r1 = in[2 * idx + 1];
        if (n >= 8192)
            break;
        a_out[2 * n] = (short)(l0 + (((l1 - l0) * (s32)f) >> 16));
        a_out[2 * n + 1] = (short)(r0 + (((r1 - r0) * (s32)f) >> 16));
        n++;
        a_pos += step;
    }
    a_pos -= size << 16;
    a_last[0] = in[2 * (size - 1)];
    a_last[1] = in[2 * (size - 1) + 1];
    a_written += n;
    if (n)
        real_writei(pcm, a_out, n);
    return (snd_sframes)size;
}

/* ------------------------------------------------------------------ 1. frame limiter */

#define FN_SYSTEM_FRAME_SYNC   0x08006850u
#define FN_GET_TICKS_US        0x080a7d50u
static void (*get_ticks_us)(s64 *t) = (void (*)(s64 *))FN_GET_TICKS_US;
static void (*delay_us)(u32 us) = (void (*)(u32))0x080a7dc0u;
static u32 (*audio_buffer_state)(void *a) = (u32 (*)(void *))0x080aa718u;
static void (*audio_buffer_force_feed)(void *a) = (void (*)(void *))0x080aa760u;

/* DraStic's get_ticks_us() reads the wall clock (gettimeofday), computes (s32)tv_sec * 1000000 and
   sign-extends bit 31, jumping -4295 s every ~71.6 minutes (2^32 us), and it also jumps whenever the
   system time is changed. A jump back during the limiter's wait froze the game for the size of the jump.
   Replace it with 64-bit CLOCK_MONOTONIC via mono_us(). Reported and fixed by Kmieciu4ever (PR #1). */
static void dsfast_get_ticks_us(s64 *t) { *t = mono_us(); }

#define CFG(s, off)    (*(u32 *)((char *)(s) + 0x85000 + (off)))
#define SKIP_TYPE      0x768
#define SKIP_VALUE     0x76c
#define FAST_FORWARD   0x784
#define FRAME_INTERVAL 0x7b0
#define F(s, off)      (*(u8 *)((char *)(s) + 0x2aea000 + (off)))
#define AUDIO_MODE     0xe6a  /* paced by the sound buffer instead of the timer */
#define AUDIO_COUNT    0xe6b
#define SKIP_NEXT      0xe6c  /* the next frame is not drawn */
#define TOGGLE         0xe6d
#define COUNT2         0xe6e
#define SKIPPED        0xe6f  /* frames skipped in a row */

/* times in 1/3 microsecond, as DraStic keeps them */
static void wait_us(u32 us)
{
    s64 t0, t;
    if (!us)
        return;
    t0 = mono_us();
    if (us > 11000)
        delay_us(us - 11000);
    do
        t = mono_us();
    while (t - t0 < (s64)us);
}

static void frame_sync(void *sys)
{
    s64 *target = (s64 *)((char *)sys + 0x2aeae50);
    void *audio = (char *)sys + 0x1566000;
    u32 skip_type, skip_value, interval, astate;
    s32 delta;
    int late;
    s64 t, now, tgt;

    interval = CFG(sys, FRAME_INTERVAL);
    astate = audio_buffer_state(audio);
    skip_type = CFG(sys, SKIP_TYPE);
    skip_value = CFG(sys, SKIP_VALUE);
    get_ticks_us(&t);
    now = t * 3 - target[-1];
    if (!interval) {
        u32 q = vsync_q8;
        interval = 50000;
        if (q) {
            interval = (q * 3 + 128) >> 8;
            if (interval < 49000)
                interval = 49000;
            if (interval > 51700)
                interval = 51700;
        }
    }
    *target += interval;
    tgt = *target;
    if (CFG(sys, FAST_FORWARD)) {
        *target = now;
        tgt = now;
        if (*(u32 *)((char *)sys + 0x898e0) == 0) {
            skip_type = 1;
            skip_value = 6;
        }
    }

    if (F(sys, AUDIO_MODE)) {
        if (F(sys, AUDIO_COUNT) == 20) {
            audio_buffer_force_feed(audio);
            tgt = *target;
            goto dry;
        }
        if (astate == 0)
            goto dry;
        F(sys, AUDIO_COUNT)++;
        goto audio_paced;
    }
    if (astate == 2) {
        F(sys, AUDIO_COUNT) = 0;
        F(sys, AUDIO_MODE) = 1;
        goto audio_paced;
    }
    goto timer;

dry: /* the sound buffer ran dry: back to the timer, realigned with now */
    F(sys, AUDIO_MODE) = 0;
    *target -= (u32)((interval - 1 + (u32)tgt - (u32)now) / interval * interval);
    tgt = *target;

timer:
    delta = (s32)((u32)tgt - (u32)now);
    late = 0;
    if (delta > 300000) {
        /* Guard against >100 ms target skew (e.g. long pause or clock jump): resync instead of spinning */
        *target = now;
        delta = 0;
    }
    if (delta > 0)
        wait_us((u32)delta / 3);
    else if (delta < -18000)   /* 6 ms late (DraStic: 3 ms), see section 12 */
        late = 1;
    goto frameskip;

audio_paced:
    delta = (s32)((u32)tgt - (u32)now);
    late = 1;

frameskip:
    if (skip_type == 0) {
        F(sys, SKIP_NEXT) = 0;
    } else if (skip_type == 1) {
        u8 s = F(sys, SKIPPED);
        F(sys, SKIP_NEXT) = s != 0;
        s++;
        F(sys, SKIPPED) = s;
        if (s == skip_value + 1)
            F(sys, SKIPPED) = 0;
    } else if (skip_type == 2) {
        u8 tog = F(sys, TOGGLE) ^ 1;
        F(sys, TOGGLE) = tog;
        if (*(s32 *)((char *)sys + 0x25ce7c4) < 0) {
            u8 c;
            if (tog)
                return;
            c = F(sys, COUNT2);
            F(sys, COUNT2) = 0;
            if (c > 120) {
                F(sys, SKIP_NEXT) = 0;
                return;
            }
        } else if (F(sys, COUNT2) != 255) {
            F(sys, COUNT2)++;
        }
        F(sys, SKIP_NEXT) = 0;
        if (!late) {
            F(sys, SKIPPED) = 0;
            return;
        }
        {
            u8 s = F(sys, SKIPPED) + 1;
            F(sys, SKIPPED) = s;
            if (s != skip_value + 1) {
                F(sys, SKIP_NEXT) = 1;
                return;
            }
        }
        /* skipped the most frames allowed in a row: draw this one and realign */
        F(sys, AUDIO_MODE) = 0;
        F(sys, SKIPPED) = 0;
        *target -= (u32)(((u32)(interval - 1 + (u32)delta)) / interval * interval);
    }
}

static void *g_sys;
/* diagnostics for dsfast.log (section 14): emulation time per frame, frames not drawn */
static s64 sync_end_us;
static u32 work_n, work_sum10, work_max, work_hist[5], undrawn_n, work_ms[64];
static void copy_setup_late(void);    /* section 16 */
static int copy_mode;
static u32 us_async, us_sync, us_main_sum10, us_main_max, copy_waits, copy_wait_us;
static int prof_on;                   /* section 17 */
static u32 pf_2d, pf_wait3d, pf_lines;
static u32 pd[2][6], pd_hist[2][64];
static void prof_frame(u32 w, int counted, int drawn_next);
static void count_work(u32 w)
{
    work_n++;
    work_ms[w / 1000 < 63 ? w / 1000 : 63]++;
    work_sum10 += w / 10;
    if (w > work_max)
        work_max = w;
    work_hist[w < 12000 ? 0 : w < 16700 ? 1 : w < 20000 ? 2 : w < 33300 ? 3 : 4]++;
}

void dsfast_system_frame_sync(void *sys)
{
    s64 t = mono_us();
    g_sys = sys;
    int counted = sync_end_us && t - sync_end_us < 200000;   /* not after a menu or a load */
    copy_setup_late();
    if (counted)
        count_work((u32)(t - sync_end_us));
    frame_ready();
    frame_sync(sys);
    if (F(sys, SKIP_NEXT))
        undrawn_n++;
    if (prof_on)
        prof_frame((u32)(t - sync_end_us), counted, !F(sys, SKIP_NEXT));
    sync_end_us = mono_us();
}

/* ------------------------------------------------------------------ 2. page flip */

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOPAN_DISPLAY     0x4606
static u32 page_h;                  /* screen height: 480 (Mini Plus, v2/v3) or 560 (Mini v4, Flip) */

extern char *getenv(const char *name);
struct fbvar { u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset; u32 rest[34]; };

static int (*real_ioctl)(int, unsigned long, void *);
static int flip_state;            /* 0 not set up, 1 on, -1 off (plain flips) */
static pmutex_t fmtx;
static pcond_t fcond;
static int pending = -1, ffd;
static u32 orig_yres_virtual;
static struct fbvar fvar;
static u32 flip_xres, flip_n, flip_waits, vsync_n, flip_wait_us, flip_hist[4];
static long start_sec;

static void measure_vsync(void)
{
    static s64 last;
    static u32 n, sum;
    s64 t = mono_us();
    u32 d = (u32)(t - last);
    last = t;
    if (d < 200000)                      /* flip intervals for dsfast.log, not across menus */
        flip_hist[d < 14000 ? 0 : d < 19500 ? 1 : d < 36000 ? 2 : 3]++;
    if (d < 14000 || d > 19500)          /* not one refresh apart */
        return;
    vsync_n++;
    if (n < 120) {                       /* first estimate: plain average */
        sum += d;
        if (++n == 120)
            vsync_q8 = (sum << 8) / 120;
        return;
    }
    vsync_q8 += (s32)((d << 8) - vsync_q8) / 64;
}

static void *flip_thread(void *p)
{
    (void)p;
    for (;;) {
        pthread_mutex_lock(&fmtx);
        while (pending < 0)
            pthread_cond_wait(&fcond, &fmtx);
        fvar.yoffset = (u32)pending;
        pthread_mutex_unlock(&fmtx);
        real_ioctl(ffd, FBIOPAN_DISPLAY, &fvar); /* returns at the vsync */
        measure_vsync();
        pthread_mutex_lock(&fmtx);
        pending = -1;
        pthread_cond_broadcast(&fcond);
        pthread_mutex_unlock(&fmtx);
    }
    return 0;
}

static void flip_setup(int fd)
{
    struct fbvar cur;
    unsigned long th;
    flip_state = -1;
    if (real_ioctl(fd, FBIOGET_VSCREENINFO, &cur))
        return;
    page_h = cur.yres;
    flip_xres = cur.xres;
    /* 640x480 (Mini Plus, v2/v3) and 752x560 (Mini v4, Flip, set by the launcher) */
    if (cur.yres != 480 && cur.yres != 560)
        return;
    if (getenv("DSFAST_PLAINFLIP"))   /* for comparisons: keep the SDL layer's own flip */
        return;
    orig_yres_virtual = cur.yres_virtual;
    if (cur.yres_virtual < 3 * page_h) {
        cur.yres_virtual = 3 * page_h;
        if (real_ioctl(fd, FBIOPUT_VSCREENINFO, &cur))
            return;
        real_ioctl(fd, FBIOGET_VSCREENINFO, &cur);
        if (cur.yres_virtual < 3 * page_h)
            return;
    }
    fvar = cur;
    ffd = fd;
    if (pthread_create(&th, 0, flip_thread, 0))
        return;
    flip_state = 1;
}

int ioctl(int fd, unsigned long req, void *arg)
{
    struct fbvar *v = (struct fbvar *)arg;
    u32 page;
    if (!real_ioctl)
        real_ioctl = (int (*)(int, unsigned long, void *))dlsym(RTLD_NEXT, "ioctl");
    if (req != FBIOPAN_DISPLAY || !active)
        return real_ioctl(fd, req, arg);
    if (!flip_state)
        flip_setup(fd);
    flip_n++;
    if (flip_state != 1) {
        /* plain flip (returns at the vsync): still measure the panel, so the limiter follows it (4) */
        int r = real_ioctl(fd, req, arg);
        measure_vsync();
        return r;
    }
    page = v->yoffset / page_h;
    pthread_mutex_lock(&fmtx);
    if (pending >= 0) {                /* the previous flip has not reached the screen yet */
        s64 t0 = mono_us();
        while (pending >= 0)
            pthread_cond_wait(&fcond, &fmtx);
        flip_waits++;
        flip_wait_us += (u32)(mono_us() - t0);
    }
    pending = (int)(page * page_h);
    pthread_cond_broadcast(&fcond);
    pthread_mutex_unlock(&fmtx);
    /* the SDL layer does yoffset ^= height after this call: make that land on the next of the 3 pages */
    v->yoffset = (((page + 1) % 3) * page_h) ^ page_h;
    return 0;
}

/* ------------------------------------------------------------------ 14. Blur remembered, and a short log
   The SDL layer's Blur/Pixel choice (MENU+B, its variable down_scale: 0 = Blur) lives only in memory and starts
   as Pixel every time. It is kept in config/.dsfast_blur (exists = Blur) and set again at start.
   On exit, dsfast.log (next to launch.sh) gets one block: screen mode, flip mode, measured panel refresh,
   frames per second, so that players on other models can send it with a report. */
/* libc's stdio, under other names (no headers here) */
extern void *log_fopen(const char *path, const char *mode) __asm__("fopen");
extern int log_printf(void *f, const char *fmt, ...) __asm__("fprintf");
extern int log_fclose(void *f) __asm__("fclose");
extern int unlink(const char *path);
extern int access(const char *path, int mode);
#define BLUR_FILE "config/.dsfast_blur"
static int *sdl_down_scale;

static void blur_setup(void)
{
    u8 *init = (u8 *)dlsym((void *)0, "SDL_Init");
    /* only with the libSDL2 v1.8 build (same check as in hide_setup) */
    if (!init || *(u32 *)(init - 0x35470 + 0xe6178) != 0xe92d4ff0u)
        return;
    sdl_down_scale = (int *)(init - 0x35470 + 0x122f38);
    if (*sdl_down_scale != 0 && *sdl_down_scale != 1) {   /* not the variable we expect */
        sdl_down_scale = 0;
        return;
    }
    if (access(BLUR_FILE, 0) == 0)
        *sdl_down_scale = 0;
}

/* the CPU clock as the chip runs it (OnionOS's cpuclock programs the PLL directly; cpufreq does not see it) */
extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);
extern int munmap(void *addr, unsigned long len);
extern int log_scanf(void *f, const char *fmt, ...) __asm__("fscanf");
static u32 cpu_mhz(void)
{
    int fd = open("/dev/mem", 2 | 0x101000);   /* O_RDWR | O_SYNC */
    volatile u32 *r;
    u32 lpf, pd, mhz = 0;
    if (fd < 0)
        return 0;
    r = (volatile u32 *)mmap(0, 4096, 1, 1, fd, 0x1f206000);   /* PROT_READ, MAP_SHARED: the CPU PLL */
    close(fd);
    if (r == (volatile u32 *)-1)
        return 0;
    lpf = (r[0x548 / 4] & 0xffff) | (r[0x54c / 4] & 0xffff) << 16;
    pd = (r[0x464 / 4] & 0xffff) + 1;
    if (lpf >> 6)
        mhz = 226492416u / (lpf >> 6) / 2 / pd;   /* 432 MHz * 2^19 / lpf * 2 * 16 / pd */
    munmap((void *)r, 4096);
    return mhz;
}
static u32 mhz_start;

/* Settings of the SDL layer's menu that it does not save itself (section 15): the CPU clock chosen in its menu
   (set in the chip, read back here) and the screen the stylus is on (nds.pen.pos, MENU+UP/DOWN in stylus mode;
   read from settings.json at start, never written back). launch.sh applies the two files at the next start. */
#define CPU_FILE "config/.dsfast_cpuclock"
#define TOUCH_FILE "config/.dsfast_touchpad"
static int *sdl_nds;             /* libSDL2 v1.8 'nds' struct (section 8) */
static void settings_save(void)
{
    u32 now = cpu_mhz();
    if (mhz_start && now >= 400 && now <= 2400 && (now > mhz_start + 25 || now + 25 < mhz_start)) {
        void *f = log_fopen(CPU_FILE, "w");
        if (f) {
            log_printf(f, "%u\n", (now + 25) / 50 * 50);   /* the menu moves in steps of 50 MHz */
            log_fclose(f);
        }
    }
    /* nds.pen.xv at +0x6f4 holds the X speed (between 10000 and 300000 in the menu): sanity check of the offsets */
    if (sdl_nds && sdl_nds[0x6f4 / 4] >= 1000 && sdl_nds[0x6f4 / 4] <= 1000000 &&
        (sdl_nds[0x704 / 4] == 0 || sdl_nds[0x704 / 4] == 1)) {
        void *f = log_fopen(TOUCH_FILE, "w");
        if (f) {
            log_printf(f, "%d\n", sdl_nds[0x704 / 4]);
            log_fclose(f);
        }
    }
}

static void write_log(void)
{
    void *f = log_fopen("dsfast.log", "w");
    struct timespec ts;
    u32 q = vsync_q8, hz100 = q >= 10 ? 2560000000u / (q / 10) : 0, secs, fps10;
    if (!f)
        return;
    clock_gettime(1, &ts);
    secs = (u32)(ts.tv_sec - start_sec);
    fps10 = secs ? frames_n * 10 / secs : 0;
    log_printf(f, "libdsfast beta4\n");
    log_printf(f, "screen: %ux%u, pages: %s\n", flip_xres, page_h, flip_state == 1 ? "3 (flip without blocking)" :
            flip_state == -1 ? "2 (plain flip)" : "not set up");
    log_printf(f, "panel refresh: %u.%02u Hz (%u vsyncs measured)\n", hz100 / 100, hz100 % 100, vsync_n);
    log_printf(f, "time: %u s, frames emulated: %u (%u.%u per s), screen flips: %u\n", secs, frames_n,
            fps10 / 10, fps10 % 10, flip_n);
    log_printf(f, "flips that waited for the previous one: %u (%u ms in all)\n", flip_waits, flip_wait_us / 1000);
    log_printf(f, "blur: %s\n", !sdl_down_scale ? "unknown" : *sdl_down_scale ? "off (Pixel)" : "on");
    {
        int model = 0;
        void *m = log_fopen("/tmp/deviceModel", "r");
        if (m) {
            log_scanf(m, "%d", &model);
            log_fclose(m);
        }
        log_printf(f, "device model: %d, CPU clock: %u MHz at start, %u MHz at exit\n", model, mhz_start, cpu_mhz());
    }
    if (work_n) {
        u32 avg100 = work_sum10 / work_n * 10 / 100;   /* in 0.1 ms */
        log_printf(f, "emulation per frame: average %u.%u ms, worst %u.%u ms\n", avg100 / 10, avg100 % 10,
                   work_max / 1000, work_max / 100 % 10);
        log_printf(f, "frames by emulation time: <12 ms %u, 12-16.7 ms %u, 16.7-20 ms %u, 20-33 ms %u, >33 ms %u\n",
                   work_hist[0], work_hist[1], work_hist[2], work_hist[3], work_hist[4]);
    }
    if (work_n) {
        /* percentiles from the 1 ms histogram: the frame time that p% of the frames stay under */
        static const u32 pc[4] = { 500, 900, 990, 999 };   /* per mille */
        u32 v[4], i, k, acc;
        for (i = 0; i < 4; i++) {
            u32 need = work_n / 1000 * pc[i] + work_n % 1000 * pc[i] / 1000;
            for (k = 0, acc = 0; k < 63 && acc + work_ms[k] < need; k++)
                acc += work_ms[k];
            v[i] = k + 1;
        }
        log_printf(f, "emulation per frame, percentiles (ms, upper bound): p50 %u, p90 %u, p99 %u, p99.9 %u\n",
                   v[0], v[1], v[2], v[3]);
    }
    log_printf(f, "screen copy (sdl_update_screen): %s, on core 1 %u, on the emulation thread %u\n",
               copy_mode == 1 ? "on core 1" : copy_mode == 2 ? "kept on the emulation thread" : "not hooked",
               us_async, us_sync);
    if (us_async + us_sync)
        log_printf(f, "  time on the emulation thread: average %u us, worst %u us; waits for the copy: %u (%u ms)\n",
                   us_main_sum10 / (us_async + us_sync) * 10, us_main_max, copy_waits, copy_wait_us / 1000);
    log_printf(f, "frames not drawn (frameskip): %u\n", undrawn_n);
    if (prof_on) {
        int k;
        for (k = 1; k >= 0; k--) {
            u32 *p = pd[k], n = p[0] ? p[0] : 1, i, acc, v[3];
            static const u32 pc[3] = { 500, 900, 990 };
            for (i = 0; i < 3; i++) {
                u32 need = p[0] / 1000 * pc[i] + p[0] % 1000 * pc[i] / 1000, j;
                for (j = 0, acc = 0; j < 63 && acc + pd_hist[k][j] < need; j++)
                    acc += pd_hist[k][j];
                v[i] = j + 1;
            }
            log_printf(f, "PROF frames %s: %u, per frame (ms x10): emulation %u (p50 %u p90 %u p99 %u ms), 2D lines %u "
                       "(%u lines), waits for 3D %u, 3D work all threads %u\n", k ? "drawn" : "skipped", p[0],
                       p[1] / n * 10 / 100, v[0], v[1], v[2], p[2] / n * 10 / 100, p[5] / n, p[3] / n * 10 / 100,
                       p[4] / n * 10 / 100);
        }
    }
    log_printf(f, "time between screen flips: <14 ms %u, 1 refresh %u, 2 refreshes %u, more %u\n",
               flip_hist[0], flip_hist[1], flip_hist[2], flip_hist[3]);
    log_fclose(f);
}

__attribute__((destructor)) static void dsfast_end(void)
{
    if (!active)
        return;
    if (sdl_down_scale) {
        if (*sdl_down_scale == 0) {
            void *f = log_fopen(BLUR_FILE, "w");
            if (f)
                log_fclose(f);
        } else
            unlink(BLUR_FILE);
    }
    settings_save();
    write_log();
    if (flip_state != 1)
        return;
    pthread_mutex_lock(&fmtx);
    while (pending >= 0)
        pthread_cond_wait(&fcond, &fmtx);
    pending = 0x7fffffff;              /* keeps the flip thread from flipping again */
    pthread_mutex_unlock(&fmtx);
    fvar.yoffset = 0;
    real_ioctl(ffd, FBIOPAN_DISPLAY, &fvar);
    if (orig_yres_virtual && orig_yres_virtual < fvar.yres_virtual) {
        fvar.yres_virtual = orig_yres_virtual;
        real_ioctl(ffd, FBIOPUT_VSCREENINFO, &fvar);
    }
}

/* ------------------------------------------------------------------ setup */

static void flush_icache(void *start, void *end)
{
    register long r0 __asm__("r0") = (long)start;
    register long r1 __asm__("r1") = (long)end;
    register long r2 __asm__("r2") = 0;
    register long r7 __asm__("r7") = 0xf0002; /* ARM cacheflush */
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
}

/* overwrite the start of a DraStic function with "ldr pc, [pc, #-4]; .word new_function" */
static void patch(u32 at, void *to)
{
    u32 *p = (u32 *)at;
    mprotect((void *)(at & ~4095u), 8192, 7);
    p[0] = 0xe51ff004;
    p[1] = (u32)to;
    flush_icache(p, p + 2);
}

/* ------------------------------------------------------------------ 6. thread placement */

static long sys3(long nr, long a, long b, long c)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
static void set_core(unsigned long mask) { sys3(241, 0, sizeof(mask), (long)&mask); } /* sched_setaffinity */
#define CORE0 1ul
#define CORE1 2ul
#define FN_VIDEO_RENDER_THREAD 0x080287e0u     /* 2D, hands over to the emulation and waits */
#define FN_VIDEO_3D_RENDER_THREAD 0x080782dcu  /* 3D helper threads (initialize_video_3d always makes 3) */
static int helpers_seen;
static int use_nice = 1;
struct tramp { void *(*fn)(void *); void *arg; unsigned long mask; int nice; };
extern void *malloc(unsigned int n);
extern void free(void *p);
static void *tramp_run(void *p)
{
    struct tramp t = *(struct tramp *)p;
    free(p);
    set_core(t.mask);
    /* Main sets -5 before spawning threads, which clone() inherits; reset each child
     * explicitly so flip (-8) > Main (-5) == Engine B (-5) > w0 (+5), and others stay at 0. */
    if (use_nice)
        sys3(97, 0, 0, t.nice);
    return t.fn(t.arg);
}
int pthread_create(unsigned long *th, void *attr, void *(*fn)(void *), void *arg)
{
    static int (*real)(unsigned long *, void *, void *(*)(void *), void *);
    u32 a = (u32)fn;
    unsigned long mask = CORE1;   /* 3D (video_3d_run_thread, video_3d_render_thread), SDL audio and video... */
    int nice = 0;
    struct tramp *t;
    if (!real)
        real = (int (*)(unsigned long *, void *, void *(*)(void *), void *))dlsym(RTLD_NEXT, "pthread_create");
    if (a == FN_VIDEO_RENDER_THREAD) {
        mask = CORE0;
        nice = -5;                /* match Main (-5) so Engine B 2D never stalls behind Main */
    }
    if (fn == flip_thread) {
        mask = CORE0;
        nice = -8;                /* never miss a vsync pan behind Main/2D */
    }
    if (a == FN_VIDEO_3D_RENDER_THREAD && helpers_seen++ == 0) {
        mask = CORE0;             /* 11. the first 3D helper (bins 1, 4, 7...) runs next to the emulation */
        nice = 5;                 /* idle-fill on core 0: never preempt flip (-8), Main (-5), or Engine B (-5) */
    }
    if (!active || !(t = (struct tramp *)malloc(sizeof(*t))))
        return real(th, attr, fn, arg);
    t->fn = fn;
    t->arg = arg;
    t->mask = mask;
    t->nice = nice;
    return real(th, attr, tramp_run, t);
}

/* ------------------------------------------------------------------ 16. screen copy off the emulation thread
   The SDL layer's sdl_update_screen (DraStic's update_screen, detoured to it) runs on the emulation thread at
   the end of every frame: it flushes the CPU cache of DraStic's two screens, has the 2D accelerator copy them
   to the buffer its video thread reads, and waits for the copy. Here a thread on core 1 makes that call; the
   emulation goes on at once and waits for the copy only if it starts drawing the next frame (render_scanline)
   or ends it before the copy is done. Not in high resolution (512x384) nor while a menu is open (no frame for
   100 ms): there the call stays where it was. DSFAST_SYNCCOPY=1 keeps it there always (for comparisons). */
#define SDL_UPDATE_SCREEN 0xe7490u          /* sdl_update_screen in libSDL2 v1.8, from SDL_Init at 0x35470 */
#define FN_UPDATE_SCREEN 0x080a83c0u        /* DraStic's update_screen: ldr pc, [pc, #-4] + address (detour) */
static u8 *sdl_base;
static void (*orig_update_screen)(void);
static volatile int copy_busy;
static int copy_mode;                        /* 0 not set up, 1 on core 1, 2 kept on the emulation thread */
static u32 us_async, us_sync, us_main_sum10, us_main_max, copy_waits, copy_wait_us;
static void copy_wait(void)
{
    s64 t;
    if (!copy_busy)
        return;
    t = mono_us();
    while (copy_busy)
        futex(&copy_busy, 0 | 128, 1, 0);   /* FUTEX_WAIT_PRIVATE while 1 */
    copy_waits++;
    copy_wait_us += (u32)(mono_us() - t);
}
static void *copy_thread(void *arg)
{
    (void)arg;
    for (;;) {
        while (!copy_busy)
            futex(&copy_busy, 0 | 128, 0, 0);
        orig_update_screen();
        __sync_synchronize();
        copy_busy = 0;
        futex(&copy_busy, 1 | 128, 0x7fffffff, 0);
        /* the SDL video thread may have looked before the copy was done: wake it again (section 3) */
        __sync_fetch_and_add(&frame_seq, 1);
        futex(&frame_seq, 1 | 128, 0x7fffffff, 0);
    }
    return 0;
}
static void dsfast_update_screen(void)
{
    static u32 calls;
    s64 t0 = mono_us();
    u32 d;
    calls++;
    copy_wait();                             /* the previous copy, if it is still going */
    /* the first 30 calls draw directly in the SDL layer (its prepare_time) */
    static int hires_ok = -1;
    if (hires_ok < 0)
        hires_ok = getenv("DSFAST_HIRESCOPY") != 0;   /* test: also in high resolution */
    if (copy_mode == 1 && calls > 40 && (hires_ok || (!*(u8 *)(0x0aee952cu + 0x19) && !*(u8 *)(0x0aee952cu + 28 + 0x19))) &&
        t0 - last_frame_us < 100000) {
        static int started;
        if (!started) {
            unsigned long th;
            started = 1;
            pthread_create(&th, 0, copy_thread, 0);   /* placed on core 1 by pthread_create below */
        }
        copy_busy = 1;
        futex(&copy_busy, 1 | 128, 1, 0);
        us_async++;
    } else {
        orig_update_screen();
        us_sync++;
    }
    d = (u32)(mono_us() - t0);
    us_main_sum10 += d / 10;
    if (d > us_main_max)
        us_main_max = d;
}
/* called every frame until the SDL layer has detoured update_screen */
static void copy_setup_late(void)
{
    u32 *p = (u32 *)FN_UPDATE_SCREEN;
    if (copy_mode || !sdl_base || p[0] != 0xe51ff004u || p[1] != (u32)(sdl_base + SDL_UPDATE_SCREEN))
        return;
    orig_update_screen = (void (*)(void))(void *)p[1];
    copy_mode = getenv("DSFAST_SYNCCOPY") ? 2 : 1;
    mprotect((void *)(FN_UPDATE_SCREEN & ~4095u), 8192, 7);
    p[1] = (u32)dsfast_update_screen;
    flush_icache(p, p + 2);
}

/* ------------------------------------------------------------------ 8. hidden screen */

#define FN_RENDER_SCANLINE 0x0804c9f0u
#define DS_SCREENS 0x0aee952cu   /* DraStic's 2 screens, 28 bytes each: +4 pixels, +0x19 high-res mode */
static int *sdl_nds;             /* libSDL2 v1.8 'nds' struct, dis_mode at +0x10 */
static int *sdl_dis_mode;        /* direct &nds.dis_mode when mmiyoo_get_dis_mode() is exported */
static int no_skipeb;
static u32 hide_tramp[4] __attribute__((aligned(16)));   /* render_scanline's first 2 instructions + jump back */
static u8 *volatile hide_a, *volatile hide_a_cap;   /* engine A (the one with 3D) and its capture settings */
static int hidden_screen(void)
{
    u8 h0 = *(u8 *)(DS_SCREENS + 0x19);
    u8 h1 = *(u8 *)(DS_SCREENS + 28 + 0x19);
    int dm;
    if (!no_skipeb) {
        if (h0 && !h1)
            return 1;
        if (h1)
            return 0;
    } else if (h0 || h1) {
        /* DSFAST_NO_SKIPEB=1: keep Beta 4's hires_3d guard */
        return -1;
    }
    dm = sdl_dis_mode ? *sdl_dis_mode : (sdl_nds ? sdl_nds[4] : -1);
    if (dm == 2 || dm == 3)           /* NDS_DIS_MODE_S0 / S1 */
        return 0;
    return -1;
}
static void hide_render_scanline(u8 *v2d, u8 *dst, u32 line, u8 *cap)
{
    int hidden;
    if (copy_busy)                     /* 16. the copy of the last frame still reads the screens */
        copy_wait();
    hidden = hidden_screen();
    if (!v2d[0x97]) {
        hide_a = v2d;
        hide_a_cap = cap;
    }
    /* v2d +0x1c: this engine's output; cap: capture settings (engine A only), +0x2d capture on */
    if (hidden >= 0 && *(void **)(v2d + 0x1c) == *(void **)(DS_SCREENS + 28 * hidden + 4) && !(cap && cap[0x2d]))
        return;
    if (prof_on) {
        s64 t = mono_us();
        ((void (*)(u8 *, u8 *, u32, u8 *))(void *)hide_tramp)(v2d, dst, line, cap);
        pf_2d += (u32)(mono_us() - t);
        pf_lines++;
        return;
    }
    ((void (*)(u8 *, u8 *, u32, u8 *))(void *)hide_tramp)(v2d, dst, line, cap);
}
/* The 3D of a frame is not started (video_3d_start_rendering, threaded 3D) while engine A has been on the
   hidden screen, without display capture, for 8 frames in a row: games with 3D on both screens move it
   from screen to screen every frame (and capture it) and never get there. */
#define FN_VIDEO_3D_START_RENDERING 0x08078a94u
static u32 hide3d_tramp[4] __attribute__((aligned(16)));
static u32 hide3d_streak;
static void hide3d_start_rendering(void *v3d, u32 arg)
{
    int hidden = hidden_screen();
    u8 *a = hide_a, *cap = hide_a_cap;
    if (hidden >= 0 && a && *(void **)(a + 0x1c) == *(void **)(DS_SCREENS + 28 * hidden + 4) && !(cap && cap[0x2d])) {
        if (++hide3d_streak > 8)
            return;
    } else
        hide3d_streak = 0;
    ((void (*)(void *, u32))(void *)hide3d_tramp)(v3d, arg);
}
/* ------------------------------------------------------------------ 11. 3D split over both cores when needed
   DraStic splits the 3D picture in bins and reads batch_threads_3d_count (sys+0x857b4) every frame: bin i goes
   to thread i % count. Its 3 helper threads always exist. While the emulation keeps waiting for the 3D
   (video_3d_finish_rendering over 1 ms in 4 of the last 16 frames: 3D on both screens, heavy scenes), the
   count is 3, so a third of the bins is drawn on core 0 by the first helper; otherwise it is 1, as normal. */
#define FN_VIDEO_3D_FINISH_RENDERING 0x08078b68u
#define CFG_BATCH_3D 0x7b4
static u32 split_tramp[4] __attribute__((aligned(16)));
static u32 split_hist;
static u32 batch3d_max = 3;
static void split_finish_rendering(void *v3d)
{
    s64 t0 = mono_us();
    u32 h, c, x;
    ((void (*)(void *))(void *)split_tramp)(v3d);
    if (prof_on)
        pf_wait3d += (u32)(mono_us() - t0);
    h = (split_hist << 1) | (mono_us() - t0 > 1000);
    split_hist = h;
    for (c = 0, x = h & 0xffff; x; x &= x - 1)
        c++;
    if (g_sys) {
        u32 n = batch3d_max;
        CFG(g_sys, CFG_BATCH_3D) = c >= 4 ? n : c == 0 ? 1 : CFG(g_sys, CFG_BATCH_3D) == n ? n : 1;
    }
}

__attribute__((visibility("hidden"))) int dsfast_should_skip_eb(const u8 *video)
{
    int hidden = hidden_screen();
    if (hidden < 0)
        return 0;
    return *(void *const *)(video + 0x82d30 + 0x1c) == *(void *const *)(DS_SCREENS + 28 * hidden + 4);
}

/* Called by dsfast_engine_b_tramp after skipping hidden Engine B: run video_2d_render_scanlines on it
   with screen_ptr (v2d +0x1c) cleared, as DraStic does for a skipped frame, so its event queue (deferred
   palette, OAM and 2D register writes of this frame) still reaches palette_ram / the engine state but
   nothing is drawn. A POWCNT1 event in the queue may set a new screen_ptr: keep that one. */
__attribute__((visibility("hidden"))) void dsfast_eb_events_only(u8 *video)
{
    u8 *v2d = video + 0x82d30;
    void *saved = *(void **)(v2d + 0x1c);
    *(void **)(v2d + 0x1c) = 0;
    ((void (*)(u8 *, u32, u32, void *))0x0804f480u)(v2d, 0, 191, 0);
    if (!*(void **)(v2d + 0x1c))
        *(void **)(v2d + 0x1c) = saved;
}

extern void dsfast_engine_b_tramp(void);

static void hide_setup(void)
{
    int *(*get_dm)(void) = (int *(*)(void))dlsym((void *)0, "mmiyoo_get_dis_mode");
    u8 *init = (u8 *)dlsym((void *)0, "SDL_Init");
    const char *noeb = getenv("DSFAST_NO_SKIPEB");
    if (noeb && noeb[0] == '1')
        no_skipeb = 1;
    if (get_dm)
        sdl_dis_mode = get_dm();
    /* with stock libSDL2 v1.8: SDL_Init at 0x35470, process_screen (push r4-r11, lr) at 0xe6178 */
    if (init && *(u32 *)(init - 0x35470 + 0xe6178) == 0xe92d4ff0u) {
        sdl_nds = (int *)(init - 0x35470 + 0x16e4a0);
        sdl_base = init - 0x35470;
    }
    if (!sdl_dis_mode && !sdl_nds && no_skipeb)
        return;
    hide_tramp[0] = *(u32 *)FN_RENDER_SCANLINE;         /* strd r4, r5, [sp, #-36]! */
    hide_tramp[1] = *(u32 *)(FN_RENDER_SCANLINE + 4);   /* movw r5, #0x2008 */
    hide_tramp[2] = 0xe51ff004;                          /* ldr pc, [pc, #-4] */
    hide_tramp[3] = FN_RENDER_SCANLINE + 8;
    mprotect((void *)((u32)hide_tramp & ~4095u), 8192, 7);
    flush_icache(hide_tramp, hide_tramp + 4);
    patch(FN_RENDER_SCANLINE, (void *)hide_render_scanline);
    hide3d_tramp[0] = *(u32 *)FN_VIDEO_3D_START_RENDERING;        /* strd r4, r5, [sp, #-32]! */
    hide3d_tramp[1] = *(u32 *)(FN_VIDEO_3D_START_RENDERING + 4);  /* mov r4, r0 */
    hide3d_tramp[2] = 0xe51ff004;
    hide3d_tramp[3] = FN_VIDEO_3D_START_RENDERING + 8;
    mprotect((void *)((u32)hide3d_tramp & ~4095u), 8192, 7);
    flush_icache(hide3d_tramp, hide3d_tramp + 4);
    patch(FN_VIDEO_3D_START_RENDERING, (void *)hide3d_start_rendering);
    if (!no_skipeb && *(u32 *)0x08035facu == 0xe59d9008u && *(u32 *)0x08035fb0u == 0xe30a8c98u)
        patch(0x08035facu, (void *)dsfast_engine_b_tramp);
}

/* ------------------------------------------------------------------ 17. where a frame's time goes (DSFAST_PROF=1)
   For studies only (the timing itself costs a little): per frame, separately for frames drawn and skipped,
   the time of the 2D lines (render_scanline, on the emulation thread), the emulation's waits for the 3D
   (video_3d_finish_rendering) and the 3D work of all threads (video_3d_render_bins_1x/4x). In dsfast.log. */
#define FN_RENDER_BINS_1X 0x08074338u
#define FN_RENDER_BINS_4X 0x08077b80u
static int prof_on;
static u32 pf_2d, pf_wait3d, pf_lines;
static volatile u32 pf_3d;
static u32 pd[2][6], pd_hist[2][64];        /* [skipped, drawn]: n, work, 2d, wait3d, 3d, lines */
static int prof_drawn = 1;
static u32 bins1_tramp[4] __attribute__((aligned(16))), bins4_tramp[4] __attribute__((aligned(16)));
static void prof_bins1(void *a)
{
    s64 t = mono_us();
    ((void (*)(void *))(void *)bins1_tramp)(a);
    __sync_fetch_and_add(&pf_3d, (u32)(mono_us() - t));
}
static void prof_bins4(void *a)
{
    s64 t = mono_us();
    ((void (*)(void *))(void *)bins4_tramp)(a);
    __sync_fetch_and_add(&pf_3d, (u32)(mono_us() - t));
}
/* end of a frame (from system_frame_sync): w = its emulation time, drawn_next = it will draw the next one */
static void prof_frame(u32 w, int counted, int drawn_next)
{
    u32 *p = pd[prof_drawn], d3 = __sync_lock_test_and_set(&pf_3d, 0);
    if (counted) {
        p[0]++;
        p[1] += w / 10;
        p[2] += pf_2d / 10;
        p[3] += pf_wait3d / 10;
        p[4] += d3 / 10;
        p[5] += pf_lines;
        pd_hist[prof_drawn][w / 1000 < 63 ? w / 1000 : 63]++;
    }
    pf_2d = pf_wait3d = pf_lines = 0;
    prof_drawn = drawn_next;
}
/* ------------------------------------------------------------------ 18. 3D 4x bin work-stealing
   Replace static modulo assignment (bin i -> thread i % count) in video_3d_render_bins_4x with an
   atomic 0..11 bin counter when multi-threaded 3D is active. Because Main Thread runs at nice -5 on
   core 0, helper w0 sleeps while core 0 finishes CPU/Geom/2D; work-stealing lets core 1 claim bins
   1..11 without stalling on w0, while w0 picks up remaining bins as soon as main hits finish_rendering. */
#define FN_VIDEO_3D_RENDER_BINS_4X 0x08077b80u
#define FN_UPDATE_FRAME_3D_4X_PREP 0x080785acu
extern volatile u32 dsfast_next_bin;
extern void dsfast_prep_bins_4x(void);
static int steal3d_on;
static u32 steal_tramp[4] __attribute__((aligned(16)));

static void dsfast_render_bins_4x(u8 *rt)
{
    void (*orig)(u8 *) = (void (*)(u8 *))(void *)steal_tramp;
    u8 orig_base = rt[0x240aa];
    u8 orig_inter = rt[0x240ab];
    s64 t = prof_on ? mono_us() : 0;
    if (orig_inter <= 1) {
        orig(rt);
        if (prof_on)
            __sync_fetch_and_add(&pf_3d, (u32)(mono_us() - t));
        return;
    }
    rt[0x240ab] = 12; /* __aeabi_idiv(12, 12) == 1 bin per call */
    for (;;) {
        u32 bin = __atomic_fetch_add(&dsfast_next_bin, 1, __ATOMIC_RELAXED);
        if (bin >= 12)
            break;
        rt[0x240aa] = (u8)bin;
        orig(rt);
    }
    rt[0x240aa] = orig_base;
    rt[0x240ab] = orig_inter;
    if (prof_on)
        __sync_fetch_and_add(&pf_3d, (u32)(mono_us() - t));
}

static void steal3d_setup(void)
{
    const char *off = getenv("DSFAST_NO_STEAL");
    if (off && off[0] == '1')
        return;
    if (*(u32 *)FN_UPDATE_FRAME_3D_4X_PREP != 0xe5943004u ||
        *(u32 *)(FN_UPDATE_FRAME_3D_4X_PREP + 4) != 0xe285296fu)
        return;
    if (*(u32 *)FN_VIDEO_3D_RENDER_BINS_4X != 0xe2803909u ||
        *(u32 *)(FN_VIDEO_3D_RENDER_BINS_4X + 4) != 0xe16d42f4u)
        return;
    steal_tramp[0] = *(u32 *)FN_VIDEO_3D_RENDER_BINS_4X;       /* add r3, r0, #0x24000 */
    steal_tramp[1] = *(u32 *)(FN_VIDEO_3D_RENDER_BINS_4X + 4); /* strd r4, [sp, #-36]! */
    steal_tramp[2] = 0xe51ff004u;
    steal_tramp[3] = FN_VIDEO_3D_RENDER_BINS_4X + 8;
    mprotect((void *)((u32)steal_tramp & ~4095u), 8192, 7);
    flush_icache(steal_tramp, steal_tramp + 4);
    patch(FN_UPDATE_FRAME_3D_4X_PREP, (void *)dsfast_prep_bins_4x);
    patch(FN_VIDEO_3D_RENDER_BINS_4X, (void *)dsfast_render_bins_4x);
    steal3d_on = 1;
}

static void prof_setup(void)
{
    if (!getenv("DSFAST_PROF"))
        return;
    prof_on = 1;
    bins1_tramp[0] = *(u32 *)FN_RENDER_BINS_1X;          /* add r3, r0, #0x24000 */
    bins1_tramp[1] = *(u32 *)(FN_RENDER_BINS_1X + 4);    /* strd r4, r5, [sp, #-36]! */
    bins1_tramp[2] = 0xe51ff004;
    bins1_tramp[3] = FN_RENDER_BINS_1X + 8;
    mprotect((void *)((u32)bins1_tramp & ~4095u), 8192, 7);
    flush_icache(bins1_tramp, bins1_tramp + 4);
    patch(FN_RENDER_BINS_1X, (void *)prof_bins1);
    if (!steal3d_on) {
        bins4_tramp[0] = *(u32 *)FN_RENDER_BINS_4X;
        bins4_tramp[1] = *(u32 *)(FN_RENDER_BINS_4X + 4);
        bins4_tramp[2] = 0xe51ff004;
        bins4_tramp[3] = FN_RENDER_BINS_4X + 8;
        mprotect((void *)((u32)bins4_tramp & ~4095u), 8192, 7);
        flush_icache(bins4_tramp, bins4_tramp + 4);
        patch(FN_RENDER_BINS_4X, (void *)prof_bins4);
    }
}

static void split_setup(void)
{
    /* With 3D bin work-stealing enabled, 2 threads (1 on core 1 + idle-fill w0 on core 0) beat 3
     * by avoiding core-1 context-switching and L1/L2 thrashing between two 144 KB render targets. */
    const char *env_b = getenv("DSFAST_BATCH3D");
    if (steal3d_on)
        batch3d_max = 2;
    if (env_b && env_b[0] >= '1' && env_b[0] <= '4' && !env_b[1])
        batch3d_max = (u32)(env_b[0] - '0');
    split_tramp[0] = *(u32 *)FN_VIDEO_3D_FINISH_RENDERING;        /* strd r4, r5, [sp, #-24]! */
    split_tramp[1] = *(u32 *)(FN_VIDEO_3D_FINISH_RENDERING + 4);  /* movw r4, #0x93b8 */
    split_tramp[2] = 0xe51ff004;
    split_tramp[3] = FN_VIDEO_3D_FINISH_RENDERING + 8;
    mprotect((void *)((u32)split_tramp & ~4095u), 8192, 7);
    flush_icache(split_tramp, split_tramp + 4);
    patch(FN_VIDEO_3D_FINISH_RENDERING, (void *)split_finish_rendering);
}

/* ------------------------------------------------------------------ 18. 3D 4x skipped-frame copy bypass
   When a 4x frame is skipped right before finishing 3D, update_frame_3d_4x copies the entire
   512x384 (768 KB) previous color buffer via memcpy. Pointing color_buffer_render straight at
   color_buffer_last_rendered achieves the exact same reuse in a single store (DSFAST_NO_COPY3D=1). */
#define FN_3D_4X_SKIP_COPY 0x08078458u
static void copy3d_setup(void)
{
    const char *off = getenv("DSFAST_NO_COPY3D");
    u32 *p = (u32 *)FN_3D_4X_SKIP_COPY;
    if (off && off[0] == '1')
        return;
    if (p[0] == 0xe3a02703u && p[1] == 0xe28dd01cu) {
        mprotect((void *)(FN_3D_4X_SKIP_COPY & ~4095u), 8192, 7);
        p[0] = 0xe5881054u; /* str r1, [r8, #84] (color_buffer_render = color_buffer_last_rendered) */
        p[1] = 0xea000018u; /* b 0x080784c4 */
        flush_icache(p, p + 2);
    }
}

/* ------------------------------------------------------------------ 19. bit-exact NEON + PLD 3D 4x & 2D kernels */
#define HIDDEN __attribute__((visibility("hidden")))
HIDDEN extern void dsfast_resolve_bin_4x(void);
HIDDEN extern void dsfast_writeback_4x(void);
HIDDEN extern void dsfast_load_depth_4x(void);
HIDDEN extern void dsfast_load_depth_colors_id_4x(void);
HIDDEN extern void dsfast_writeback_all_pass_4x(void);
HIDDEN extern void dsfast_render_scanline_set_3d_visibility(void);
HIDDEN extern void dsfast_select_pixels_binary32(void);
HIDDEN extern void dsfast_edge_mark_4x(void);
HIDDEN extern void dsfast_edge_identify_4x(void);
#undef HIDDEN

static void neon3d_setup(void)
{
    static const struct { u32 addr; u32 insn; void (*fn)(void); } k[] = {
        { 0x080c50fcu, 0xe3a03020u, dsfast_resolve_bin_4x },
        { 0x080c4a90u, 0xe92d4ff0u, dsfast_writeback_4x },
        { 0x080c49d0u, 0xe282cd16u, dsfast_load_depth_4x },
        { 0x080c4a18u, 0xe92d47f0u, dsfast_load_depth_colors_id_4x },
        { 0x080c4b08u, 0xe92d4070u, dsfast_writeback_all_pass_4x },
        { 0x08048c50u, 0xe16d41f0u, dsfast_render_scanline_set_3d_visibility },
        { 0x080bef80u, 0xe92d4010u, dsfast_select_pixels_binary32 },
        { 0x080c55a0u, 0xe92d4000u, dsfast_edge_mark_4x },
        { 0x080c5658u, 0xe92d4030u, dsfast_edge_identify_4x },
    };
    const char *off = getenv("DSFAST_NO_NEON3D");
    unsigned i;
    if (off && off[0] == '1')
        return;
    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (*(u32 *)k[i].addr == k[i].insn)
            patch(k[i].addr, (void *)k[i].fn);
    }
}
__asm__(
    ".syntax unified\n"
    ".arch armv7-a\n"
    ".arm\n"
    ".text\n"
    ".global dsfast_engine_b_tramp\n"
    ".hidden dsfast_engine_b_tramp\n"
    ".type dsfast_engine_b_tramp, %function\n"
    ".p2align 4\n"
"dsfast_engine_b_tramp:\n"
    "ldr     r0, [sp, #8]\n"
    "bl      dsfast_should_skip_eb\n"
    "cmp     r0, #0\n"
    "beq     .Leb_orig\n"
    "ldr     r9, [sp, #8]\n"
    "movw    r3, #44096\n"
    "movt    r3, #67\n"
    "mov     r1, #0\n"
    "mov     r2, #191\n"
    "add     r0, r9, #7616\n"
    "add     r3, r9, r3\n"
    "ldr     ip, .Leb_render2d\n"
    "blx     ip\n"
    "mov     r0, r9\n"
    "bl      dsfast_eb_events_only\n"
    "ldr     pc, .Leb_epilogue\n"
".Leb_orig:\n"
    "ldr     r9, [sp, #8]\n"
    "movw    r8, #44184\n"
    "ldr     pc, .Leb_ret\n"
    ".align 2\n"
".Leb_render2d:\n"
    ".word   0x0804f480\n"
".Leb_epilogue:\n"
    ".word   0x08035ed0\n"
".Leb_ret:\n"
    ".word   0x08035fb4\n"
    ".size dsfast_engine_b_tramp, .-dsfast_engine_b_tramp\n"

    ".data\n"
    ".align 4\n"
    ".global dsfast_next_bin\n"
    ".hidden dsfast_next_bin\n"
"dsfast_next_bin:\n"
    ".word   0\n"

    ".text\n"
    ".align 4\n"
    ".global dsfast_prep_bins_4x\n"
    ".hidden dsfast_prep_bins_4x\n"
    ".type dsfast_prep_bins_4x, %function\n"
"dsfast_prep_bins_4x:\n"
    "ldr     r3, .Lnext_bin_off\n"
".Lpic_next_bin:\n"
    "add     r3, pc, r3\n"
    "mov     r2, #0\n"
    "str     r2, [r3]\n"
    "dmb     ish\n"
    "ldr     r3, [r4, #4]\n"
    "add     r2, r5, #0x1bc000\n"
    "ldr     pc, .Lprep_ret\n"
".Lnext_bin_off:\n"
    ".word   dsfast_next_bin - (.Lpic_next_bin + 8)\n"
".Lprep_ret:\n"
    ".word   0x080785b4\n"
    ".size dsfast_prep_bins_4x, .-dsfast_prep_bins_4x\n"
);

__attribute__((constructor)) static void dsfast_init(void)
{
    char exe[256];
    long n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    /* act only inside the drastic executable: the programs it starts (sh...) inherit LD_PRELOAD */
    if (n < 8)
        return;
    exe[n] = 0;
    if (exe[n - 8] != '/' || exe[n - 7] != 'd' || exe[n - 6] != 'r' || exe[n - 5] != 'a' || exe[n - 4] != 's' ||
        exe[n - 3] != 't' || exe[n - 2] != 'i' || exe[n - 1] != 'c')
        return;
    /* and only with the DraStic build these addresses belong to (first word of system_frame_sync) */
    if (*(u32 *)FN_SYSTEM_FRAME_SYNC != 0xe16d42f4u)
        return;
    active = 1;
    set_core(CORE0);   /* the emulation; threads it creates are placed in pthread_create */
    {
        const char *no_nice = getenv("DSFAST_NO_NICE");
        if (no_nice && no_nice[0] == '1')
            use_nice = 0;
        if (use_nice)
            sys3(97, 0, 0, -5); /* setpriority: keep Core 0 priority above w0 3D helper (nice 0/+5) */
    }
    if (*(u32 *)FN_GET_TICKS_US == 0xe16d40fcu)
        patch(FN_GET_TICKS_US, (void *)dsfast_get_ticks_us);
    patch(FN_SYSTEM_FRAME_SYNC, (void *)dsfast_system_frame_sync);
    {
        struct timespec ts;
        clock_gettime(1, &ts);
        start_sec = ts.tv_sec;
    }
    hide_setup();
    steal3d_setup();
    split_setup();
    copy3d_setup();
    neon3d_setup();
    prof_setup();
    blur_setup();
    mhz_start = cpu_mhz();
}
