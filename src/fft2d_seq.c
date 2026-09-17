/*
 * fft2d_seq.c - sequential 2D FFT benchmark.
 * Samuele Mega, Parallel Computing laboratory, University of Padova.
 *
 * Fills an NY x NX complex matrix with reproducible pseudo-random values,
 * runs one forward 2D FFT on it and prints a single CSV line:
 *
 *   implementation,shape,ny,nx,seed,processes,run,time_seconds,compute_fraction
 *
 * Build: cc -O3 -std=c11 -o fft2d_seq fft2d_seq.c -lm
 * Usage: ./fft2d_seq NY NX SEED [RUN]
 *
 * Both dimensions must be powers of two. Everything up to print_csv is kept
 * identical in fft2d_seq.c and fft2d_mpi.c, so each program is one file.
 */
#define _POSIX_C_SOURCE 200809L
#include <complex.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define FFT_FORWARD (-1)
#define FFT_INVERSE (+1)

static const double PI = 3.141592653589793238462643383279502884;

#define SPLITMIX_GAMMA UINT64_C(0x9e3779b97f4a7c15)

/*
 * One SplitMix64 draw. Unsigned arithmetic wraps modulo 2^64, so the
 * sequence depends only on the initial state, on any machine.
 */
static uint64_t splitmix64(uint64_t *state) {
    *state += SPLITMIX_GAMMA;

    uint64_t z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);

    return z ^ (z >> 31);
}

/*
 * Fills a[] with matrix elements first, first+1, ... in row-major order,
 * two draws per element (real then imaginary), each mapped to [-0.5, 0.5).
 * The generator state before element k is seed + 2k * SPLITMIX_GAMMA, so a
 * value depends only on the seed and on its position: the MPI ranks can
 * generate their own rows and still get the sequential program's matrix.
 */
static void generate(
    double complex *a,
    size_t count,
    uint64_t seed,
    size_t first) {
    uint64_t state = seed + 2 * (uint64_t)first * SPLITMIX_GAMMA;

    for (size_t k = 0; k < count; ++k) {
        double re = (double)(splitmix64(&state) >> 11) * 0x1.0p-53 - 0.5;
        double im = (double)(splitmix64(&state) >> 11) * 0x1.0p-53 - 0.5;

        a[k] = re + im * I;
    }
}

/*
 * In-place radix-2 decimation-in-frequency FFT of x[0..n-1], n a power of
 * two, following section 6.3 of the Parallel Algorithms course notes.
 * Each stage of width w applies the butterfly
 *     (a, b) -> (a + b, (a - b) * omega^j),  omega = exp(sign * 2*pi*i / w),
 * to the pairs at distance w/2. The stages leave the result in bit-reversed
 * order, so a final pass applies the bit-reversal permutation. sign is -1
 * for the forward transform and +1 for the inverse; the inverse is not
 * normalized here (fft2d divides by the element count once).
 */
static void fft1d(double complex *x, size_t n, int sign) {
    for (size_t w = n; w > 1; w /= 2) {
        for (size_t j = 0; j < w / 2; ++j) {
            double angle = (double)sign * 2.0 * PI * (double)j / (double)w;
            double complex omega_j = cexp(I * angle);

            for (size_t k = 0; k < n; k += w) {
                double complex a = x[k + j];
                double complex b = x[k + j + w / 2];

                x[k + j] = a + b;
                x[k + j + w / 2] = (a - b) * omega_j;
            }
        }
    }

    size_t r = 0;

    for (size_t k = 1; k < n; ++k) {
        size_t bit = n / 2;

        while (r & bit) {
            r ^= bit;
            bit /= 2;
        }

        r ^= bit;

        if (k < r) {
            double complex swap = x[k];
            x[k] = x[r];
            x[r] = swap;
        }
    }
}

/* True when v is a positive power of two; 1 is included. */
static int is_power_of_two(uint64_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

/* Parses a plain unsigned decimal integer; rejects any other input. */
static int parse_u64(const char *text, uint64_t *value) {
    if (*text < '0' || *text > '9') {
        return 0;
    }

    char *end;
    errno = 0;

    unsigned long long parsed = strtoull(text, &end, 10);

    if (errno != 0 || *end != '\0') {
        return 0;
    }

    *value = parsed;

    return 1;
}

/*
 * Command line: NY NX SEED [RUN], all unsigned decimals, RUN >= 1 with
 * default 1. The dimensions must be powers of two and small enough that a
 * full buffer fits in size_t bytes. On bad input prints the reason on
 * stderr and returns 0.
 */
static int parse_config(int argc, char **argv, uint64_t cfg[4]) {
    cfg[3] = 1;

    if ((argc != 4 && argc != 5) || !parse_u64(argv[1], &cfg[0]) ||
        !parse_u64(argv[2], &cfg[1]) || !parse_u64(argv[3], &cfg[2]) ||
        (argc == 5 && !parse_u64(argv[4], &cfg[3])) || cfg[3] == 0) {
        fprintf(
            stderr,
            "usage: %s NY NX SEED [RUN] (unsigned decimal, RUN >= 1)\n",
            argv[0]);

        return 0;
    }

    if (!is_power_of_two(cfg[0]) || !is_power_of_two(cfg[1])) {
        fprintf(stderr, "NY and NX must be powers of two\n");

        return 0;
    }

    if (cfg[0] > SIZE_MAX / cfg[1] ||
        cfg[0] * cfg[1] > SIZE_MAX / sizeof(double complex)) {
        fprintf(stderr, "matrix too large for this platform\n");

        return 0;
    }

    return 1;
}

/* One CSV measurement row, following the schema in the header comment. */
static void print_csv(
    const uint64_t cfg[4],
    const char *implementation,
    int processes,
    double time_seconds,
    double compute_fraction) {
    const char *shape = cfg[0] == cfg[1]  ? "square"
                        : cfg[0] < cfg[1] ? "wide"
                                          : "tall";

    printf(
        "%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%d,%" PRIu64
        ",%.17g,%.17g\n",
        implementation,
        shape,
        cfg[0],
        cfg[1],
        cfg[2],
        processes,
        cfg[3],
        time_seconds,
        compute_fraction);
}

/* Monotonic clock, in seconds. */
static double now(void) {
    struct timespec t;

    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        abort();
    }

    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* malloc that exits with a message when memory runs out. */
static double complex *xmalloc(size_t count) {
    double complex *a = malloc(count * sizeof *a);

    if (!a) {
        fprintf(stderr, "memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    return a;
}

/* Out-of-place transpose of the ny-by-nx matrix a: b[x][y] = a[y][x]. */
static void transpose(
    const double complex *a,
    double complex *b,
    size_t ny,
    size_t nx) {
    for (size_t y = 0; y < ny; ++y) {
        for (size_t x = 0; x < nx; ++x) {
            b[x * ny + y] = a[y * nx + x];
        }
    }
}

/*
 * In-place 2D FFT of the row-major ny x nx matrix a, with a scratch buffer
 * of the same size. The transform is separable: FFT of every row, then FFT
 * of every column, with no twiddle factors between the two axes (unlike
 * the factorization of a single 1D DFT, notes section 6.2). A transpose
 * makes the columns contiguous for the second phase and a second transpose
 * restores the original layout. The inverse divides by ny*nx here, once.
 * Returns the time spent inside fft1d, used for the compute fraction.
 */
static double fft2d(
    double complex *a,
    double complex *scratch,
    size_t ny,
    size_t nx,
    int sign) {
    double fft_seconds = 0.0;
    double phase_start = now();

    for (size_t y = 0; y < ny; ++y) {
        fft1d(a + y * nx, nx, sign);
    }

    fft_seconds += now() - phase_start;
    transpose(a, scratch, ny, nx);
    phase_start = now();

    for (size_t x = 0; x < nx; ++x) {
        fft1d(scratch + x * ny, ny, sign);
    }

    fft_seconds += now() - phase_start;
    transpose(scratch, a, nx, ny);

    if (sign == FFT_INVERSE) {
        for (size_t k = 0; k < ny * nx; ++k) {
            a[k] /= (double)(ny * nx);
        }
    }

    return fft_seconds;
}

/*
 * One measurement per invocation. Allocation and input generation stay
 * outside the timed interval; the kernel is the whole fft2d call.
 */
int main(int argc, char **argv) {
    uint64_t cfg[4];

    if (!parse_config(argc, argv, cfg)) {
        return EXIT_FAILURE;
    }

    size_t ny = (size_t)cfg[0];
    size_t nx = (size_t)cfg[1];
    double complex *a = xmalloc(ny * nx);
    double complex *scratch = xmalloc(ny * nx);

    generate(a, ny * nx, cfg[2], 0);

    double kernel_start = now();
    double fft_seconds = fft2d(a, scratch, ny, nx, FFT_FORWARD);
    double kernel_seconds = now() - kernel_start;
    double compute_fraction =
        kernel_seconds > 0 ? fft_seconds / kernel_seconds : 0.0;

    print_csv(cfg, "seq", 1, kernel_seconds, compute_fraction);
    free(a);
    free(scratch);

    return EXIT_SUCCESS;
}
