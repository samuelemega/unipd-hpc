# Parallel 2D FFT

A standalone C11 complex 2D FFT for the Parallel Computing Laboratory, in a
sequential and an MPI version that share the same iterative radix-2 DIF kernel
from the course notes (Bilardi, *Parallel Algorithms*, ch. 6). Both dimensions
must independently be powers of two, including 1; forward and inverse
transforms of square and rectangular matrices are supported, and every result
is returned in the original row-major layout. No FFT library, OpenMP, GPU code
or fluid solver is used.

## Layout and deliverable

Each program is **one self-contained C file**, written so the course
instructor can read and build it in isolation:

- [src/fft2d_seq.c](src/fft2d_seq.c) — sequential benchmark
  (`cc -O3 -std=c11 -o fft2d_seq fft2d_seq.c -lm`)
- [src/fft2d_mpi.c](src/fft2d_mpi.c) — MPI benchmark, row slabs plus
  distributed transpose (`mpicc -O3 -std=c11 -o fft2d_mpi fft2d_mpi.c -lm`)

The two files share an identical, clearly marked block (generator, 1D kernel,
parsing, CSV output). The course deliverable is only `fft2d_seq.c`,
`fft2d_mpi.c` and `report/report.pdf` (see `make dist`). Everything else in
this repository — tests, scripts, campaign results, docs — is development
support and is not submitted.

## Build and test

Requirements: a C11 compiler, Make and an MPI installation providing `mpicc`,
`mpiexec` and `MPI_C_DOUBLE_COMPLEX`. Python and LaTeX are not needed for the
C tests.

```sh
make
make test
make test-mpi
```

The test suites (`src/tests/`) include the program sources directly and compare
them against an independent direct DFT with criterion
`||actual-reference||_2 / max(||reference||_2, 1) <= 1e-10`, covering both
directions, round trips, all supported shape families and invalid inputs.
`make test-mpi` runs with 1, 2 and 4 processes by default:

```sh
make test-mpi MPIEXEC=mpiexec MPIEXEC_FLAGS='--oversubscribe' MPI_TEST_PROCESSES='1 2 4 8'
```

`--oversubscribe` is an Open MPI option for local correctness checks only; do
not use oversubscribed measurements for the CAPRI campaign. Keep the same
optimization flags for both versions and avoid `-ffast-math`. Run
`make format` (clang-format 15+) to keep the layout; `make editor-config`
regenerates `compile_flags.txt` for clangd/Zed after changing MPI or flags.

## Running benchmarks

```sh
./fft2d_seq 256 512 42
mpiexec -n 4 ./fft2d_mpi 256 512 42 1
```

Arguments are `NY NX SEED [RUN]`, unsigned decimals with `RUN >= 1`
defaulting to 1. MPI requires a power-of-two `P <= min(NY, NX)`. Each
successful invocation measures exactly one forward transform and
writes one CSV row, without a header:

```text
implementation,shape,ny,nx,seed,processes,run,time_seconds,compute_fraction
```

Diagnostics go to stderr. Allocation and input generation are outside the
timing; both row-FFT phases, twiddle evaluation, packing, unpacking and both
transposes are inside. Each MPI rank generates its own row slab (the
SplitMix64 generator is random-access, so element `k` depends only on the
seed and `k`), synchronizes on a barrier, and rank zero reports the maximum
kernel time across ranks. The compute fraction is
`sum(local FFT time) / sum(kernel time)`; its complement includes memory
movement and waiting, not just network time.

## Campaign, analysis, report

```sh
make bench        # local pilot into results/local-smoke (BASE_N=256, RUNS=3)
make analyze      # summary.csv + speedup plots
make report       # report/report.pdf via latexmk
```

Select another campaign or size with environment variables, e.g.
`BASE_N=512 RUNS=10 make bench RESULTS=results/local-512`; the MPI launcher
is `LAUNCH` (default `mpiexec -n`, e.g. `LAUNCH='mpiexec --oversubscribe -n'`
locally or `LAUNCH='mpirun -np'` on CAPRI). Every campaign
measures one sequential baseline and MPI series for the three shapes `N x N`,
`N/2 x 2N`, `2N x N/2`; existing campaign directories are never overwritten.
Analysis uses project-local dependencies fixed in `uv.lock`
(`uv sync --locked`); `src/scripts/analyze.py` writes `summary.csv`, the speedup
plots, a two-panel overview figure and the LaTeX fragment included by the
report. `src/scripts/compare.py OUTPUT DIR:LABEL...` draws the same two panels
for the square shape of several campaigns at once (used for the
size-comparison figure in the conclusions).

## CAPRI and delivery

Follow the step-by-step [manual CAPRI guide](docs/capri-setup.md) (Italian)
for SSH setup, Slurm checks, pilot selection and the final campaign. After the
final CAPRI campaign (20 runs per point):

```sh
make report RESULTS=results/capri-final-JOBID
make dist RESULTS=results/capri-final-JOBID
```

`make dist` expects a `results/capri-final-JOBID` campaign, rebuilds the
report labeled as final (`CAPRI_FINAL=1`) and writes
`dist/parallel-2d-fft.zip` containing exactly the deliverable:
`fft2d_seq.c`, `fft2d_mpi.c` and `report.pdf`. Course-platform submission
instructions take precedence over this archive format.

## Documentation

- `report/report.tex`: the course report — algorithm, topology analysis
  (complete graph and hypercube mappings, following the course notes),
  measurement method and results.
- Course material (Bilardi, *Parallel Algorithms* and *Parallel
  Architectures*, and the laboratory slides) is cited in the report but not
  redistributed here; it is available to enrolled students on the course
  platform.
