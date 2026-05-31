"""
Gera figuras para o Capítulo 6 usando exactamente o mesmo estilo do csv_analyzer.
Corre a partir da raiz do relatório:
    python3 05_figures/06_preliminary/generate_plots.py
"""

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path

# ── Paths ─────────────────────────────────────────────────────────────────────
OUT = Path(__file__).parent
CSV = Path("/Users/afons/Desktop/petBionic/TestData/Round1dia28_cleaned"
           "/AndamentoTestes28/20260528_12h46m27_run09.csv")

# ── Cores do csv_analyzer ─────────────────────────────────────────────────────
C = {
    "roll":  "#e41a1c",
    "pitch": "#4daf4a",
    "yaw":   "#984ea3",
    "kg":    "#377eb8",
    "az":    "#d62728",
}
_SCATTER_PT = 2

# ── Helpers ───────────────────────────────────────────────────────────────────
def _smooth(arr, w):
    return pd.Series(arr).rolling(w, center=True, min_periods=1).mean().to_numpy()

def _uw(series):
    return np.degrees(np.unwrap(np.radians(series.to_numpy(float))))

def load(path):
    df = pd.read_csv(path, skipinitialspace=True)
    df.columns = [c.strip() for c in df.columns]
    df["t"] = (df["sample_us"] - df["sample_us"].iloc[0]) / 1e6
    return df

# ── Estilo publicação (serif, 9 pt — igual ao template LaTeX) ─────────────────
plt.rcParams.update({
    "font.family":     "serif",
    "font.size":        9,
    "axes.titlesize":   9,
    "axes.labelsize":   9,
    "xtick.labelsize":  8,
    "ytick.labelsize":  8,
    "legend.fontsize":  8,
    "axes.grid":       True,
    "grid.linestyle":  "--",
    "grid.alpha":       0.3,
    "lines.linewidth":  1.4,
    "figure.dpi":      150,
})

# ── Figura 1: RPY — igual ao tab "Main" do analyzer ──────────────────────────
def fig_rpy(df):
    x = df["t"].to_numpy()
    w = max(5, len(df) // 60)
    roll_uw  = _uw(df["roll_deg"])
    pitch_uw = _uw(df["pitch_deg"])
    yaw_uw   = _uw(df["yaw_deg"])

    fig, ax = plt.subplots(figsize=(6.5, 2.8))
    for arr, col, lbl in [
        (roll_uw,  C["roll"],  "Roll"),
        (pitch_uw, C["pitch"], "Pitch"),
        (yaw_uw,   C["yaw"],  "Yaw"),
    ]:
        ax.scatter(x, arr, s=_SCATTER_PT, c=col, alpha=0.4, linewidths=0)
        ax.plot(x, _smooth(arr, w), color=col, linewidth=1.4, alpha=0.9, label=lbl)

    ax.set_ylabel("Angle (°)")
    ax.set_xlabel("Time (s)")
    ax.set_title("Roll / Pitch / Yaw  (unwrapped)")
    ax.legend(loc="upper right", markerscale=4)
    fig.tight_layout()
    fig.savefig(OUT / "signal_imu.pdf", bbox_inches="tight", dpi=200)
    plt.close(fig)
    print(f"Saved signal_imu.pdf  (w={w}, N={len(df)})")

# ── Figura 2: Force + az — igual ao painel overview ───────────────────────────
def fig_overview(df):
    x = df["t"].to_numpy()
    w = max(5, len(df) // 60)
    kg = df["load_cell_est_kg"].to_numpy(float)
    az = (df["imu_az"].to_numpy(float) / 16384.0)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(6.5, 3.8),
                                    sharex=True, gridspec_kw={"hspace": 0.35})

    ax1.scatter(x, kg, s=_SCATTER_PT, c=C["kg"], alpha=0.4, linewidths=0)
    ax1.plot(x, _smooth(kg, w), color=C["kg"], linewidth=1.4, alpha=0.9, label="Force")
    ax1.axhline(0, color="k", lw=0.5, ls="--")
    ax1.set_ylabel("Force (kg)")
    ax1.set_title("Force and vertical acceleration during walking (run 9)")
    ax1.legend(loc="upper right")

    ax2.scatter(x, az, s=_SCATTER_PT, c=C["az"], alpha=0.4, linewidths=0)
    ax2.plot(x, _smooth(az, w), color=C["az"], linewidth=1.4, alpha=0.9, label=r"$a_z$")
    ax2.set_ylabel(r"$a_z$ (g)")
    ax2.set_xlabel("Time (s)")
    ax2.legend(loc="upper right")

    fig.savefig(OUT / "signal_overview.pdf", bbox_inches="tight", dpi=200)
    plt.close(fig)
    print(f"Saved signal_overview.pdf")

# ── Main ──────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    df = load(CSV)
    print(f"Loaded {CSV.name}: {len(df)} samples, {df['t'].iloc[-1]:.1f}s")
    fig_rpy(df)
    fig_overview(df)
    print("Done.")
