/*
 * test_seq.c - correctness suite for the sequential 2D FFT.
 * Development-only: this file is not part of the course deliverable.
 *
 * The program under test is included directly, with its main renamed, so
 * that fft2d_seq.c needs no test scaffolding of its own.
 *
 * Build:  cc -O3 -std=c11 -o test_seq src/tests/test_seq.c -lm
 */
#pragma GCC diagnostic ignored "-Wunused-function"
#define main fft2d_seq_main
#include "../fft2d_seq.c"
#undef main

#include "reference.h"

#define TOLERANCE 1e-10

static int failures = 0;
static double worst_error = 0;

/* Records and prints one PASS/FAIL line. */
static void check(const char *name, int ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);

    if (!ok) {
        ++failures;
    }
}

/* Compares an error against the tolerance, remembering the worst one. */
static int within(double error) {
    if (error > worst_error) {
        worst_error = error;
    }

    return error <= TOLERANCE;
}

/*
 * Forward transform against the direct DFT, then inverse round trip back
 * to the original input, for one shape and seed.
 */
static void check_shape(size_t ny, size_t nx, uint64_t seed) {
    size_t count = ny * nx;
    double complex *data = xmalloc(count);
    double complex *scratch = xmalloc(count);
    double complex *original = xmalloc(count);
    double complex *expected = xmalloc(count);
    char name[64];

    generate(data, count, seed, 0);

    for (size_t k = 0; k < count; ++k) {
        original[k] = data[k];
    }

    reference_dft2d(data, expected, ny, nx);
    fft2d(data, scratch, ny, nx, FFT_FORWARD);
    snprintf(name, sizeof name, "forward vs direct DFT %zux%zu", ny, nx);
    check(name, within(relative_error(data, expected, count)));

    fft2d(data, scratch, ny, nx, FFT_INVERSE);
    snprintf(name, sizeof name, "inverse round trip %zux%zu", ny, nx);
    check(name, within(relative_error(data, original, count)));

    free(data);
    free(scratch);
    free(original);
    free(expected);
}

/*
 * The generator must be reproducible across machines (golden first element
 * for seed zero) and independent of where generation starts, which is what
 * lets each MPI rank generate its own rows.
 */
static void check_generator(void) {
    double complex whole[8];
    double complex part[4];

    generate(whole, 8, 0, 0);
    check(
        "golden first element for seed zero",
        fabs(creal(whole[0]) - 0.38331080821364261) < 1e-16 &&
            fabs(cimag(whole[0]) - -0.06847200295149003) < 1e-16);

    generate(part, 4, 0, 4);
    check(
        "generation is independent of the starting offset",
        part[0] == whole[4] && part[1] == whole[5] && part[2] == whole[6] &&
            part[3] == whole[7]);
}

int main(void) {
    check_generator();

    check_shape(1, 1, 7);
    check_shape(1, 8, 7);
    check_shape(8, 1, 7);
    check_shape(8, 8, 42);
    check_shape(4, 16, 42);
    check_shape(16, 4, 42);
    check_shape(32, 16, 1);

    if (failures > 0) {
        printf("%d checks failed\n", failures);

        return EXIT_FAILURE;
    }

    printf(
        "all sequential checks passed, worst relative error %.2g\n",
        worst_error);

    return EXIT_SUCCESS;
}
