#!/usr/bin/env bash
set -euo pipefail
mode=${1:?usage: test-cli.sh seq|mpi}
read -r -a launcher <<< "${MPIEXEC:-mpiexec}"
read -r -a flags <<< "${MPIEXEC_FLAGS:-}"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# Runs the command given after the test name, expecting a failure.
# Checks that it prints no CSV row on stdout and explains the error on
# stderr. stdout is not required to be empty because mpirun itself may
# print a notice there when the ranks exit with a non-zero status.
reject() {
    local name=$1
    shift
    if "$@" >"$scratch/out" 2>"$scratch/err"; then
        echo "FAIL $name unexpectedly succeeded"
        exit 1
    fi
    if grep -qE '^(seq|mpi),' "$scratch/out" || [[ ! -s $scratch/err ]]; then
        echo "FAIL $name CSV/diagnostic contract"
        exit 1
    fi
    echo "PASS $name"
}

if [[ $mode == seq ]]; then
    reject 'missing arguments' ./fft2d_seq
    reject 'zero dimension' ./fft2d_seq 0 8 42
    reject 'non-power-of-two dimension' ./fft2d_seq 8 3 42
    reject 'negative seed' ./fft2d_seq 8 8 -1
    reject 'overflow seed' ./fft2d_seq 8 8 18446744073709551616
    reject 'trailing input' ./fft2d_seq 8x 8 42
    reject 'zero run index' ./fft2d_seq 8 8 42 0
    reject 'allocation overflow' ./fft2d_seq 9223372036854775808 1 42
elif [[ $mode == mpi ]]; then
    reject 'MPI invalid dimensions terminate collectively' "${launcher[@]}" ${flags[@]+"${flags[@]}"} -n 2 ./fft2d_mpi 0 8 42
    reject 'MPI incompatible dimensions terminate collectively' "${launcher[@]}" ${flags[@]+"${flags[@]}"} -n 2 ./fft2d_mpi 1 8 42
    reject 'MPI P=3 terminates collectively' "${launcher[@]}" ${flags[@]+"${flags[@]}"} -n 3 ./fft2d_mpi 8 8 42
    reject 'MPI invalid seed terminates collectively' "${launcher[@]}" ${flags[@]+"${flags[@]}"} -n 2 ./fft2d_mpi 8 8 nope
else
    echo "unknown test mode: $mode" >&2; exit 1
fi
