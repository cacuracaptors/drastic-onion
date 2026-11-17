/* libdsbench: lightweight LD_PRELOAD benchmark & frame-pacing logger for DraStic r2.5.0.4.
 *
 * Logs per-frame times in microseconds plus getrusage deltas (majflt, minflt, nvcsw, nivcsw),
 * matching tools-miyoo/analysis/frametimes.py. Buffers in RAM and flushes on exit to avoid
 * SD card I/O jitter during measurement.
 *
 * Load order:
 *   stock:        LD_PRELOAD=".../libdsbench.so"
 *   with dsfast:  LD_PRELOAD=".../libdsfast.so:.../libdsbench.so"
 *
 * Environment variables:
 *   DSBENCH_LOG        Output path for real FBIOPAN_DISPLAY intervals (panel pacing)
 *   DSBENCH_EMU_LOG    Output path for emulation update_input intervals
 *   DSBENCH_LOAD_SLOT  If set (>=0), calls load_state_index(slot) at frame 25 (after auto_state)
 *   DSBENCH_FRAMES     If set (>0), quits cleanly after N recorded emulation frames
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef long long s64;
typedef unsigned long long u64;

extern long readlink(const char *path, char *buf, unsigned long len);
extern int mprotect(void *addr, unsigned long len, int prot);
extern char *getenv(const char *name);

#define FBIOPAN_DISPLAY 0x4606

#define FN_QUIT             0x08006444u
#define FN_LOAD_STATE_INDEX 0x08095ce4u
#define FN_UPDATE_INPUT     0x080a65e0u
#define INPUT_SYS_OFFSET    0x80004u

#define MAX_SAMPLES 32768

struct k_timespec {
    long tv_sec;
    long tv_nsec;
};

struct sample {
    u32 us;
    u32 majflt;
    u32 minflt;
    u32 nvcsw;
    u32 nivcsw;
};

static long sys1(long nr, long a)
{
    register long r0 __asm__("r0") = a;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long sys2(long nr, long a, long b)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return r0;
}

static long sys3(long nr, long a, long b, long c)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static s64 mono_us(void)
{
    struct k_timespec ts;
    sys2(263, 1, (long)&ts); /* __NR_clock_gettime(CLOCK_MONOTONIC, &ts) */
    return (s64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void get_ru(u32 *maj, u32 *min, u32 *nvcsw, u32 *nivcsw)
{
    long ru[18];
    sys2(77, 0, (long)ru); /* __NR_getrusage(RUSAGE_SELF, ru) */
    *min = (u32)ru[8];
    *maj = (u32)ru[9];
    *nvcsw = (u32)ru[16];
    *nivcsw = (u32)ru[17];
}

static void flush_icache(void *start, void *end)
{
    register long r0 __asm__("r0") = (long)start;
    register long r1 __asm__("r1") = (long)end;
    register long r2 __asm__("r2") = 0;
    register long r7 __asm__("r7") = 0xf0002;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
}

static void patch(u32 at, void *to)
{
    u32 *p = (u32 *)at;
    mprotect((void *)(at & ~4095u), 8192, 7);
    p[0] = 0xe51ff004;
    p[1] = (u32)to;
    flush_icache(p, p + 2);
}

static int parse_int(const char *s, int def)
{
    int sign = 1, val = 0;
    if (!s || !*s)
        return def;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    if (*s < '0' || *s > '9')
        return def;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}

static int fmt_u32(char *dst, u32 v)
{
    char tmp[16];
    int n = 0, i;
    if (v == 0) {
        dst[0] = '0';
        return 1;
    }
    while (v) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = 0; i < n; i++)
        dst[i] = tmp[n - 1 - i];
    return n;
}

static void write_samples(const char *path, const struct sample *arr, u32 count)
{
    int fd;
    char buf[4096];
    int pos = 0;
    u32 i;
    if (!path || !*path)
        return;
    fd = (int)sys3(5, (long)path, 0x241, 0644); /* open O_WRONLY|O_CREAT|O_TRUNC */
    if (fd < 0)
        return;
    for (i = 0; i < count; i++) {
        if (pos > (int)sizeof(buf) - 96) {
            sys3(4, fd, (long)buf, pos);
            pos = 0;
        }
        pos += fmt_u32(buf + pos, arr[i].us);
        buf[pos++] = ' ';
        pos += fmt_u32(buf + pos, arr[i].majflt);
        buf[pos++] = ' ';
        pos += fmt_u32(buf + pos, arr[i].minflt);
        buf[pos++] = ' ';
        pos += fmt_u32(buf + pos, arr[i].nvcsw);
        buf[pos++] = ' ';
        pos += fmt_u32(buf + pos, arr[i].nivcsw);
        buf[pos++] = '\n';
    }
    if (pos > 0)
        sys3(4, fd, (long)buf, pos);
    sys1(6, fd);
}

static int active;
static volatile int recording;
static volatile int flushed;

static const char *log_flip_path;
static const char *log_emu_path;
static const char *shot_path;
static int target_load_slot = -1;
static int target_frames = 0;

static struct sample flip_samples[MAX_SAMPLES];
static volatile u32 flip_count;
static s64 flip_prev_us;
static u32 flip_pmaj, flip_pmin, flip_pnvc, flip_pniv;

static struct sample emu_samples[MAX_SAMPLES];
static u32 emu_seen;
static u32 emu_count;
static s64 emu_prev_us;
static u32 emu_pmaj, emu_pmin, emu_pnvc, emu_pniv;

static void capture_shot(const char *path)
{
    char pan[32], buf[4096];
    int pfd, fb, out, i, y = 0;
    long n, rem = 640 * 480 * 4;
    if (!path || !*path)
        return;
    pfd = (int)sys3(5, (long)"/sys/class/graphics/fb0/pan", 0, 0);
    if (pfd >= 0) {
        n = sys3(3, pfd, (long)pan, sizeof(pan) - 1);
        sys1(6, pfd);
        if (n > 0 && n < (long)sizeof(pan)) {
            pan[n] = 0;
            for (i = 0; pan[i]; i++) {
                if (pan[i] == ',') {
                    y = parse_int(pan + i + 1, 0);
                    break;
                }
            }
        }
    }
    fb = (int)sys3(5, (long)"/dev/fb0", 0, 0);
    if (fb < 0)
        return;
    out = (int)sys3(5, (long)path, 0x241, 0644);
    if (out >= 0) {
        sys3(19, fb, (long)y * 640 * 4, 0); /* __NR_lseek SEEK_SET */
        while (rem > 0) {
            long want = rem > (long)sizeof(buf) ? (long)sizeof(buf) : rem;
            n = sys3(3, fb, (long)buf, want);
            if (n <= 0)
                break;
            sys3(4, out, (long)buf, n);
            rem -= n;
        }
        sys1(6, out);
    }
    sys1(6, fb);
}

static void flush_all(void)
{
    if (!active || __sync_lock_test_and_set(&flushed, 1))
        return;
    write_samples(log_flip_path, flip_samples, flip_count);
    write_samples(log_emu_path, emu_samples, emu_count);
}

int ioctl(int fd, unsigned long req, void *arg)
{
    int ret = (int)sys3(54, fd, (long)req, (long)arg); /* __NR_ioctl */
    if (active && req == FBIOPAN_DISPLAY && recording) {
        s64 now = mono_us();
        u32 maj, min, nvc, niv;
        get_ru(&maj, &min, &nvc, &niv);
        if (flip_prev_us == 0) {
            flip_prev_us = now;
            flip_pmaj = maj; flip_pmin = min; flip_pnvc = nvc; flip_pniv = niv;
        } else {
            u32 idx = flip_count;
            if (idx < MAX_SAMPLES) {
                s64 dt = now - flip_prev_us;
                flip_samples[idx].us = dt > 0 ? (u32)dt : 0;
                flip_samples[idx].majflt = maj - flip_pmaj;
                flip_samples[idx].minflt = min - flip_pmin;
                flip_samples[idx].nvcsw = nvc - flip_pnvc;
                flip_samples[idx].nivcsw = niv - flip_pniv;
                flip_count = idx + 1;
            }
            flip_prev_us = now;
            flip_pmaj = maj; flip_pmin = min; flip_pnvc = nvc; flip_pniv = niv;
        }
    }
    return ret;
}

static u32 input_tramp[4] __attribute__((aligned(16)));
static void hook_update_input(void *input)
{
    void *sys = *(void **)((u8 *)input + INPUT_SYS_OFFSET);
    ((void (*)(void *))(void *)input_tramp)(input);

    emu_seen++;
    if (emu_seen == 25) {
        if (target_load_slot >= 0) {
            ((int (*)(void *, u32, u16 *, u16 *, u32))FN_LOAD_STATE_INDEX)(
                sys, (u32)target_load_slot, 0, 0, 0);
        }
        flip_prev_us = 0;
        emu_prev_us = mono_us();
        get_ru(&emu_pmaj, &emu_pmin, &emu_pnvc, &emu_pniv);
        __sync_synchronize();
        recording = 1;
        return;
    }

    if (!recording)
        return;

    if (log_emu_path && emu_count < MAX_SAMPLES) {
        s64 now = mono_us();
        u32 maj, min, nvc, niv;
        get_ru(&maj, &min, &nvc, &niv);
        s64 dt = now - emu_prev_us;
        emu_samples[emu_count].us = dt > 0 ? (u32)dt : 0;
        emu_samples[emu_count].majflt = maj - emu_pmaj;
        emu_samples[emu_count].minflt = min - emu_pmin;
        emu_samples[emu_count].nvcsw = nvc - emu_pnvc;
        emu_samples[emu_count].nivcsw = niv - emu_pniv;
        emu_count++;
        emu_prev_us = now;
        emu_pmaj = maj; emu_pmin = min; emu_pnvc = nvc; emu_pniv = niv;
    } else if (!log_emu_path) {
        emu_count++;
    }

    if (target_frames > 0 && (int)emu_count >= target_frames) {
        recording = 0;
        flush_all();
        capture_shot(shot_path);
        ((void (*)(void *))FN_QUIT)(sys);
    }
}

__attribute__((destructor)) static void dsbench_fini(void)
{
    flush_all();
}

__attribute__((constructor)) static void dsbench_init(void)
{
    char exe[256];
    long n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 8)
        return;
    exe[n] = 0;
    if (exe[n - 8] != '/' || exe[n - 7] != 'd' || exe[n - 6] != 'r' || exe[n - 5] != 'a' ||
        exe[n - 4] != 's' || exe[n - 3] != 't' || exe[n - 2] != 'i' || exe[n - 1] != 'c')
        return;
    if (*(u32 *)FN_UPDATE_INPUT != 0xe16d42f4u)
        return;

    log_flip_path = getenv("DSBENCH_LOG");
    log_emu_path = getenv("DSBENCH_EMU_LOG");
    shot_path = getenv("DSBENCH_SHOT");
    target_load_slot = parse_int(getenv("DSBENCH_LOAD_SLOT"), -1);
    target_frames = parse_int(getenv("DSBENCH_FRAMES"), 0);

    if (!log_flip_path && !log_emu_path && target_load_slot < 0 && target_frames <= 0)
        return;

    input_tramp[0] = *(u32 *)FN_UPDATE_INPUT;       /* strd r4, [sp, #-36]! */
    input_tramp[1] = *(u32 *)(FN_UPDATE_INPUT + 4); /* add r5, r0, #0x80000 */
    input_tramp[2] = 0xe51ff004;                    /* ldr pc, [pc, #-4] */
    input_tramp[3] = FN_UPDATE_INPUT + 8;
    mprotect((void *)((u32)input_tramp & ~4095u), 8192, 7);
    flush_icache(input_tramp, input_tramp + 4);
    patch(FN_UPDATE_INPUT, (void *)hook_update_input);

    active = 1;
}
