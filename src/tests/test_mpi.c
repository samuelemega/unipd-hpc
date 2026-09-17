/*
 * test_mpi.c - correctness suite for the MPI 2D FFT.
 * Development-only: this file is not part of the course deliverable.
 *
 * The program under test is included directly, with its main renamed, so
 * that fft2d_mpi.c needs no test scaffolding of its own. Run the suite with
 * every process count of interest, e.g. mpiexec -n 4 ./test_mpi.
 *
 * Build:  mpicc -O3 -std=c11 -o test_mpi src/tests/test_mpi.c -lm
 */
#pragma GCC diagnostic ignored "-Wunused-function"
#define main fft2d_mpi_main
#include "../fft2d_mpi.c"
#undef main

#include "reference.h"

#define TOLERANCE 1e-10

static int failures = 0;
static double worst_error = 0;

/* Records and prints one PASS/FAIL line on rank zero. */
static void check(int rank, const char *name, int ok) {
    if (rank == 0) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    }

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
 * Forward distributed transform gathered and compared against the direct
 * DFT, then inverse round trip compared locally on every rank, for one
 * shape and seed. Shapes incompatible with the process count are skipped.
 */
static void check_shape(int rank, int processes, size_t ny, size_t nx) {
    if ((size_t)processes > ny || (size_t)processes > nx) {
        return;
    }

    const uint64_t seed = 42;
    size_t count = ny * nx;
    size_t local_count = count / (size_t)processes;
    double complex *slab = xmalloc_mpi(local_count);
    double complex *transposed = xmalloc_mpi(local_count);
    double complex *send_buffer = xmalloc_mpi(local_count);
    double complex *receive_buffer = xmalloc_mpi(local_count);
    double complex *original = xmalloc_mpi(local_count);
    double complex *gathered = rank == 0 ? xmalloc_mpi(count) : NULL;
    char name[64];

    generate(slab, local_count, seed, (size_t)rank * local_count);

    for (size_t k = 0; k < local_count; ++k) {
        original[k] = slab[k];
    }

    fft2d_mpi(
        slab,
        transposed,
        send_buffer,
        receive_buffer,
        ny,
        nx,
        FFT_FORWARD,
        MPI_COMM_WORLD);
    MPI_Gather(
        slab,
        (int)local_count,
        MPI_C_DOUBLE_COMPLEX,
        gathered,
        (int)local_count,
        MPI_C_DOUBLE_COMPLEX,
        0,
        MPI_COMM_WORLD);

    int forward_ok = 1;

    if (rank == 0) {
        double complex *input = xmalloc_mpi(count);
        double complex *expected = xmalloc_mpi(count);

        generate(input, count, seed, 0);
        reference_dft2d(input, expected, ny, nx);
        forward_ok = within(relative_error(gathered, expected, count));
        free(input);
        free(expected);
    }

    snprintf(
        name,
        sizeof name,
        "forward vs direct DFT %zux%zu P=%d",
        ny,
        nx,
        processes);
    check(rank, name, forward_ok);

    fft2d_mpi(
        slab,
        transposed,
        send_buffer,
        receive_buffer,
        ny,
        nx,
        FFT_INVERSE,
        MPI_COMM_WORLD);

    double local_error = relative_error(slab, original, local_count);
    double max_error = 0.0;

    MPI_Reduce(
        &local_error,
        &max_error,
        1,
        MPI_DOUBLE,
        MPI_MAX,
        0,
        MPI_COMM_WORLD);
    snprintf(
        name,
        sizeof name,
        "inverse round trip %zux%zu P=%d",
        ny,
        nx,
        processes);
    check(rank, name, rank != 0 || within(max_error));

    free(slab);
    free(transposed);
    free(send_buffer);
    free(receive_buffer);
    free(original);
    free(gathered);
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank;
    int processes;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processes);

    check_shape(rank, processes, 1, 8);
    check_shape(rank, processes, 8, 8);
    check_shape(rank, processes, 4, 16);
    check_shape(rank, processes, 16, 4);
    check_shape(rank, processes, 32, 32);

    /* Every rank exits with the outcome established on rank zero. */
    MPI_Bcast(&failures, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        if (failures > 0) {
            printf("%d checks failed\n", failures);
        } else {
            printf(
                "all MPI checks passed with %d processes, "
                "worst relative error %.2g\n",
                processes,
                worst_error);
        }
    }

    MPI_Finalize();

    return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
