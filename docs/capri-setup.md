# Manual setup and execution on CAPRI

Guide prepared on September 10, 2026. Run all remote commands manually with
your own account. You do not need to share passwords or keys. Verify software
module names and final resource requests on the platform because public
examples may refer to older versions.

## 1. Configure access from the Mac

Use the login-node hostname and account provided by the course. If access
requires a VPN or a jump host, follow the instructions associated with your
account. You can add an alias to your `~/.ssh/config`, replacing the
placeholders:

```sshconfig
Host capri
    HostName capri.dei.unipd.it
    User YOUR_ACCOUNT
    ServerAliveInterval 60
```

If you already use an authorized key, specify it with `IdentityFile`. Do not
copy the private key to the cluster. Test the connection:

```sh
ssh capri
```

On the first login, change your password with `passwd` as instructed in class.
Connect only to the login node. Run computations through Slurm. These rules
and the acknowledgement of the platform in scientific work are defined by the
[CAPRI regulations](https://capri.dei.unipd.it/regulation/).

## 2. Inspect resources and the environment without running computations

On the login node:

```sh
sinfo
scontrol show partition allgroups
module avail
command -v spack
```

If Spack is available, `spack find` lists the installed software. Find a C11
compiler and a compatible MPI implementation. The MPI example in the
[CAPRI guide](https://capriuserguide.readthedocs.io/en/latest/SLURMExamples.html#mpi-job)
uses a legacy Intel environment; do not copy its name and version without
checking them.

The laboratory uses the system Open MPI installation, which is already in
`PATH` and does not require `module load`:

```sh
command -v gcc mpicc mpirun
gcc --version
mpicc --version
```

To identify the compiler invoked by the wrapper, use `mpicc -show` or, with
Open MPI, `mpicc --showme`. Choose a `CC` consistent with the compiler used by
`MPICC` so that the optimization options are comparable.

Launch MPI programs with `mpirun` inside the Slurm allocation, as shown in
class. Do not infer compatibility merely because `mpicc` compiles the source:
the validation job in Section 5 also checks that multiple ranks start
successfully.

## 3. Copy the project

From the Mac, in the repository's `project/` directory:

```sh
ssh capri 'mkdir -p ~/unipd-hpc'
rsync -av --exclude .venv --exclude dist \
  --exclude fft2d_seq --exclude fft2d_mpi \
  --exclude test_seq --exclude test_mpi \
  --exclude results --exclude 'report/generated' \
  --exclude 'report/*.pdf' --exclude compile_flags.txt \
  --exclude src/scripts/capri-env.sh \
  ./ capri:~/unipd-hpc/
```

Note that `.git` lives at the repository root and is not copied, so metadata
collected on the cluster records the revision as `unknown`. Record the local
revision that you synchronized, preferably after committing it before the
final campaign. Do not transfer macOS executables; they will be rebuilt on
CAPRI. Do not start two jobs that compile concurrently in the same directory.

## 4. Save the job environment

On the login node:

```sh
cd ~/unipd-hpc
cp src/scripts/capri-env.example.sh src/scripts/capri-env.sh
nano src/scripts/capri-env.sh
```

Add the `module load` or `spack load` commands verified in Section 2,
including any initialization required by the batch shell. Configure:

```sh
export CC=gcc
export MPICC=mpicc
export CFLAGS='-O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion'
```

These are starting values. Adapt the compiler, wrapper and any MPI flags to
the selected environment. The file is excluded from Git and must not contain
credentials. The Slurm scripts launch MPI programs with `mpirun` and no
binding options: the Slurm allocation already confines the job to its assigned
cores, and explicit per-core binding conflicts with that confinement.

## 5. Build and validate through Slurm

`src/scripts/capri-check.slurm` initially requests 4 tasks, 1 CPU per task,
1 GiB per node and 5 minutes, then builds and tests on the allocated nodes.
Confirm that these requests are compatible with your account, then submit:

```sh
sbatch src/scripts/capri-check.slurm
squeue -u "$USER"
```

Record the returned JOBID. After completion:

```sh
cat slurm-JOBID.out
cat slurm-JOBID.err
seff JOBID
```

The C test suites must pass with 1, 2 and 4 ranks, as must the invalid-input
checks. The latter intentionally produce nonzero exit codes in some steps, but
the overall job must still finish successfully. The last two commands print
sequential and MPI CSV rows. If the launcher reports errors, review the
environment in `capri-env.sh` and repeat the validation job.

The `allgroups` partition and explicit task, partition, time and memory
requests, followed by `seff` inspection, follow the
[CAPRI Slurm guide](https://capriuserguide.readthedocs.io/en/latest/UsingSLURM.html).

## 6. Run the first pilot

The initial pilot uses `N=4096`, seed 42, 3 invocations per point and
`P=1,2,4,8`. The matrices are therefore 4096x4096, 2048x8192 and 8192x2048.
This is an operational starting point, not the final campaign choice: on the
Mac, small matrices up to about 1024 mostly measure communication, while a
single CAPRI core will probably be slower rather than faster.

```sh
sbatch src/scripts/capri-benchmark.slurm
```

The script rebuilds on the allocated node and creates
`results/capri-pilot-JOBID/`. Each invocation measures one transform.
It saves `raw.csv` and `metadata.txt` with the configuration, launcher, Git
revision and software versions. If an error occurs, partial data remain
available but the Slurm log records the failure; do not use them as the final
campaign.

For a later pilot, choose a new size and a process list justified by the first
results. For example, **only if authorized and useful**, use 16 processes and
`N=8192`:

```sh
export BASE_N=8192 RUNS=3 PROCESSES='1 2 4 8 16'
sbatch --ntasks=16 --time=00:20:00 --mem=4G src/scripts/capri-benchmark.slurm
```

The displayed options are pilot examples and must be adapted using
measurements. The scripts inherit exported variables. Restore the defaults
with:

```sh
unset BASE_N RUNS PROCESSES CAMPAIGN
```

## 7. Choose N, process counts, memory and time

Retrieve a pilot on the Mac as described in Section 9, then run `make analyze`
on that path. Inspect the minimum, median and maximum time for every point. If
times become too short or their spread is high, try a larger N before fixing
the campaign. The maximum P is always a power of two no greater than N/2
because it must be valid for all three shapes.

With `M=16*N*N` bytes per matrix, the main allocations are:

- sequential: `2*M`;
- MPI, per rank: `4*M/P`, because each rank directly generates its own rows
  and no rank allocates the complete matrix.

If all ranks share one node, the aggregate MPI-array peak is `4*M`. It is
approximately 1 GiB for N=4096, 4 GiB for N=8192 and 16 GiB for N=16384. Add
MPI, the runtime and a prudent margin, then check the measurements with
`seff`. With multiple nodes, account for how many ranks reside on each node.
`--mem` is a **per-node** request, as specified in the
[sbatch documentation](https://slurm.schedmd.com/sbatch.html#OPT_mem).

Batch time includes compilation, generation and startup of every process; do
not estimate it using only the CSV kernel time. With K MPI process counts, the
number of invocations is `3 * RUNS * (1 + K)`. Use the pilots' total time plus
a margin for the final request. There is no a priori need for 128 or 256
processes; explore 8, 16 and beyond only if the resources and initial
measurements justify them. Preserve saturation points and slowdowns.

Before the final campaign, record N, the P list, memory, time and the reason
for the choice in a file in the results directory. Keep the launcher and its
options fixed across points. The metadata record the launcher, Git revision
and Slurm job; a separate rank-placement campaign is not required.

## 8. Run the final campaign

After choosing the values, export them and submit the job. In the following
example, replace `8192`, `1 2 4 8 16`, `16`, `00:30:00` and `4G` with the
values determined by your pilot:

```sh
export BASE_N=8192 RUNS=20 PROCESSES='1 2 4 8 16' SEED=42 CAMPAIGN=final
sbatch --job-name=fft-final --ntasks=16 --time=00:30:00 --mem=4G \
  src/scripts/capri-benchmark.slurm
```

The directory will be `results/capri-final-JOBID/`. The program runs twenty
sequential baselines for each shape and twenty MPI invocations for every P,
including P=1. Do not change the list after seeing only the favorable points.
Do not combine results from different N values, seeds, revisions or flags in
one analysis.

## 9. Retrieve and analyze on the Mac

After the job, on the login node:

```sh
seff JOBID
```

From the Mac, in the repository's `project/` directory:

```sh
rsync -av capri:~/unipd-hpc/results/capri-final-JOBID/ results/capri-final-JOBID/
scp capri:~/unipd-hpc/slurm-JOBID.out results/capri-final-JOBID/
scp capri:~/unipd-hpc/slurm-JOBID.err results/capri-final-JOBID/
ssh capri 'seff JOBID' > results/capri-final-JOBID/seff.txt
uv sync --locked
make analyze RESULTS=results/capri-final-JOBID
```

Replace JOBID inside the quotes as well. For a pilot, use the
`capri-pilot-JOBID` path instead. Python and LaTeX do not need to be installed
on the cluster.

The analysis produces `summary.csv` and the three `speedup-square`,
`speedup-wide` and `speedup-tall` plots in PDF and PNG formats. Speedup is the
ratio of the sequential and MPI medians; the tables include minimum, maximum
and compute fraction. The latter measures the fraction spent in local FFTs;
its complement includes packing, unpacking, collectives and waiting, not only
network transfers.

## 10. Build the report and archive

On the Mac:

```sh
make report RESULTS=results/capri-final-JOBID CAPRI_FINAL=1
```

Open `report/report.pdf`. The tables and plots are generated from the CSV
files. Complete the discussion with observations from the pilot, the selected
environment, stability and the limitations of the three curves. Do not
attribute aspect-ratio differences that the experiment did not isolate. The
course requirements from slide 11 of the laboratory introduction are 3-8
single-column pages, a font of at least 10 pt, English and PDF. The source uses
IEEEtran in single-column mode and includes the required CAPRI
acknowledgement.

```sh
make dist RESULTS=results/capri-final-JOBID
```

The `dist/parallel-2d-fft.zip` archive contains exactly the submission:
`fft2d_seq.c`, `fft2d_mpi.c` and `report.pdf`, rebuilt from the selected
campaign and labeled as final. Creation is rejected unless the directory is a
`results/capri-final-JOBID` campaign. Follow any newer Moodle submission
instructions.
