#!/usr/bin/env bash
# Writes compile_flags.txt so clangd (e.g. in Zed) uses the project C flags
# and finds mpi.h through the MPI compiler wrapper.
set -euo pipefail
mpicc=${1:?usage: editor-config.sh MPICC [compile flags...]}
shift
dirs=$("$mpicc" --showme:incdirs 2>/dev/null || true)
[[ -n $dirs ]] || echo "warning: $mpicc did not report MPI include dirs" >&2
{
    printf '%s\n' -xc "$@"
    for dir in $dirs; do
        printf -- '-isystem\n%s\n' "$dir"
    done
} > compile_flags.txt
