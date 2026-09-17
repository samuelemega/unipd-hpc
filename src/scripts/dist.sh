#!/usr/bin/env bash
# Packages the course deliverable: the two C programs and the report PDF,
# rebuilt from a final CAPRI campaign (results/capri-final-JOBID).
set -euo pipefail
results=${1:?usage: dist.sh RESULTS_DIRECTORY}
[[ -f $results/raw.csv ]] || { echo "no raw.csv in $results" >&2; exit 1; }
[[ $results == *capri-final* ]] || {
    echo "dist expects a final CAPRI campaign (results/capri-final-JOBID)" >&2
    exit 1
}
make report RESULTS="$results" CAPRI_FINAL=1
rm -rf dist/parallel-2d-fft dist/parallel-2d-fft.zip
mkdir -p dist/parallel-2d-fft
cp src/fft2d_seq.c src/fft2d_mpi.c report/report.pdf dist/parallel-2d-fft/
cat > dist/parallel-2d-fft/README.txt <<'EOF'
Parallel 2D FFT - Samuele Mega
Parallel Computing laboratory, University of Padova

Build:
  cc    -O3 -std=c11 -o fft2d_seq fft2d_seq.c -lm
  mpicc -O3 -std=c11 -o fft2d_mpi fft2d_mpi.c -lm

Run (NY and NX must be powers of two; the optional RUN numbers repeated
measurements in the CSV output):
  ./fft2d_seq NY NX SEED [RUN]
  mpiexec -n P ./fft2d_mpi NY NX SEED [RUN]

Each run measures one forward transform and prints one CSV line:
  implementation,shape,ny,nx,seed,processes,run,time_seconds,compute_fraction

report.pdf describes the algorithm, the topology analysis and the
measurements.
EOF
(cd dist && zip -qr parallel-2d-fft.zip parallel-2d-fft)
echo "Created dist/parallel-2d-fft.zip"
