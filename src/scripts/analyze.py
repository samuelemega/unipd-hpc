"""Analyzes a campaign CSV: summary.csv, speedup plots, LaTeX fragment."""

import argparse
import csv
import shutil
import statistics
from pathlib import Path

SHAPES = ("square", "wide", "tall")


def load(path):
    """
    Reads raw.csv and groups the rows by (implementation, shape, processes).
    Requires the three shapes, a sequential baseline per shape and the same
    number of runs for every point; anything else raises ValueError.
    """
    groups = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            key = (row["implementation"], row["shape"], int(row["processes"]))
            groups.setdefault(key, []).append(row)

    counts = {len(rows) for rows in groups.values()}
    if not groups or len(counts) != 1:
        raise ValueError("every point needs the same number of runs")
    for shape in SHAPES:
        if ("seq", shape, 1) not in groups:
            raise ValueError(f"missing sequential baseline for {shape}")
    return groups


def summarize(groups):
    """
    Returns one summary row per point: dimensions, min/median/max time,
    median compute fraction, and speedup as the ratio of the sequential and
    MPI median times of the same shape. Rows are ordered by shape, version
    and process count.
    """
    summary = []
    for (implementation, shape, processes), rows in sorted(
        groups.items(),
        key=lambda item: (SHAPES.index(item[0][1]), item[0][0] != "seq",
                          item[0][2]),
    ):
        times = [float(row["time_seconds"]) for row in rows]
        fractions = [float(row["compute_fraction"]) for row in rows]
        summary.append({
            "implementation": implementation,
            "shape": shape,
            "ny": int(rows[0]["ny"]),
            "nx": int(rows[0]["nx"]),
            "processes": processes,
            "runs": len(rows),
            "min_seconds": min(times),
            "median_seconds": statistics.median(times),
            "max_seconds": max(times),
            "median_compute_fraction": statistics.median(fractions),
        })

    baselines = {
        row["shape"]: row["median_seconds"]
        for row in summary if row["implementation"] == "seq"
    }
    for row in summary:
        row["speedup"] = baselines[row["shape"]] / row["median_seconds"]
    return summary


def write_plots(summary, output, label):
    """Saves one PDF and PNG speedup plot per shape, with the ideal S(P)=P."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 10})
    for shape in SHAPES:
        rows = [
            row for row in summary
            if row["shape"] == shape and row["implementation"] == "mpi"
        ]
        processes = [row["processes"] for row in rows]
        figure, axes = plt.subplots(figsize=(6.0, 2.4), layout="constrained")
        axes.plot(processes, processes, "--", color="#9a9a9a",
                  label="Ideal S(P) = P")
        axes.plot(processes, [row["speedup"] for row in rows], "o-",
                  color="#176b9b", label="Measured median speedup")
        axes.set_xscale("log", base=2)
        axes.set_xticks(processes, [str(count) for count in processes])
        axes.set_ylim(bottom=0)
        axes.set_xlabel("MPI processes P")
        axes.set_ylabel("Speedup S(P)")
        axes.set_title(
            f"{shape.capitalize()}: {rows[0]['ny']} x {rows[0]['nx']}, {label}",
            fontsize=10,
        )
        axes.grid(alpha=0.2)
        axes.legend(fontsize=9)
        for extension in ("pdf", "png"):
            figure.savefig(output / f"speedup-{shape}.{extension}", dpi=160)
        plt.close(figure)


def write_overview(summary, output):
    """
    Saves one two-panel figure for the report: median speedup on the left
    and median compute fraction on the right, one curve per shape.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 9})
    figure, (left, right) = plt.subplots(
        1, 2, figsize=(6.9, 2.2), layout="constrained")
    colors = {"square": "#176b9b", "wide": "#c46f1a", "tall": "#3a8f5d"}
    processes = []

    for shape in SHAPES:
        rows = [
            row for row in summary
            if row["shape"] == shape and row["implementation"] == "mpi"
        ]
        processes = [row["processes"] for row in rows]
        left.plot(processes, [row["speedup"] for row in rows], "o-",
                  color=colors[shape], label=shape)
        right.plot(processes,
                   [row["median_compute_fraction"] for row in rows], "o-",
                   color=colors[shape], label=shape)

    left.plot(processes, processes, "--", color="#9a9a9a",
              label="ideal S(P) = P")

    for axes, ylabel in ((left, "Speedup S(P)"),
                         (right, "Compute fraction f")):
        axes.set_xscale("log", base=2)
        axes.set_xticks(processes, [str(count) for count in processes])
        axes.set_xlabel("MPI processes P")
        axes.set_ylabel(ylabel)
        axes.grid(alpha=0.2)
        axes.legend(fontsize=8)

    left.set_ylim(bottom=0)
    right.set_ylim(0, 1)

    for extension in ("pdf", "png"):
        figure.savefig(output / f"overview.{extension}", dpi=160)
    plt.close(figure)


def write_latex(summary, output, destination, label, final):
    """Writes results.tex (one table and one figure per shape) for the report."""
    destination.mkdir(parents=True, exist_ok=True)
    spread = round(100 * max(
        (row["max_seconds"] - row["min_seconds"]) / row["median_seconds"]
        for row in summary
    ))
    by_shape = {
        shape: {
            (row["implementation"], row["processes"]): row
            for row in summary if row["shape"] == shape
        }
        for shape in SHAPES
    }
    dims = {
        shape: next(iter(by_shape[shape].values())) for shape in SHAPES
    }
    text = [
        r"\FinalCampaigntrue" if final else r"\FinalCampaignfalse",
        rf"\textbf{{{label}.}} "
        f"{summary[0]['runs']} independent invocations per point, on the "
        rf"square {dims['square']['ny']} by {dims['square']['nx']}, wide "
        rf"{dims['wide']['ny']} by {dims['wide']['nx']} and tall "
        rf"{dims['tall']['ny']} by {dims['tall']['nx']} matrices; the "
        r"speedup uses the median times. "
        r"Table~\ref{tab:kernel} reports the median kernel measurements "
        r"and Figure~\ref{fig:overview} the strong-scaling curves with "
        r"the compute fraction $f$ of Section~\ref{sec:method}. Across "
        r"every point the minimum and the maximum stay within "
        rf"{spread}\% of the median.",
    ]
    if not final:
        text.append(
            "These are preliminary measurements and do not describe the "
            "final CAPRI performance."
        )
    table = [
        r"\begin{tabular}{lrrrrrrrrrr}\toprule",
        r" & & \multicolumn{3}{c}{Square} & \multicolumn{3}{c}{Wide} "
        r"& \multicolumn{3}{c}{Tall}\\",
        r"\cmidrule(lr){3-5}\cmidrule(lr){6-8}\cmidrule(lr){9-11}",
        r"Version & $P$ & $t$ (s) & $f$ & $S$ & $t$ (s) & $f$ & $S$ "
        r"& $t$ (s) & $f$ & $S$\\\midrule",
    ]
    for key in [(row["implementation"], row["processes"])
                for row in summary if row["shape"] == "square"]:
        cells = [key[0], str(key[1])]
        for shape in SHAPES:
            row = by_shape[shape][key]
            cells += [
                f"{row['median_seconds']:.4g}",
                f"{row['median_compute_fraction']:.3f}",
                f"{row['speedup']:.3f}",
            ]
        table.append(" & ".join(cells) + r"\\")
    table.append(r"\bottomrule\end{tabular}")
    shutil.copyfile(output / "overview.pdf", destination / "overview.pdf")
    (destination / "results.tex").write_text("\n".join(text) + "\n")
    (destination / "table-kernel.tex").write_text("\n".join(table) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--capri-final", action="store_true")
    parser.add_argument("--label")
    args = parser.parse_args()

    label = args.label or ("CAPRI final campaign" if args.capri_final
                           else "Preliminary campaign")
    try:
        summary = summarize(load(args.csv))
        args.output.mkdir(parents=True, exist_ok=True)
        with (args.output / "summary.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
        write_plots(summary, args.output, label)
        write_overview(summary, args.output)
        if args.report_dir:
            write_latex(summary, args.output, args.report_dir, label,
                        args.capri_final)
        print(f"Wrote {len(summary)} summary rows and three speedup plots "
              f"to {args.output}")
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"analysis error: {error}\n")


if __name__ == "__main__":
    main()
