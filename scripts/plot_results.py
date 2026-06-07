#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def floats(rows, key):
    return [float(row[key]) for row in rows]


def ints(rows, key):
    return [int(float(row[key])) for row in rows]


def save(fig, path):
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def plot_strong(results_dir, charts_dir):
    strong_files = sorted(results_dir.glob("strong_scaling_*_100000.csv"))
    if not strong_files:
        return

    fig_speed, ax_speed = plt.subplots(figsize=(7, 4.5))
    fig_eff, ax_eff = plt.subplots(figsize=(7, 4.5))
    fig_times, ax_times = plt.subplots(figsize=(7, 4.5))
    max_processes = 1

    for path in strong_files:
        rows = read_csv(path)
        if not rows:
            continue
        label = path.stem.replace("strong_scaling_", "N=").replace("_100000", "")
        p = ints(rows, "processes")
        speedup = floats(rows, "speedup")
        efficiency = floats(rows, "efficiency_percent")
        elapsed = floats(rows, "elapsed_seconds")
        max_processes = max(max_processes, max(p))

        ax_speed.plot(p, speedup, marker="o", label=label)
        ax_eff.plot(p, efficiency, marker="o", label=label)
        ax_times.plot(p, elapsed, marker="o", label=label)

    if strong_files:
        ideal_p = [value for value in [1, 2, 4, 8, 16] if value <= max_processes]
        ax_speed.plot(ideal_p, ideal_p, "--", color="gray", label="ideal")

    ax_speed.set_title("Strong Scaling - Speedup")
    ax_speed.set_xlabel("Numar procese")
    ax_speed.set_ylabel("Speedup")
    ax_speed.grid(True, alpha=0.3)
    ax_speed.legend()

    ax_eff.set_title("Strong Scaling - Eficienta")
    ax_eff.set_xlabel("Numar procese")
    ax_eff.set_ylabel("Eficienta (%)")
    ax_eff.grid(True, alpha=0.3)
    ax_eff.legend()

    ax_times.set_title("Strong Scaling - Timp Total")
    ax_times.set_xlabel("Numar procese")
    ax_times.set_ylabel("Timp (s)")
    ax_times.grid(True, alpha=0.3)
    ax_times.legend()

    save(fig_speed, charts_dir / "strong_speedup.png")
    save(fig_eff, charts_dir / "strong_efficiency.png")
    save(fig_times, charts_dir / "strong_elapsed.png")


def plot_compute_vs_comm(results_dir, charts_dir):
    path = results_dir / "strong_scaling_1000_100000.csv"
    if not path.exists():
        return

    rows = read_csv(path)
    p = ints(rows, "processes")
    compute = floats(rows, "compute_seconds")
    communication = floats(rows, "communication_seconds")
    io = floats(rows, "io_seconds")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(p, compute, marker="o", label="calcul")
    ax.plot(p, communication, marker="o", label="comunicare")
    ax.plot(p, io, marker="o", label="I/O")
    ax.set_title("Calcul vs Comunicare vs I/O (N=1000, T=100000)")
    ax.set_xlabel("Numar procese")
    ax.set_ylabel("Timp (s)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    save(fig, charts_dir / "compute_communication_io.png")


def plot_weak(results_dir, charts_dir):
    rows = read_csv(results_dir / "weak_scaling_100000.csv")
    p = ints(rows, "processes")
    elapsed = floats(rows, "elapsed_seconds")
    compute = floats(rows, "compute_seconds")
    communication = floats(rows, "communication_seconds")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(p, elapsed, marker="o", label="total")
    ax.plot(p, compute, marker="o", label="calcul")
    ax.plot(p, communication, marker="o", label="comunicare")
    ax.set_title("Weak Scaling")
    ax.set_xlabel("Numar procese")
    ax.set_ylabel("Timp (s)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    save(fig, charts_dir / "weak_scaling.png")


def plot_migration(results_dir, charts_dir):
    rows = read_csv(results_dir / "migration_overhead_1000_100000.csv")
    ants = ints(rows, "ants")
    elapsed = floats(rows, "elapsed_seconds")
    compute = floats(rows, "compute_seconds")
    communication = floats(rows, "communication_seconds")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(ants, elapsed, marker="o", label="total")
    ax.plot(ants, compute, marker="o", label="calcul")
    ax.plot(ants, communication, marker="o", label="comunicare")
    ax.set_xscale("log")
    ax.set_title("Overhead Migrare Furnici")
    ax.set_xlabel("Numar furnici")
    ax.set_ylabel("Timp (s)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    save(fig, charts_dir / "migration_overhead.png")


def plot_gather(results_dir, charts_dir):
    rows = read_csv(results_dir / "gather_frequency_1000_10000.csv")
    gather_every = ints(rows, "gather_every")
    elapsed = floats(rows, "elapsed_seconds")
    io = floats(rows, "io_seconds")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(gather_every, elapsed, marker="o", label="total")
    ax.plot(gather_every, io, marker="o", label="I/O gather")
    ax.set_xscale("log")
    ax.set_title("Impact Frecventa Colectarii")
    ax.set_xlabel("gather_every")
    ax.set_ylabel("Timp (s)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    save(fig, charts_dir / "gather_frequency.png")


def plot_compute_heavy(results_dir, charts_dir):
    path = results_dir / "strong_scaling_compute_heavy_5000_50000_ants5000.csv"
    if not path.exists():
        return

    rows = read_csv(path)
    p = ints(rows, "processes")
    speedup = floats(rows, "speedup")
    efficiency = floats(rows, "efficiency_percent")
    compute = floats(rows, "compute_seconds")
    communication = floats(rows, "communication_seconds")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(p, speedup, marker="o", label="speedup")
    ax.plot(p, p, "--", color="gray", label="ideal")
    ax.set_title("Compute-Heavy Strong Scaling (N=5000, T=50000, ants=5000)")
    ax.set_xlabel("Numar procese")
    ax.set_ylabel("Speedup")
    ax.grid(True, alpha=0.3)
    ax.legend()
    save(fig, charts_dir / "compute_heavy_speedup.png")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(p, efficiency, marker="o")
    ax.set_title("Compute-Heavy Eficienta")
    ax.set_xlabel("Numar procese")
    ax.set_ylabel("Eficienta (%)")
    ax.grid(True, alpha=0.3)
    save(fig, charts_dir / "compute_heavy_efficiency.png")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(p, compute, marker="o", label="calcul")
    ax.plot(p, communication, marker="o", label="comunicare")
    ax.set_title("Compute-Heavy Calcul vs Comunicare")
    ax.set_xlabel("Numar procese")
    ax.set_ylabel("Timp (s)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    save(fig, charts_dir / "compute_heavy_compute_vs_comm.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", default="results_large")
    parser.add_argument("--charts-dir", default="results_large/charts")
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    charts_dir = Path(args.charts_dir)
    charts_dir.mkdir(parents=True, exist_ok=True)

    plot_strong(results_dir, charts_dir)
    plot_compute_vs_comm(results_dir, charts_dir)
    plot_weak(results_dir, charts_dir)
    plot_migration(results_dir, charts_dir)
    plot_gather(results_dir, charts_dir)
    plot_compute_heavy(results_dir, charts_dir)

    for path in sorted(charts_dir.glob("*.png")):
        print(path)


if __name__ == "__main__":
    main()
