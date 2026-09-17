"""Compares the square-matrix scaling of several campaigns in one figure.

Usage: compare.py OUTPUT_PDF DIR:LABEL [DIR:LABEL ...]

Reads summary.csv from each campaign directory and draws two panels:
median speedup on the left, median compute fraction on the right, one
curve per campaign (square shape only).
"""

import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

COLORS = ["#176b9b", "#c46f1a", "#3a8f5d", "#8d5bb0"]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)

    output = Path(sys.argv[1])
    plt.rcParams.update({"font.size": 9})
    figure, (left, right) = plt.subplots(
        1, 2, figsize=(6.9, 2.0), layout="constrained")
    all_processes = set()

    for color, argument in zip(COLORS, sys.argv[2:]):
        directory, _, label = argument.partition(":")
        with (Path(directory) / "summary.csv").open(newline="") as stream:
            rows = [
                row for row in csv.DictReader(stream)
                if row["shape"] == "square" and row["implementation"] == "mpi"
            ]
        processes = [int(row["processes"]) for row in rows]
        all_processes.update(processes)
        left.plot(processes, [float(row["speedup"]) for row in rows], "o-",
                  color=color, label=label)
        right.plot(processes,
                   [float(row["median_compute_fraction"]) for row in rows],
                   "o-", color=color, label=label)

    ticks = sorted(all_processes)
    left.plot(ticks, ticks, "--", color="#9a9a9a", label="ideal S(P) = P")

    for axes, ylabel in ((left, "Speedup S(P)"),
                         (right, "Compute fraction f")):
        axes.set_xscale("log", base=2)
        axes.set_xticks(ticks, [str(count) for count in ticks])
        axes.set_xlabel("MPI processes P")
        axes.set_ylabel(ylabel)
        axes.grid(alpha=0.2)
        axes.legend(fontsize=8)

    left.set_ylim(0, 20)
    right.set_ylim(0, 1)

    for extension in ("pdf", "png"):
        figure.savefig(output.with_suffix("." + extension), dpi=160)


if __name__ == "__main__":
    main()
