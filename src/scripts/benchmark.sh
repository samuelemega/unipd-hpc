#!/usr/bin/env bash
# Runs one measurement campaign into a new directory: for each shape
# (square, wide, tall) it measures RUNS sequential baselines and RUNS MPI
# invocations per process count. Every program invocation measures exactly
# one transform and appends one CSV row to raw.csv.
#
# Configuration comes from the environment:
#   BASE_N=256  RUNS=3  SEED=42  PROCESSES='1 2 4'  LAUNCH='mpiexec -n'
# On CAPRI use LAUNCH='mpirun -np' (see the Slurm scripts).
set -euo pipefail
out=${1:?usage: benchmark.sh OUTPUT_DIRECTORY}
base=${BASE_N:-256}
runs=${RUNS:-3}
seed=${SEED:-42}
processes=${PROCESSES:-1 2 4}
launch=${LAUNCH:-mpiexec -n}

mkdir -p "$(dirname "$out")"
mkdir "$out" # deliberately refuses to reuse an existing campaign

{
    echo "base_n=$base seed=$seed runs=$runs processes='$processes'"
    echo "launch='$launch' date=$(date -u +%FT%TZ)"
    echo "git=$(git rev-parse HEAD 2>/dev/null || echo unknown)" \
         "dirty=$([ -n "$(git status --porcelain 2>/dev/null)" ] && echo yes || echo no)"
    echo "host=$(uname -n) slurm_job=${SLURM_JOB_ID:-none}"
    "${MPICC:-mpicc}" --version 2>/dev/null | head -1
} > "$out/metadata.txt"

echo 'implementation,shape,ny,nx,seed,processes,run,time_seconds,compute_fraction' > "$out/raw.csv"
for shape in "$base $base" "$((base / 2)) $((base * 2))" "$((base * 2)) $((base / 2))"; do
    read -r ny nx <<< "$shape"
    for run in $(seq 1 "$runs"); do
        echo "seq ${ny}x${nx} run $run/$runs" >&2
        ./fft2d_seq "$ny" "$nx" "$seed" "$run" >> "$out/raw.csv"
    done
    for p in $processes; do
        for run in $(seq 1 "$runs"); do
            echo "mpi ${ny}x${nx} P=$p run $run/$runs" >&2
            $launch "$p" ./fft2d_mpi "$ny" "$nx" "$seed" "$run" >> "$out/raw.csv"
        done
    done
done
echo "Campaign saved in $out" >&2
