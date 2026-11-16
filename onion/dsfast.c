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
 *    screen 1; the high-res 3D mode shows only screen 0) the other screen was drawn line by line for nothing.
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
static void frame_ready(void)
{
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
    if (!a_t0 || now - a_tlast > 200000) {   /* start, or sound was stopped (menu, loading) */
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
static void (*get_ticks_us)(s64 *t) = (void (*)(s64 *))0x080a7d50u;
static void (*delay_us)(u32 us) = (void (*)(u32))0x080a7dc0u;
static u32 (*audio_buffer_state)(void *a) = (u32 (*)(void *))0x080aa718u;
static void (*audio_buffer_force_feed)(void *a) = (void (*)(void *))0x080aa760u;

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
    get_ticks_us(&t0);
    if (us > 11000)
        delay_us(us - 11000);
    do
        get_ticks_us(&t);
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
void dsfast_system_frame_sync(void *sys)
{
    g_sys = sys;
    frame_ready();
    frame_sync(sys);
}

/* ------------------------------------------------------------------ 2. page flip */

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOPAN_DISPLAY     0x4606
#define PAGE_H              480

struct fbvar { u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset; u32 rest[34]; };

static int (*real_ioctl)(int, unsigned long, void *);
static int flip_state;            /* 0 not set up, 1 on, -1 off (plain flips) */
static pmutex_t fmtx;
static pcond_t fcond;
static int pending = -1, ffd;
static u32 orig_yres_virtual;
static struct fbvar fvar;

static void measure_vsync(void)
{
    static s64 last;
    static u32 n, sum;
    s64 t = mono_us();
    u32 d = (u32)(t - last);
    last = t;
    if (d < 14000 || d > 19500)          /* not one refresh apart */
        return;
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
    if (real_ioctl(fd, FBIOGET_VSCREENINFO, &cur) || cur.yres != PAGE_H)
        return;
    orig_yres_virtual = cur.yres_virtual;
    if (cur.yres_virtual < 3 * PAGE_H) {
        cur.yres_virtual = 3 * PAGE_H;
        if (real_ioctl(fd, FBIOPUT_VSCREENINFO, &cur))
            return;
        real_ioctl(fd, FBIOGET_VSCREENINFO, &cur);
        if (cur.yres_virtual < 3 * PAGE_H)
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
    if (flip_state != 1)
        return real_ioctl(fd, req, arg);
    page = v->yoffset / PAGE_H;
    pthread_mutex_lock(&fmtx);
    while (pending >= 0)               /* the previous flip has not reached the screen yet */
        pthread_cond_wait(&fcond, &fmtx);
    pending = (int)(page * PAGE_H);
    pthread_cond_broadcast(&fcond);
    pthread_mutex_unlock(&fmtx);
    /* the SDL layer does yoffset ^= 480 after this call: make that land on the next of the 3 pages */
    v->yoffset = (((page + 1) % 3) * PAGE_H) ^ PAGE_H;
    return 0;
}

__attribute__((destructor)) static void dsfast_end(void)
{
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
struct tramp { void *(*fn)(void *); void *arg; unsigned long mask; };
extern void *malloc(unsigned int n);
extern void free(void *p);
static void *tramp_run(void *p)
{
    struct tramp t = *(struct tramp *)p;
    free(p);
    set_core(t.mask);
    return t.fn(t.arg);
}
int pthread_create(unsigned long *th, void *attr, void *(*fn)(void *), void *arg)
{
    static int (*real)(unsigned long *, void *, void *(*)(void *), void *);
    u32 a = (u32)fn;
    unsigned long mask = CORE1;   /* 3D (video_3d_run_thread, video_3d_render_thread), SDL audio and video... */
    struct tramp *t;
    if (!real)
        real = (int (*)(unsigned long *, void *, void *(*)(void *), void *))dlsym(RTLD_NEXT, "pthread_create");
    if (a == FN_VIDEO_RENDER_THREAD || fn == flip_thread)
        mask = CORE0;
    if (a == FN_VIDEO_3D_RENDER_THREAD && helpers_seen++ == 0)
        mask = CORE0;             /* 11. the first 3D helper (bins 1, 4, 7...) runs next to the emulation */
    if (!active || !(t = (struct tramp *)malloc(sizeof(*t))))
        return real(th, attr, fn, arg);
    t->fn = fn;
    t->arg = arg;
    t->mask = mask;
    return real(th, attr, tramp_run, t);
}

/* ------------------------------------------------------------------ 8. hidden screen */

#define FN_RENDER_SCANLINE 0x0804c9f0u
#define DS_SCREENS 0x0aee952cu   /* DraStic's 2 screens, 28 bytes each: +4 pixels, +0x19 high-res mode */
static int *sdl_nds;             /* libSDL2 v1.8 'nds' struct, dis_mode at +0x10 */
static u32 hide_tramp[4] __attribute__((aligned(16)));   /* render_scanline's first 2 instructions + jump back */
static u8 *volatile hide_a, *volatile hide_a_cap;   /* engine A (the one with 3D) and its capture settings */
static int hidden_screen(void)
{
    if (*(u8 *)(DS_SCREENS + 0x19))
        return 1;
    if (sdl_nds[4] == 2 || sdl_nds[4] == 3)           /* NDS_DIS_MODE_S0 / S1 */
        return 0;
    return -1;
}
static void hide_render_scanline(u8 *v2d, u8 *dst, u32 line, u8 *cap)
{
    int hidden = hidden_screen();
    if (!v2d[0x97]) {
        hide_a = v2d;
        hide_a_cap = cap;
    }
    /* v2d +0x1c: this engine's output; cap: capture settings (engine A only), +0x2d capture on */
    if (hidden >= 0 && *(void **)(v2d + 0x1c) == *(void **)(DS_SCREENS + 28 * hidden + 4) && !(cap && cap[0x2d]))
        return;
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
static void split_finish_rendering(void *v3d)
{
    s64 t0 = mono_us();
    u32 h, c, x;
    ((void (*)(void *))(void *)split_tramp)(v3d);
    h = (split_hist << 1) | (mono_us() - t0 > 1000);
    split_hist = h;
    for (c = 0, x = h & 0xffff; x; x &= x - 1)
        c++;
    if (g_sys)
        CFG(g_sys, CFG_BATCH_3D) = c >= 4 ? 3 : c == 0 ? 1 : CFG(g_sys, CFG_BATCH_3D) == 3 ? 3 : 1;
}

static void hide_setup(void)
{
    u8 *init = (u8 *)dlsym((void *)0, "SDL_Init");
    /* only with the libSDL2 v1.8 build: SDL_Init at 0x35470, process_screen (push r4-r11, lr) at 0xe6178 */
    if (!init || *(u32 *)(init - 0x35470 + 0xe6178) != 0xe92d4ff0u)
        return;
    sdl_nds = (int *)(init - 0x35470 + 0x16e4a0);
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
}

static void split_setup(void)
{
    split_tramp[0] = *(u32 *)FN_VIDEO_3D_FINISH_RENDERING;        /* strd r4, r5, [sp, #-24]! */
    split_tramp[1] = *(u32 *)(FN_VIDEO_3D_FINISH_RENDERING + 4);  /* movw r4, #0x93b8 */
    split_tramp[2] = 0xe51ff004;
    split_tramp[3] = FN_VIDEO_3D_FINISH_RENDERING + 8;
    mprotect((void *)((u32)split_tramp & ~4095u), 8192, 7);
    flush_icache(split_tramp, split_tramp + 4);
    patch(FN_VIDEO_3D_FINISH_RENDERING, (void *)split_finish_rendering);
}

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
    patch(FN_SYSTEM_FRAME_SYNC, (void *)dsfast_system_frame_sync);
    hide_setup();
    split_setup();
}
