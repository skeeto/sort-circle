#define _POSIX_C_SOURCE 2
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>  // getopt(3)

#include "font.h"

#define S     800           // video size
#define N     360           // number of points
#define R0    (N / 180.0f)  // circle inner radius
#define R1    (N / 90.0f)   // circle outer radius
#define PAD   (N / 64)      // message padding
#define WAIT  1             // pause in seconds between sorts
#define HZ    44100         // audio sample rate
#define FPS   60            // output framerate
#define MINHZ 20            // lowest tone
#define MAXHZ 1000          // highest tone
#define PI 3.141592653589793f

static uint32_t
pcg32(uint64_t *s)
{
    uint64_t m = 0x9b60933458e17d7d;
    uint64_t a = 0xd737232eeccdf7ed;
    *s = *s * m + a;
    int shift = 29 - (*s >> 61);
    return *s >> shift;
}

static void
emit_u32le(unsigned long v, FILE *f)
{
    fputc((v >>  0) & 0xff, f);
    fputc((v >>  8) & 0xff, f);
    fputc((v >> 16) & 0xff, f);
    fputc((v >> 24) & 0xff, f);
}

static void
emit_u32be(unsigned long v, FILE *f)
{
    fputc((v >> 24) & 0xff, f);
    fputc((v >> 16) & 0xff, f);
    fputc((v >>  8) & 0xff, f);
    fputc((v >>  0) & 0xff, f);
}

static void
emit_u16le(unsigned v, FILE *f)
{
    fputc((v >> 0) & 0xff, f);
    fputc((v >> 8) & 0xff, f);
}

static float
clamp(float x, float lower, float upper)
{
    if (x < lower)
        return lower;
    if (x > upper)
        return upper;
    return x;
}

static float
smoothstep(float lower, float upper, float x)
{
    x = clamp((x - lower) / (upper - lower), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/* Convert 24-bit color to pseudo sRGB. */
static void
rgb_split(unsigned long c, float *r, float *g, float *b)
{
    *r = sqrtf((c >> 16) / 255.0f);
    *g = sqrtf(((c >> 8) & 0xff) / 255.0f);
    *b = sqrtf((c & 0xff) / 255.0f);
}

/* Convert pseudo sRGB to 24-bit color. */
static unsigned long
rgb_join(float r, float g, float b)
{
    unsigned long ir = roundf(r * r * 255.0f);
    unsigned long ig = roundf(g * g * 255.0f);
    unsigned long ib = roundf(b * b * 255.0f);
    return (ir << 16) | (ig << 8) | ib;
}

static void
ppm_write(const unsigned char *buf, FILE *f)
{
    fprintf(f, "P6\n%d %d\n255\n", S, S);
    fwrite(buf, S * 3, S, f);
    fflush(f);
}

static void
ppm_set(unsigned char *buf, int x, int y, unsigned long color)
{
    buf[y * S * 3 + x * 3 + 0] = color >> 16;
    buf[y * S * 3 + x * 3 + 1] = color >>  8;
    buf[y * S * 3 + x * 3 + 2] = color >>  0;
}

static unsigned long
ppm_get(unsigned char *buf, int x, int y)
{
    unsigned long r = buf[y * S * 3 + x * 3 + 0];
    unsigned long g = buf[y * S * 3 + x * 3 + 1];
    unsigned long b = buf[y * S * 3 + x * 3 + 2];
    return (r << 16) | (g << 8) | b;
}

static void
ppm_circle(unsigned char *buf, float x, float y, unsigned long fgc)
{
    float fr, fg, fb;
    rgb_split(fgc, &fr, &fg, &fb);
    for (int py = floorf(y - R1 - 1); py <= ceilf(y + R1 + 1); py++) {
        float dy = py - y;
        for (int px = floorf(x - R1 - 1); px <= ceilf(x + R1 + 1); px++) {
            float dx = px - x;
            float d = sqrtf(dy * dy + dx * dx);
            float a = smoothstep(R1, R0, d);

            unsigned long bgc = ppm_get(buf, px, py);
            float br, bg, bb;
            rgb_split(bgc, &br, &bg, &bb);

            float r = a * fr + (1 - a) * br;
            float g = a * fg + (1 - a) * bg;
            float b = a * fb + (1 - a) * bb;
            ppm_set(buf, px, py, rgb_join(r, g, b));
        }
    }
}

static void
ppm_char(unsigned char *buf, int c, int x, int y, unsigned long fgc)
{
    float fr, fg, fb;
    rgb_split(fgc, &fr, &fg, &fb);
    for (int dy = 0; dy < FONT_H; dy++) {
        for (int dx = 0; dx < FONT_W; dx++) {
            float a = font_value(c, dx, dy);
            if (a > 0.0f) {
                unsigned long bgc = ppm_get(buf, x + dx, y + dy);
                float br, bg, bb;
                rgb_split(bgc, &br, &bg, &bb);

                float r = a * fr + (a - 1) * br;
                float g = a * fg + (a - 1) * bg;
                float b = a * fb + (a - 1) * bb;
                ppm_set(buf, x + dx, y + dy, rgb_join(r, g, b));
            }
        }
    }
}

static unsigned long
hue(int v)
{
    unsigned long h = v / (N / 6);
    unsigned long f = v % (N / 6);
    unsigned long t = 0xff * f / (N / 6);
    unsigned long q = 0xff - t;
    switch (h) {
        case 0:
            return 0xff0000UL | (t << 8);
        case 1:
            return (q << 16) | 0x00ff00UL;
        case 2:
            return 0x00ff00UL | t;
        case 3:
            return (q << 8) | 0x0000ffUL;
        case 4:
            return (t << 16) | 0x0000ffUL;
        case 5:
            return 0xff0000UL | q;
    }
    abort();
}

static int array[N];
static int swaps[N];
static const char *message;
static FILE *wav;

static void
frame(void)
{
    static unsigned char buf[S * S * 3];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < N; i++) {
        float delta = abs(i - array[i]) / (N / 2.0f);
        float x = -sinf(i * 2.0f * PI / N);
        float y = -cosf(i * 2.0f * PI / N);
        float r = S * 15.0f / 32.0f * (1.0f - delta);
        float px = r * x + S / 2.0f;
        float py = r * y + S / 2.0f;
        ppm_circle(buf, px, py, hue(array[i]));
    }
    if (message)
        for (int c = 0; message[c]; c++)
            ppm_char(buf, message[c], c * FONT_W + PAD, PAD, 0xffffffUL);
    ppm_write(buf, stdout);

    /* Output audio */
    if (wav) {
        int nsamples = HZ / FPS;
        static float samples[HZ / FPS];
        memset(samples, 0, sizeof(samples));

        /* How many voices to mix? */
        int voices = 0;
        for (int i = 0; i < N; i++)
            voices += swaps[i];

        /* Generate each voice */
        for (int i = 0; i < N; i++) {
            if (swaps[i]) {
                float hz = i * (MAXHZ - MINHZ) / (float)N + MINHZ;
                for (int j = 0; j < nsamples; j++) {
                    float u = 1.0f - j / (float)(nsamples - 1);
                    float parabola = 1.0f - (u * 2 - 1) * (u * 2 - 1);
                    float envelope = parabola * parabola * parabola;
                    float v = sinf(j * 2.0f * PI / HZ * hz) * envelope;
                    samples[j] += swaps[i] * v / voices;
                }
            }
        }

        /* Write out 16-bit samples */
        for (int i = 0; i < nsamples; i++) {
            int s = samples[i] * 0x7fff;
            emit_u16le(s, wav);
        }
        fflush(wav);
    }

    memset(swaps, 0, sizeof(swaps));
}

static void
swap(int a[N], int i, int j)
{
    int tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
    swaps[(a - array) + i]++;
    swaps[(a - array) + j]++;
}

static void
sort_bubble(int array[N])
{
    int c;
    do {
        c = 0;
        for (int i = 1; i < N; i++) {
            if (array[i - 1] > array[i]) {
                swap(array, i - 1, i);
                c = 1;
            }
        }
        frame();
    } while (c);
}

static void
sort_odd_even(int array[N])
{
    int c;
    do {
        c = 0;
        for(int i = 1; i < N - 1; i += 2) {
            if (array[i] > array[i + 1]) {
                swap(array, i, i + 1);
                c = 1;
            }
        }
        for (int i = 0; i < N - 1; i += 2) {
            if (array[i] > array[i + 1]) {
                swap(array, i, i + 1);
                c = 1;
            }
        }
        frame();
    } while (c);
}

static void
sort_insertion(int array[N])
{
    for (int i = 1; i < N; i++) {
        for (int j = i; j > 0 && array[j - 1] > array[j]; j--)
            swap(array, j, j - 1);
        frame();
    }
}

static void
sort_stoogesort(int array[N], int i, int j)
{
    static int c = 0;
    if (array[i] > array[j]) {
        swap(array, i, j);
        if (c++ % 32 == 0)
            frame();
    }
    if (j - i + 1 > 2) {
        int t = (j - i + 1) / 3;
        sort_stoogesort(array, i, j - t);
        sort_stoogesort(array, i + t, j);
        sort_stoogesort(array, i, j - t);
    }
}

static void
sort_quicksort(int *array, int n)
{
    if (n > 1) {
        int high = n;
        for (int i = 1; i < high;) {
            if (array[0] < array[i]) {
                swap(array, i, --high);
                if (n > 12)
                    frame();
            } else {
                i++;
            }
        }
        swap(array, 0, --high);
        frame();
        sort_quicksort(array, high + 1);
        sort_quicksort(array + high + 1, n - high - 1);
    }
}

static int
digit(int v, int b, int d)
{
    for (int i = 0; i < d; i++)
        v /= b;
    return v % b;
}

static void
sort_radix_lsd(int *array, int b)
{
    int c, total = 1;
    for (int d = 0; total; d++) {
        total = -1;
        /* Odd-even sort on the current digit */
        do {
            total++;
            c = 0;
            for(int i = 1; i < N - 1; i += 2) {
                if (digit(array[i], b, d) > digit(array[i + 1], b, d)) {
                    swap(array, i, i + 1);
                    c = 1;
                }
            }
            for (int i = 0; i < N - 1; i += 2) {
                if (digit(array[i], b, d) > digit(array[i + 1], b, d)) {
                    swap(array, i, i + 1);
                    c = 1;
                }
            }
            frame();
        } while (c);
    }
}

#define SHUFFLE_DRAW  (1u << 0)
#define SHUFFLE_FAST  (1u << 1)

static void
shuffle(int array[N], uint64_t *rng, unsigned flags)
{
    message = "Fisher-Yates";
    for (int i = N - 1; i > 0; i--) {
        uint32_t r = pcg32(rng) % (i + 1);
        swap(array, i, r);
        if (flags & SHUFFLE_DRAW) {
            if (!(flags & SHUFFLE_FAST) || i % 2)
            frame();
        }
    }
}

enum sort {
    SORT_NULL,
    SORT_BUBBLE,
    SORT_ODD_EVEN,
    SORT_INSERTION,
    SORT_STOOGESORT,
    SORT_QUICKSORT,
    SORT_RADIX_8_LSD,

    SORTS_TOTAL
};

static const char *const sort_names[] = {
    [SORT_ODD_EVEN] = "Odd-even",
    [SORT_BUBBLE] = "Bubble",
    [SORT_INSERTION] = "Insertion",
    [SORT_STOOGESORT] = "Stoogesort",
    [SORT_QUICKSORT] = "Quicksort",
    [SORT_RADIX_8_LSD] = "Radix LSD (base 8)",
};

static void
run_sort(enum sort type)
{
    if (type > 0 && type < SORTS_TOTAL)
        message = sort_names[type];
    else
        message = 0;
    switch (type) {
        case SORT_NULL:
            break;
        case SORT_ODD_EVEN:
            sort_odd_even(array);
            break;
        case SORT_BUBBLE:
            sort_bubble(array);
            break;
        case SORT_INSERTION:
            sort_insertion(array);
            break;
        case SORT_STOOGESORT:
            sort_stoogesort(array, 0, N - 1);
            break;
        case SORT_QUICKSORT:
            sort_quicksort(array, N);
            break;
        case SORT_RADIX_8_LSD:
            sort_radix_lsd(array, 8);
            break;
        case SORTS_TOTAL:
            break;
    }
    frame();
}

static FILE *
wav_init(const char *file)
{
    FILE *f = fopen(file, "wb");
    if (f) {
        emit_u32be(0x52494646UL, f); // "RIFF"
        emit_u32le(0xffffffffUL, f); // file length
        emit_u32be(0x57415645UL, f); // "WAVE"
        emit_u32be(0x666d7420UL, f); // "fmt "
        emit_u32le(16,           f); // struct size
        emit_u16le(1,            f); // PCM
        emit_u16le(1,            f); // mono
        emit_u32le(HZ,           f); // sample rate (i.e. 44.1 kHz)
        emit_u32le(HZ * 2,       f); // byte rate
        emit_u16le(2,            f); // block size
        emit_u16le(16,           f); // bits per sample
        emit_u32be(0x64617461UL, f); // "data"
        emit_u32le(0xffffffffUL, f); // byte length
    }
    return f;
}

static void
usage(const char *name, FILE *f)
{
    fprintf(f, "usage: %s [-a file] [-h] [-q] [s N] [-w N] [-x HEX] [-y]\n",
            name);
    fprintf(f, "  -a       name of audio output (WAV)\n");
    fprintf(f, "  -h       print this message\n");
    fprintf(f, "  -q       don't draw the shuffle\n");
    fprintf(f, "  -s N     animate sort number N (see below)\n");
    fprintf(f, "  -w N     insert a delay of N frames\n");
    fprintf(f, "  -x HEX   use HEX as a 64-bit seed for shuffling\n");
    fprintf(f, "  -y       slow down shuffle animation\n");
    fprintf(f, "\n");
    for (int i = 1; i < SORTS_TOTAL; i++)
        fprintf(f, "  %d: %s\n", i, sort_names[i]);
}

int
main(int argc, char **argv)
{
    for (int i = 0; i < N; i++)
        array[i] = i;

    int sorts = 0;
    unsigned flags = SHUFFLE_DRAW | SHUFFLE_FAST;
    uint64_t seed = 0;

    int option;
    while ((option = getopt(argc, argv, "a:hqs:w:x:y")) != -1) {
        int n;
        switch (option) {
            case 'a':
                wav = wav_init(optarg);
                if (!wav) {
                    fprintf(stderr, "%s: %s: %s\n",
                            argv[0], strerror(errno), optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                usage(argv[0], stdout);
                exit(EXIT_SUCCESS);
            case 'q':
                flags &= ~SHUFFLE_DRAW;
                break;
            case 's':
                sorts++;
                frame();
                shuffle(array, &seed, flags);
                run_sort(atoi(optarg));
                break;
            case 'w':
                n = atoi(optarg);
                for (int i = 0; i < n; i++)
                    frame();
                break;
            case 'x':
                seed = strtoull(optarg, 0, 16);
                break;
            case 'y':
                flags &= ~SHUFFLE_FAST;
                break;
            default:
                usage(argv[0], stderr);
                exit(EXIT_FAILURE);
        }
    }

    /* If no sorts selected, run all of them in order */
    if (!sorts) {
        for (int i = 1; i < SORTS_TOTAL; i++) {
            shuffle(array, &seed, flags);
            run_sort(i);
            for (int i = 0; i < WAIT * FPS; i++)
                frame();
        }
    }
}
