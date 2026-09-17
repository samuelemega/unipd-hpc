# Copy to src/scripts/capri-env.sh and replace the environment setup below with
# the module or spack combination you verified on the login node.
# module load <verified-compiler-module> <matching-mpi-module>
# or: spack load <verified-mpi-spec>

export CC=gcc
export MPICC=mpicc
export CFLAGS='-O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion'
