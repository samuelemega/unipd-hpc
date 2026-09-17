#!/usr/bin/env bash
# Initial PILOT requests only. Override after inspecting allocation and seff,
# e.g.: BASE_N=8192 RUNS=20 PROCESSES='1 2 4 8 16' CAMPAIGN=final \
#       sbatch --ntasks=16 --time=02:30:00 --mem=6G src/scripts/capri-benchmark.slurm
#SBATCH --job-name=fft-bench
#SBATCH --partition=allgroups
#SBATCH --ntasks=8
#SBATCH --cpus-per-task=1
#SBATCH --time=00:15:00
#SBATCH --mem=2G
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err
#SBATCH --mail-user=samuelemega@gmail.com
#SBATCH --mail-type=END,FAIL
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
source src/scripts/capri-env.sh
make clean
make CC="$CC" MPICC="$MPICC" CFLAGS="$CFLAGS"
export BASE_N=${BASE_N:-4096} RUNS=${RUNS:-3} PROCESSES=${PROCESSES:-1 2 4 8}
# Plain mpirun, as in the laboratory: Slurm already confines the job to
# its allocated cores, and per-core binding conflicts with that cgroup.
export LAUNCH='mpirun -np'
bash src/scripts/benchmark.sh "results/capri-${CAMPAIGN:-pilot}-${SLURM_JOB_ID}"
