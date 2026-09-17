#!/usr/bin/env bash
#SBATCH --job-name=fft-check
#SBATCH --partition=allgroups
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=1
#SBATCH --time=00:05:00
#SBATCH --mem=1G
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err
#SBATCH --mail-user=samuelemega@gmail.com
#SBATCH --mail-type=END,FAIL
# Builds and tests inside the allocation; MPI programs are launched with
# mpirun and the default Open MPI runtime, as done in the laboratory.
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
source src/scripts/capri-env.sh
make clean
make CC="$CC" MPICC="$MPICC" CFLAGS="$CFLAGS"
make test CC="$CC" CFLAGS="$CFLAGS"
make test-mpi MPICC="$MPICC" CFLAGS="$CFLAGS" MPIEXEC=mpirun
# Benchmark sanity checks; these are not campaign samples.
./fft2d_seq 32 64 42
mpirun -np 2 ./fft2d_mpi 32 64 42
