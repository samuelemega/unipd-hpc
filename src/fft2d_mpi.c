/*
 * fft2d_mpi.c - MPI 2D FFT benchmark.
 * Samuele Mega, Parallel Computing laboratory, University of Padova.
 *
 * Same benchmark as fft2d_seq.c, parallelized with row slabs: with P ranks
 * (a power of two, P <= min(NY, NX)) each rank keeps NY/P consecutive rows,
 * so every 1D FFT is local and the matrix changes axis with a distributed
 * transpose, one MPI_Alltoall per transpose. MPI lets any two ranks talk
 * directly, so the processes form a complete graph K_P; P = 2^d also lets
 * the same exchange run on the hypercube H_d. Both views are analyzed in
 * the report. Rank 0 prints a single CSV line:
 *
 *   implementation,shape,ny,nx,seed,processes,run,time_seconds,compute_fraction
 *
 * Build: mpicc -O3 -std=c11 -o fft2d_mpi fft2d_mpi.c -lm
 * Usage: mpiexec -n P ./fft2d_mpi NY NX SEED [RUN]
 *
 * Everything up to print_csv is kept identical to fft2d_seq.c.
 */
#include <complex.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <mpi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

/*
 * malloc that aborts every rank on failure, so nobody is left waiting
 * inside a collective.
 */
static double complex *xmalloc_mpi(size_t count) {
    double complex *a = malloc(count * sizeof *a);

    if (!a) {
        fprintf(stderr, "memory allocation failed\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    return a;
}

/*
 * P must be a positive power of two with P <= min(NY, NX), so row slabs
 * and exchange blocks divide evenly; the per-pair block count must also
 * fit in the int that MPI_Alltoall takes.
 */
static int check_process_count(const uint64_t cfg[4], int processes, int rank) {
    uint64_t p = (uint64_t)processes;

    if (!is_power_of_two(p) || p > cfg[0] || p > cfg[1] ||
        (cfg[0] / p) * (cfg[1] / p) > INT_MAX) {
        if (rank == 0) {
            fprintf(
                stderr,
                "the process count must be a power of two, at most "
                "min(NY, NX), with (NY/P)*(NX/P) <= INT_MAX\n");
        }

        return 0;
    }

    return 1;
}

/*
 * Distributed transpose of an r x c matrix stored as row slabs: each rank
 * enters with r/p rows of length c and leaves with c/p rows of the
 * transposed matrix. Every pair of ranks exchanges one (r/p) x (c/p)
 * block: the columns for each peer are packed into a contiguous block,
 * MPI_Alltoall trades the equally sized blocks, and every received block
 * is unpacked at transposed indices. All four buffers hold r*c/p elements.
 */
static void distributed_transpose(
    const double complex *slab,
    double complex *transposed,
    size_t r,
    size_t c,
    double complex *send_buffer,
    double complex *receive_buffer,
    MPI_Comm comm) {
    int processes;
    MPI_Comm_size(comm, &processes);

    size_t p = (size_t)processes;
    size_t slab_rows = r / p;
    size_t block_cols = c / p;
    size_t block = slab_rows * block_cols;

    /* Pack: peer i receives the block of columns it will own as rows. */
    for (size_t peer = 0; peer < p; ++peer) {
        for (size_t y = 0; y < slab_rows; ++y) {
            for (size_t x = 0; x < block_cols; ++x) {
                send_buffer[peer * block + y * block_cols + x] =
                    slab[y * c + peer * block_cols + x];
            }
        }
    }

    MPI_Alltoall(
        send_buffer,
        (int)block,
        MPI_C_DOUBLE_COMPLEX,
        receive_buffer,
        (int)block,
        MPI_C_DOUBLE_COMPLEX,
        comm);

    /* Unpack every received block at transposed indices. */
    for (size_t peer = 0; peer < p; ++peer) {
        for (size_t y = 0; y < slab_rows; ++y) {
            for (size_t x = 0; x < block_cols; ++x) {
                transposed[x * r + peer * slab_rows + y] =
                    receive_buffer[peer * block + y * block_cols + x];
            }
        }
    }
}

/*
 * 2D FFT of the distributed ny x nx matrix: FFT of the local rows, a
 * distributed transpose, FFT of the original columns (now local rows), and
 * a second transpose that restores the initial slabs. The two transposes
 * are the only communication. The inverse divides by ny*nx here, once.
 * Returns the local time spent inside fft1d; the caller times the whole
 * call and reduces the timings across the ranks.
 */
static double fft2d_mpi(
    double complex *slab,
    double complex *transposed,
    double complex *send_buffer,
    double complex *receive_buffer,
    size_t ny,
    size_t nx,
    int sign,
    MPI_Comm comm) {
    int processes;
    MPI_Comm_size(comm, &processes);

    size_t p = (size_t)processes;
    double fft_seconds = 0.0;
    double phase_start = MPI_Wtime();

    for (size_t y = 0; y < ny / p; ++y) {
        fft1d(slab + y * nx, nx, sign);
    }

    fft_seconds += MPI_Wtime() - phase_start;
    distributed_transpose(
        slab,
        transposed,
        ny,
        nx,
        send_buffer,
        receive_buffer,
        comm);
    phase_start = MPI_Wtime();

    for (size_t x = 0; x < nx / p; ++x) {
        fft1d(transposed + x * ny, ny, sign);
    }

    fft_seconds += MPI_Wtime() - phase_start;
    distributed_transpose(
        transposed,
        slab,
        nx,
        ny,
        send_buffer,
        receive_buffer,
        comm);

    if (sign == FFT_INVERSE) {
        for (size_t k = 0; k < ny * nx / p; ++k) {
            slab[k] /= (double)(ny * nx);
        }
    }

    return fft_seconds;
}

/*
 * One measurement per invocation. Rank 0 parses the command line and
 * broadcasts it (ny = 0 marks it invalid and every rank exits). Each rank
 * generates its own rows, then a barrier starts the timed kernel. The
 * reported time is the maximum over the ranks and the compute fraction is
 * sum(local FFT time) / sum(local kernel time), both reduced after the
 * timers stop. Only rank 0 prints.
 */
int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    MPI_Comm comm = MPI_COMM_WORLD;
    int rank;
    int processes;

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &processes);

    uint64_t cfg[4] = {0, 0, 0, 0};

    if (rank == 0 && !parse_config(argc, argv, cfg)) {
        cfg[0] = 0;
    }

    MPI_Bcast(cfg, 4, MPI_UINT64_T, 0, comm);

    if (cfg[0] == 0 || !check_process_count(cfg, processes, rank)) {
        MPI_Finalize();

        return EXIT_FAILURE;
    }

    size_t ny = (size_t)cfg[0];
    size_t nx = (size_t)cfg[1];
    size_t local_count = ny * nx / (size_t)processes;
    double complex *slab = xmalloc_mpi(local_count);
    double complex *transposed = xmalloc_mpi(local_count);
    double complex *send_buffer = xmalloc_mpi(local_count);
    double complex *receive_buffer = xmalloc_mpi(local_count);

    generate(slab, local_count, cfg[2], (size_t)rank * local_count);

    MPI_Barrier(comm);

    double kernel_start = MPI_Wtime();
    double fft_seconds = fft2d_mpi(
        slab,
        transposed,
        send_buffer,
        receive_buffer,
        ny,
        nx,
        FFT_FORWARD,
        comm);
    double kernel_seconds = MPI_Wtime() - kernel_start;

    double max_kernel_seconds = 0.0;
    double local_times[2] = {fft_seconds, kernel_seconds};
    double total_times[2] = {0.0, 0.0};

    MPI_Reduce(
        &kernel_seconds,
        &max_kernel_seconds,
        1,
        MPI_DOUBLE,
        MPI_MAX,
        0,
        comm);
    MPI_Reduce(local_times, total_times, 2, MPI_DOUBLE, MPI_SUM, 0, comm);

    if (rank == 0) {
        double compute_fraction =
            total_times[1] > 0 ? total_times[0] / total_times[1] : 0.0;

        print_csv(cfg, "mpi", processes, max_kernel_seconds, compute_fraction);
    }

    free(slab);
    free(transposed);
    free(send_buffer);
    free(receive_buffer);
    MPI_Finalize();

    return EXIT_SUCCESS;
}
