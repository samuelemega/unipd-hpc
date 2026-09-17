CC ?= cc
MPICC ?= mpicc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDLIBS ?= -lm
MPIEXEC ?= mpiexec
MPIEXEC_FLAGS ?=
MPI_TEST_PROCESSES ?= 1 2 4
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || xcrun --find clang-format 2>/dev/null || printf clang-format)
C_SOURCES = src/fft2d_seq.c src/fft2d_mpi.c src/tests/reference.h src/tests/test_seq.c src/tests/test_mpi.c
RESULTS ?= results/local-4096

.PHONY: all editor-config format format-check test test-mpi bench analyze report dist clean

all: fft2d_seq fft2d_mpi

# Each program is one self-contained C file; see the header comments.
fft2d_seq: src/fft2d_seq.c
	$(CC) $(CFLAGS) src/fft2d_seq.c -o $@ $(LDLIBS)

fft2d_mpi: src/fft2d_mpi.c
	$(MPICC) $(CFLAGS) src/fft2d_mpi.c -o $@ $(LDLIBS)

# The test suites include the program sources directly.
test_seq: src/tests/test_seq.c src/tests/reference.h src/fft2d_seq.c
	$(CC) $(CFLAGS) src/tests/test_seq.c -o $@ $(LDLIBS)

test_mpi: src/tests/test_mpi.c src/tests/reference.h src/fft2d_mpi.c
	$(MPICC) $(CFLAGS) src/tests/test_mpi.c -o $@ $(LDLIBS)

test: test_seq fft2d_seq
	./test_seq
	bash src/scripts/test-cli.sh seq

test-mpi: test_mpi fft2d_mpi
	@for p in $(MPI_TEST_PROCESSES); do echo "== $$p processes =="; $(MPIEXEC) $(MPIEXEC_FLAGS) -n $$p ./test_mpi || exit $$?; done
	MPIEXEC='$(MPIEXEC)' MPIEXEC_FLAGS='$(MPIEXEC_FLAGS)' bash src/scripts/test-cli.sh mpi

# Campaign settings (BASE_N, RUNS, SEED, PROCESSES, LAUNCH) come from the
# environment; see src/scripts/benchmark.sh. Set CAPRI_FINAL=1 to label the
# analysis and report as the final CAPRI campaign.
ANALYZE_FLAGS = $(if $(CAPRI_FINAL),--capri-final)

bench: all
	MPICC='$(MPICC)' bash src/scripts/benchmark.sh '$(RESULTS)'

analyze:
	uv run --locked src/scripts/analyze.py '$(RESULTS)/raw.csv' --output '$(RESULTS)' $(ANALYZE_FLAGS)

report:
	@if test -f '$(RESULTS)/raw.csv'; then uv run --locked src/scripts/analyze.py '$(RESULTS)/raw.csv' --output '$(RESULTS)' --report-dir report/generated $(ANALYZE_FLAGS); else rm -rf report/generated; fi
	latexmk -pdf -interaction=nonstopmode -halt-on-error -cd report/report.tex

dist:
	bash src/scripts/dist.sh '$(RESULTS)'

editor-config:
	bash src/scripts/editor-config.sh '$(MPICC)' $(CFLAGS)

format:
	$(CLANG_FORMAT) -i $(C_SOURCES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

clean:
	rm -f fft2d_seq fft2d_mpi test_seq test_mpi compile_flags.txt
	rm -f report/*.aux report/*.fdb_latexmk report/*.fls report/*.log report/*.out report/*.pdf report/*.toc
