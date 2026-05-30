#!/usr/bin/env python3
"""
petBionic CSV Analyzer v2
Visualizador interactivo de dados da prótese canina.

Tabs  : Célula de Carga | Acelerómetro | Giroscópio | Magnetómetro | Orientação 3D
Browse: Downloads + TestData pré-visualizados na barra lateral
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("QtAgg")
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg, NavigationToolbar2QT
from matplotlib.figure import Figure
from matplotlib.widgets import SpanSelector
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401  (activa projection='3d')

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QSplitter,
    QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QSlider,
    QTabWidget, QSizePolicy, QMessageBox, QFileDialog,
    QTreeWidget, QTreeWidgetItem, QStyle,
)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer
from PyQt6.QtGui import QFont


# ── caminhos ───────────────────────────────────────────────────────────────

_BASE = Path.home() / "Desktop" / "petBionic"
TESTDATA_DIR  = _BASE / "TestData"
DOWNLOADS_DIR = Path.home() / "Downloads"


# ── paleta de cores ────────────────────────────────────────────────────────

C = {
    "kg":    "#1f77b4",
    "raw":   "#d95f02",
    "x":     "#e41a1c",
    "y":     "#4daf4a",
    "z":     "#984ea3",
    "roll":  "#e41a1c",
    "pitch": "#4daf4a",
    "yaw":   "#984ea3",
}

_SCATTER_PT = 3   # tamanho dos pontos do scatter (px)


# ══════════════════════════════════════════════════════════════════════════
#  3-D model da prótese (forma de taco de golfe)
# ══════════════════════════════════════════════════════════════════════════

def _euler_to_R(roll_deg: float, pitch_deg: float, yaw_deg: float) -> np.ndarray:
    """ZYX Euler → matriz de rotação 3×3."""
    r, p, y = np.radians([roll_deg, pitch_deg, yaw_deg])
    Rx = np.array([[1, 0, 0], [0, np.cos(r), -np.sin(r)], [0, np.sin(r), np.cos(r)]])
    Ry = np.array([[np.cos(p), 0, np.sin(p)], [0, 1, 0], [-np.sin(p), 0, np.cos(p)]])
    Rz = np.array([[np.cos(y), -np.sin(y), 0], [np.sin(y), np.cos(y), 0], [0, 0, 1]])
    return Rz @ Ry @ Rx


def _cyl(r: float, z0: float, z1: float, n: int = 22):
    """Superfície cilíndrica parametrizada."""
    t = np.linspace(0, 2 * np.pi, n)
    Z = np.array([z0, z1])
    T, Zm = np.meshgrid(t, Z)
    return r * np.cos(T), r * np.sin(T), Zm


def _rot(R, X, Y, Z):
    pts = R @ np.vstack([X.ravel(), Y.ravel(), Z.ravel()])
    return pts[0].reshape(X.shape), pts[1].reshape(Y.shape), pts[2].reshape(Z.shape)


def draw_prosthetic(ax, roll: float, pitch: float, yaw: float) -> None:
    """Redesenha o modelo 3D da prótese com a orientação dada."""
    ax.cla()
    R = _euler_to_R(roll, pitch, yaw)

    # -- haste (shaft) -------------------------------------------------------
    X, Y, Z = _cyl(0.038, -0.50, 0.44)
    ax.plot_surface(*_rot(R, X, Y, Z), color="#b0b0b0", alpha=0.92,
                    linewidth=0, shade=True)

    # -- punho (grip) — secção superior mais larga ---------------------------
    X, Y, Z = _cyl(0.065, 0.41, 0.50)
    ax.plot_surface(*_rot(R, X, Y, Z), color="#333333", alpha=1.0,
                    linewidth=0, shade=True)

    # -- cabeça (head / pad) — caixa achatada na base -----------------------
    #    x ∈ [-0.30, 0.08], y ∈ [-0.04, 0.04], z ∈ {-0.50, -0.44}
    for y0, y1 in [(-0.04, 0.04)]:
        for z_face in [-0.50, -0.44]:
            xv = np.array([[-0.30, 0.08], [-0.30, 0.08]])
            yv = np.array([[y0, y0], [y1, y1]])
            zv = np.full_like(xv, z_face)
            ax.plot_surface(*_rot(R, xv, yv, zv), color="#787878",
                            alpha=0.95, linewidth=0, shade=True)

    for x0, x1 in [(-0.30, -0.30), (0.08, 0.08)]:   # lados esquerdo/direito
        yv = np.array([[-0.04, -0.04], [0.04, 0.04]])
        xv = np.full_like(yv, x0)
        zv = np.array([[-0.50, -0.44], [-0.50, -0.44]])
        ax.plot_surface(*_rot(R, xv, yv, zv), color="#909090",
                        alpha=0.90, linewidth=0, shade=True)

    for y_face in [-0.04, 0.04]:                      # frente/trás
        xv = np.array([[-0.30, 0.08], [-0.30, 0.08]])
        zv = np.array([[-0.50, -0.50], [-0.44, -0.44]])
        yv = np.full_like(xv, y_face)
        ax.plot_surface(*_rot(R, xv, yv, zv), color="#a0a0a0",
                        alpha=0.88, linewidth=0, shade=True)

    # -- triedro de referência (X=vermelho, Y=verde, Z=azul) -----------------
    for vec, col in [([0.7, 0, 0], "r"), ([0, 0.7, 0], "g"), ([0, 0, 0.7], "b")]:
        vr = R @ np.array(vec)
        ax.plot([0, vr[0]], [0, vr[1]], [0, vr[2]], color=col,
                linewidth=1.0, alpha=0.45)

    lim = 0.75
    ax.set_xlim(-lim, lim); ax.set_ylim(-lim, lim); ax.set_zlim(-lim, lim)
    ax.set_xlabel("X", fontsize=8, labelpad=0)
    ax.set_ylabel("Y", fontsize=8, labelpad=0)
    ax.set_zlabel("Z", fontsize=8, labelpad=0)
    ax.set_title(f"roll {roll:+.1f}°   pitch {pitch:+.1f}°   yaw {yaw:+.1f}°",
                 fontsize=9, pad=4)
    ax.tick_params(labelsize=7, pad=1)


# ══════════════════════════════════════════════════════════════════════════
#  Botão de toggle com cor
# ══════════════════════════════════════════════════════════════════════════

class ToggleBtn(QPushButton):
    def __init__(self, text: str, color: str, parent=None):
        super().__init__(text, parent)
        self.setCheckable(True)
        self.setChecked(True)
        self.setFixedHeight(26)
        self._color = color
        self._refresh_style()
        self.toggled.connect(lambda _: self._refresh_style())

    def _refresh_style(self):
        if self.isChecked():
            self.setStyleSheet(
                f"QPushButton {{ background: {self._color}; color: white; "
                "border: none; border-radius: 4px; "
                "padding: 2px 10px; font-weight: bold; }"
            )
        else:
            self.setStyleSheet(
                "QPushButton { background: #ddd; color: #999; "
                "border: 1px solid #bbb; border-radius: 4px; padding: 2px 10px; }"
            )


# ══════════════════════════════════════════════════════════════════════════
#  Painel lateral – browser de ficheiros (árvore estilo VSCode)
# ══════════════════════════════════════════════════════════════════════════

def _fmt_size(path: Path) -> str:
    kb = path.stat().st_size // 1024
    return f"{kb} KB" if kb < 1024 else f"{kb / 1024:.1f} MB"


class FileBrowserPanel(QWidget):
    file_selected = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumWidth(220)
        self.setMaximumWidth(300)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 4, 0, 4)
        lay.setSpacing(4)

        hdr = QLabel("  Explorador")
        hdr.setFont(QFont("", 10, QFont.Weight.Bold))
        hdr.setStyleSheet(
            "color:#555; background:#f0f0f0; padding:4px 0px;"
            "border-bottom:1px solid #d0d0d0; letter-spacing:1px;"
        )
        lay.addWidget(hdr)

        # Árvore principal
        self._tree = QTreeWidget()
        self._tree.setHeaderHidden(True)
        self._tree.setAnimated(True)
        self._tree.setIndentation(16)
        self._tree.setUniformRowHeights(True)
        # padding apenas — seleção e cores ficam com o sistema (compatível dark/light)
        self._tree.setStyleSheet("QTreeWidget::item { padding: 3px 2px; }")
        self._tree.itemDoubleClicked.connect(self._on_double_click)
        lay.addWidget(self._tree, stretch=1)

        # Botões
        btn_row = QHBoxLayout()
        btn_row.setContentsMargins(4, 0, 4, 0)
        for label, slot in [("Actualizar", self.refresh),
                             ("Outro…",    self._open_dialog)]:
            btn = QPushButton(label)
            btn.setFixedHeight(24)
            btn.setStyleSheet("font-size:10px;")
            btn.clicked.connect(slot)
            btn_row.addWidget(btn)
        lay.addLayout(btn_row)

        self._build_tree()

    # ── ícones do sistema ───────────────────────────────────────────────────

    def _icon_dir(self):
        return self.style().standardIcon(QStyle.StandardPixmap.SP_DirIcon)

    def _icon_file(self):
        return self.style().standardIcon(QStyle.StandardPixmap.SP_FileIcon)

    # ── construção da árvore ────────────────────────────────────────────────

    def _root_font(self) -> QFont:
        f = QFont()
        f.setBold(True)
        return f

    def _build_tree(self):
        self._tree.clear()

        # Downloads — só CSVs directamente na pasta (não recursivo)
        dl_item = QTreeWidgetItem(self._tree, ["Downloads"])
        dl_item.setIcon(0, self._icon_dir())
        dl_item.setData(0, Qt.ItemDataRole.UserRole, None)
        dl_item.setFont(0, self._root_font())
        if DOWNLOADS_DIR.exists():
            csvs = sorted(DOWNLOADS_DIR.glob("*.csv"), key=lambda f: f.name)
            for f in csvs:
                self._add_file(dl_item, f)
        dl_item.setExpanded(False)

        # Test Data — recursivo com sub-pastas
        td_item = QTreeWidgetItem(self._tree, ["Test Data"])
        td_item.setIcon(0, self._icon_dir())
        td_item.setData(0, Qt.ItemDataRole.UserRole, None)
        td_item.setFont(0, self._root_font())
        if TESTDATA_DIR.exists():
            self._add_dir_children(td_item, TESTDATA_DIR)
        td_item.setExpanded(False)

    def _add_dir_children(self, parent: QTreeWidgetItem, directory: Path):
        """Adiciona sub-pastas (expansíveis) e depois os CSVs desta pasta."""
        subdirs = sorted(
            (d for d in directory.iterdir() if d.is_dir()),
            key=lambda d: d.name,
        )
        for subdir in subdirs:
            folder = QTreeWidgetItem(parent, [subdir.name])
            folder.setIcon(0, self._icon_dir())
            folder.setData(0, Qt.ItemDataRole.UserRole, None)
            folder.setFont(0, self._root_font())
            self._add_dir_children(folder, subdir)
            folder.setExpanded(False)  # recolhido por defeito

        # CSVs desta pasta, ordenados pelo nome (= ordem temporal pelo nosso formato)
        csvs = sorted(
            (f for f in directory.iterdir() if f.is_file() and f.suffix == ".csv"),
            key=lambda f: f.name,
        )
        for f in csvs:
            self._add_file(parent, f)

    def _add_file(self, parent: QTreeWidgetItem, path: Path):
        size = _fmt_size(path)
        item = QTreeWidgetItem(parent, [f"{path.name}   {size}"])
        item.setIcon(0, self._icon_file())
        item.setData(0, Qt.ItemDataRole.UserRole, str(path))
        item.setToolTip(0, str(path))

    # ── interacção ──────────────────────────────────────────────────────────

    def _on_double_click(self, item: QTreeWidgetItem, _col: int):
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if path:
            self.file_selected.emit(path)

    def _open_dialog(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Abrir CSV", str(_BASE), "CSV (*.csv);;Todos (*)"
        )
        if path:
            self.file_selected.emit(path)

    def refresh(self):
        # guarda quais as pastas expandidas para restaurar depois
        expanded: set[str] = set()
        it = QTreeWidgetItem()
        root = self._tree.invisibleRootItem()
        stack = [root.child(i) for i in range(root.childCount())]
        while stack:
            node = stack.pop()
            if node and node.isExpanded():
                expanded.add(node.text(0))
            if node:
                stack += [node.child(i) for i in range(node.childCount())]

        self._build_tree()

        # restaura estado de expansão
        stack = [root.child(i) for i in range(root.childCount())]
        while stack:
            node = stack.pop()
            if node and node.text(0) in expanded:
                node.setExpanded(True)
            if node:
                stack += [node.child(i) for i in range(node.childCount())]


# ── botão de modo: Pontos ↔ Linha (partilhado por todos os tabs) ──────────

class _FitBtn(QPushButton):
    """Botão que alterna entre 'Linha' (mostrar fit suavizado) e 'Pontos' (scatter)."""
    def __init__(self, parent=None):
        super().__init__("Linha", parent)
        self.setCheckable(True); self.setChecked(False)
        self.setFixedHeight(26); self.setFixedWidth(70)
        self.setStyleSheet(
            "QPushButton { padding:2px 6px; border:1px solid #999; "
            "border-radius:4px; background:#f5f5f5; }"
            "QPushButton:checked { background:#444; color:white; border-color:#333; }"
        )


# ══════════════════════════════════════════════════════════════════════════
#  Tab – Célula de Carga  (kg + raw ADC, scatter + fit, toggles)
# ══════════════════════════════════════════════════════════════════════════

class LoadCellTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)

        tbar = QHBoxLayout()
        self._btn_kg  = ToggleBtn("Peso (kg)", C["kg"])
        self._btn_raw = ToggleBtn("Raw ADC",   C["raw"])
        self._btn_fit = _FitBtn()
        for b in [self._btn_kg, self._btn_raw, self._btn_fit]:
            tbar.addWidget(b)
        tbar.addStretch()
        lay.addLayout(tbar)

        self._fig = Figure(figsize=(11, 4))
        self._fig.subplots_adjust(left=0.07, right=0.93, top=0.94, bottom=0.11)
        self._ax_kg  = self._fig.add_subplot(111)
        self._ax_raw = self._ax_kg.twinx()
        self._canvas = FigureCanvasQTAgg(self._fig)
        self._toolbar = NavigationToolbar2QT(self._canvas, self)
        self._toolbar.setFixedHeight(30)
        lay.addWidget(self._toolbar)
        lay.addWidget(self._canvas)

        self._sc_kg = self._sc_raw = None
        self._ln_kg = self._ln_raw = None   # smooth lines
        self._span: SpanSelector | None = None

        self._btn_kg.toggled.connect(lambda v: self._set_vis_lc(self._sc_kg, self._ln_kg, v))
        self._btn_raw.toggled.connect(lambda v: self._set_vis_lc(self._sc_raw, self._ln_raw, v))
        self._btn_fit.toggled.connect(self._on_fit)

    def _set_vis_lc(self, sc, ln, v: bool):
        show_line = self._btn_fit.isChecked()
        if sc: sc.set_visible(v and not show_line)
        if ln: ln.set_visible(v and show_line)
        self._canvas.draw_idle()

    def load(self, df: pd.DataFrame):
        self._ax_kg.cla(); self._ax_raw.cla()
        x = (df["sample_us"] - df["sample_us"].iloc[0]) / 1e6
        w = max(5, len(df) // 60)

        if "load_cell_est_kg" in df.columns:
            y = df["load_cell_est_kg"].to_numpy(float)
            show_line = self._btn_fit.isChecked()
            self._sc_kg = self._ax_kg.scatter(
                x, y, s=_SCATTER_PT, c=C["kg"], label="Peso (kg)",
                alpha=0.85, linewidths=0)
            self._sc_kg.set_visible(self._btn_kg.isChecked() and not show_line)
            ys = pd.Series(y).rolling(w, center=True, min_periods=1).mean().to_numpy()
            self._ln_kg, = self._ax_kg.plot(
                x, ys, color=C["kg"], linewidth=1.4, alpha=0.9, label="Peso (kg)")
            self._ln_kg.set_visible(self._btn_kg.isChecked() and show_line)

        if "load_cell_raw" in df.columns:
            y = df["load_cell_raw"].to_numpy(float)
            show_line = self._btn_fit.isChecked()
            self._sc_raw = self._ax_raw.scatter(
                x, y, s=_SCATTER_PT, c=C["raw"], label="Raw ADC",
                alpha=0.65, linewidths=0)
            self._sc_raw.set_visible(self._btn_raw.isChecked() and not show_line)
            ys = pd.Series(y).rolling(w, center=True, min_periods=1).mean().to_numpy()
            self._ln_raw, = self._ax_raw.plot(
                x, ys, color=C["raw"], linewidth=1.4, alpha=0.9, label="Raw ADC")
            self._ln_raw.set_visible(self._btn_raw.isChecked() and show_line)

        self._ax_kg.set_xlabel("Tempo (s)", fontsize=10)
        self._ax_kg.set_ylabel("Peso estimado (kg)", color=C["kg"], fontsize=10)
        self._ax_raw.set_ylabel("Raw ADC", color=C["raw"], fontsize=10)
        self._ax_kg.tick_params(axis="y", labelcolor=C["kg"])
        self._ax_raw.tick_params(axis="y", labelcolor=C["raw"])
        self._ax_kg.grid(True, linestyle="--", alpha=0.3)
        self._ax_kg.set_title(
            "Célula de Carga  —  arrastar = zoom · duplo-clique = reset", fontsize=10)

        self._span = SpanSelector(
            self._ax_kg, self._on_span, "horizontal",
            useblit=True, props=dict(alpha=0.18, facecolor="#1f77b4"))
        self._canvas.mpl_connect(
            "button_press_event", lambda e: self._reset_zoom() if e.dblclick else None)
        self._canvas.draw_idle()

    def _on_fit(self, show_line: bool):
        self._btn_fit.setText("Pontos" if show_line else "Linha")
        for sc, ln, btn in [
            (self._sc_kg,  self._ln_kg,  self._btn_kg),
            (self._sc_raw, self._ln_raw, self._btn_raw),
        ]:
            v = btn.isChecked()
            if sc: sc.set_visible(v and not show_line)
            if ln: ln.set_visible(v and show_line)
        self._canvas.draw_idle()

    def _visible_y(self, xmin, xmax):
        """Collect Y values in [xmin,xmax] from whichever artists are currently visible."""
        show_line = self._btn_fit.isChecked()
        yall: list[float] = []
        pairs = [(self._sc_kg, self._ln_kg, self._ax_kg),
                 (self._sc_raw, self._ln_raw, self._ax_raw)]
        for sc, ln, ax in pairs:
            if show_line and ln and ln.get_visible():
                xd = np.asarray(ln.get_xdata()); yd = np.asarray(ln.get_ydata())
                m = (xd >= xmin) & (xd <= xmax)
                if m.any(): yall.append((ax, yd[m]))
            elif sc and sc.get_visible():
                pts = sc.get_offsets()
                m = (pts[:, 0] >= xmin) & (pts[:, 0] <= xmax)
                if m.any(): yall.append((ax, pts[m, 1]))
        return yall

    def _on_span(self, xmin: float, xmax: float):
        if xmax - xmin < 0.05 or self._toolbar.mode:
            return
        self._ax_kg.set_xlim(xmin, xmax)
        for ax, yv in self._visible_y(xmin, xmax):
            span = float(yv.max() - yv.min())
            pad = span * 0.08 if span > 0 else max(abs(float(yv.mean())) * 0.05, 0.5)
            ax.set_ylim(float(yv.min()) - pad, float(yv.max()) + pad)
        self._canvas.draw_idle()

    def _reset_zoom(self):
        self._ax_kg.autoscale(); self._ax_raw.autoscale()
        self._canvas.draw_idle()


# ══════════════════════════════════════════════════════════════════════════
#  Tab – IMU genérico (acelerómetro | giroscópio | magnetómetro | orientação)
# ══════════════════════════════════════════════════════════════════════════

def _smooth(series, w: int) -> np.ndarray:
    return pd.Series(series).rolling(w, center=True, min_periods=1).mean().to_numpy()


class ImuTab(QWidget):
    def __init__(self,
                 cols: tuple[str, str, str],
                 labels: tuple[str, str, str],
                 title: str,
                 ylabel: str,
                 show_magnitude: bool = True,
                 unwrap: bool = False,
                 parent=None):
        super().__init__(parent)
        self._cols           = cols
        self._labels         = labels
        self._title          = title
        self._ylabel         = ylabel
        self._show_magnitude = show_magnitude
        self._unwrap         = unwrap

        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)

        tbar = QHBoxLayout()
        self._btns: list[ToggleBtn] = []
        for lbl, key in zip(labels, ("x", "y", "z")):
            btn = ToggleBtn(lbl, C[key]); self._btns.append(btn); tbar.addWidget(btn)

        self._btn_mag: ToggleBtn | None = None
        if show_magnitude:
            self._btn_mag = ToggleBtn("Módulo", "#ff7f0e")
            tbar.addWidget(self._btn_mag)

        self._btn_fit = _FitBtn()
        tbar.addWidget(self._btn_fit)
        tbar.addStretch()
        lay.addLayout(tbar)

        self._fig = Figure(figsize=(11, 4))
        self._fig.subplots_adjust(left=0.08, right=0.97, top=0.94, bottom=0.11)
        self._ax = self._fig.add_subplot(111)
        self._canvas = FigureCanvasQTAgg(self._fig)
        self._toolbar = NavigationToolbar2QT(self._canvas, self)
        self._toolbar.setFixedHeight(30)
        lay.addWidget(self._toolbar)
        lay.addWidget(self._canvas)

        self._scs:   list = [None, None, None]
        self._lines: list = [None, None, None]
        self._sc_mag = self._ln_mag = None
        self._span: SpanSelector | None = None

        for i, btn in enumerate(self._btns):
            btn.toggled.connect(lambda v, idx=i: self._set_vis(idx, v))
        if self._btn_mag:
            self._btn_mag.toggled.connect(self._on_mag_toggle)
        self._btn_fit.toggled.connect(self._on_fit)

    # ── visibility helpers ────────────────────────────────────────────────────

    def _set_vis(self, idx: int, v: bool):
        show_line = self._btn_fit.isChecked()
        if self._scs[idx]:   self._scs[idx].set_visible(v and not show_line)
        if self._lines[idx]: self._lines[idx].set_visible(v and show_line)
        self._canvas.draw_idle()

    def _on_mag_toggle(self, v: bool):
        show_line = self._btn_fit.isChecked()
        if self._sc_mag: self._sc_mag.set_visible(v and not show_line)
        if self._ln_mag: self._ln_mag.set_visible(v and show_line)
        self._canvas.draw_idle()

    def _on_fit(self, show_line: bool):
        self._btn_fit.setText("Pontos" if show_line else "Linha")
        for i, btn in enumerate(self._btns):
            v = btn.isChecked()
            if self._scs[i]:   self._scs[i].set_visible(v and not show_line)
            if self._lines[i]: self._lines[i].set_visible(v and show_line)
        if self._btn_mag:
            v = self._btn_mag.isChecked()
            if self._sc_mag: self._sc_mag.set_visible(v and not show_line)
            if self._ln_mag: self._ln_mag.set_visible(v and show_line)
        self._canvas.draw_idle()

    # ── load ──────────────────────────────────────────────────────────────────

    def load(self, df: pd.DataFrame):
        self._ax.cla()
        x   = (df["sample_us"] - df["sample_us"].iloc[0]) / 1e6
        w   = max(5, len(df) // 60)
        clr = [C["x"], C["y"], C["z"]]
        show_line = self._btn_fit.isChecked()

        for i, (col, lbl, col_c) in enumerate(zip(self._cols, self._labels, clr)):
            if col in df.columns:
                y = df[col].to_numpy(float)
                if self._unwrap:
                    y = np.degrees(np.unwrap(np.radians(y)))
                sc = self._ax.scatter(x, y, s=_SCATTER_PT, c=col_c,
                                      label=lbl, alpha=0.75, linewidths=0)
                sc.set_visible(self._btns[i].isChecked() and not show_line)
                self._scs[i] = sc

                ln, = self._ax.plot(x, _smooth(y, w), color=col_c,
                                    linewidth=1.4, alpha=0.9)
                ln.set_visible(self._btns[i].isChecked() and show_line)
                self._lines[i] = ln

        # módulo (sqrt(x²+y²+z²))
        if self._show_magnitude and all(c in df.columns for c in self._cols):
            mag = np.sqrt(sum(df[c].to_numpy(float)**2 for c in self._cols))
            sc  = self._ax.scatter(x, mag, s=_SCATTER_PT, c="#ff7f0e",
                                   label="Módulo", alpha=0.75, linewidths=0)
            sc.set_visible(self._btn_mag.isChecked() and not show_line)
            self._sc_mag = sc

            ln, = self._ax.plot(x, _smooth(mag, w), color="#ff7f0e",
                                linewidth=1.4, alpha=0.9)
            ln.set_visible(self._btn_mag.isChecked() and show_line)
            self._ln_mag = ln

        self._ax.set_xlabel("Tempo (s)", fontsize=10)
        self._ax.set_ylabel(self._ylabel, fontsize=10)
        self._ax.legend(loc="upper right", fontsize=9, markerscale=3)
        self._ax.grid(True, linestyle="--", alpha=0.3)
        self._ax.set_title(
            f"{self._title}  —  arrastar = zoom · duplo-clique = reset", fontsize=10)

        self._span = SpanSelector(
            self._ax, self._on_span, "horizontal",
            useblit=True, props=dict(alpha=0.18, facecolor="#1f77b4"))
        self._canvas.mpl_connect(
            "button_press_event", lambda e: self._reset_zoom() if e.dblclick else None)
        self._canvas.draw_idle()

    # ── zoom ──────────────────────────────────────────────────────────────────

    def _on_span(self, xmin: float, xmax: float):
        if xmax - xmin < 0.05 or self._toolbar.mode:
            return
        self._ax.set_xlim(xmin, xmax)
        show_line = self._btn_fit.isChecked()
        yall: list[float] = []

        def _collect(sc, ln):
            if show_line and ln and ln.get_visible():
                xd = np.asarray(ln.get_xdata()); yd = np.asarray(ln.get_ydata())
                m = (xd >= xmin) & (xd <= xmax)
                if m.any(): yall.extend(yd[m].tolist())
            elif sc and sc.get_visible():
                pts = sc.get_offsets()
                m   = (pts[:, 0] >= xmin) & (pts[:, 0] <= xmax)
                if m.any(): yall.extend(pts[m, 1].tolist())

        for sc, ln in zip(self._scs, self._lines):
            _collect(sc, ln)
        _collect(self._sc_mag, self._ln_mag)

        if yall:
            mn, mx = min(yall), max(yall)
            pad = (mx - mn) * 0.08 if mx != mn else max(abs(mn) * 0.05, 1.0)
            self._ax.set_ylim(mn - pad, mx + pad)
        self._canvas.draw_idle()

    def _reset_zoom(self):
        self._ax.autoscale(); self._canvas.draw_idle()


# ══════════════════════════════════════════════════════════════════════════
#  Tab – Orientação 3D
#    RPY (sem zoom, só scrub) + Kg sincronizado + modelo 3D + play
# ══════════════════════════════════════════════════════════════════════════

class Orientation3DTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        import time as _time_mod
        self._time_mod = _time_mod

        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)
        lay.setSpacing(3)

        # ── toggles: RPY + Kg + Linha/Pontos ──────────────────────────────────
        tbar = QHBoxLayout()
        self._btn_roll  = ToggleBtn("Roll",  C["roll"])
        self._btn_pitch = ToggleBtn("Pitch", C["pitch"])
        self._btn_yaw   = ToggleBtn("Yaw",   C["yaw"])
        self._btn_kg    = ToggleBtn("Kg",    C["kg"])
        self._btn_fit   = _FitBtn()
        hint = QLabel("   arrastar nos gráficos ou slider  ·  ângulos desembrulhados (unwrap)")
        hint.setStyleSheet("color:#888;font-size:10px;")
        for b in [self._btn_roll, self._btn_pitch, self._btn_yaw,
                  QLabel(" | "), self._btn_kg,
                  QLabel(" | "), self._btn_fit]:
            tbar.addWidget(b)
        tbar.addWidget(hint)
        tbar.addStretch()
        lay.addLayout(tbar)

        # ── splitter vertical: RPY | Kg | 3D ─────────────────────────────────
        vsplit = QSplitter(Qt.Orientation.Vertical)
        lay.addWidget(vsplit, stretch=1)

        def _make_plot_widget(fig, canvas):
            w = QWidget(); wl = QVBoxLayout(w)
            wl.setContentsMargins(0, 0, 0, 0)
            wl.addWidget(canvas); return w

        # Gráfico RPY (sem toolbar, sem SpanSelector — drag = scrub)
        self._fig_rpy = Figure(figsize=(11, 2.5))
        self._fig_rpy.subplots_adjust(left=0.07, right=0.97, top=0.87, bottom=0.22)
        self._ax_rpy  = self._fig_rpy.add_subplot(111)
        self._cv_rpy  = FigureCanvasQTAgg(self._fig_rpy)
        vsplit.addWidget(_make_plot_widget(self._fig_rpy, self._cv_rpy))

        # Gráfico Kg (sincronizado — drag = scrub)
        self._fig_kg2 = Figure(figsize=(11, 2.0))
        self._fig_kg2.subplots_adjust(left=0.07, right=0.97, top=0.85, bottom=0.25)
        self._ax_kg2  = self._fig_kg2.add_subplot(111)
        self._cv_kg2  = FigureCanvasQTAgg(self._fig_kg2)
        vsplit.addWidget(_make_plot_widget(self._fig_kg2, self._cv_kg2))

        # Modelo 3D
        self._fig_3d = Figure(figsize=(11, 3.5))
        self._fig_3d.subplots_adjust(left=0, right=1, top=1, bottom=0)
        self._ax_3d  = self._fig_3d.add_subplot(111, projection="3d")
        self._cv_3d  = FigureCanvasQTAgg(self._fig_3d)
        vsplit.addWidget(_make_plot_widget(self._fig_3d, self._cv_3d))

        vsplit.setSizes([220, 160, 320])

        # ── slider scrubber ───────────────────────────────────────────────────
        s_row = QHBoxLayout()
        self._lbl_s0  = QLabel("0 s");  self._lbl_s0.setStyleSheet("font-size:10px;color:#888;")
        self._lbl_s1  = QLabel("0 s");  self._lbl_s1.setStyleSheet("font-size:10px;color:#888;")
        self._slider  = QSlider(Qt.Orientation.Horizontal)
        self._slider.setMinimum(0); self._slider.setMaximum(0)
        self._slider.setTracking(True)
        s_row.addWidget(self._lbl_s0)
        s_row.addWidget(self._slider, stretch=1)
        s_row.addWidget(self._lbl_s1)
        lay.addLayout(s_row)

        # ── controlos de play ─────────────────────────────────────────────────
        ctrl = QHBoxLayout()
        self._btn_play = QPushButton("▶  Play")
        self._btn_play.setFixedHeight(28); self._btn_play.setCheckable(True)
        self._btn_play.clicked.connect(self._toggle_play)
        ctrl.addWidget(self._btn_play)

        ctrl.addWidget(QLabel("  Velocidade:"))
        self._speed_group: list[QPushButton] = []
        for lbl, spd in [("0.5×", 0.5), ("1×", 1.0), ("2×", 2.0), ("5×", 5.0)]:
            b = QPushButton(lbl)
            b.setFixedHeight(24); b.setFixedWidth(42)
            b.setCheckable(True); b.setChecked(spd == 1.0)
            b.clicked.connect(lambda _, s=spd: self._set_speed(s))
            ctrl.addWidget(b); self._speed_group.append(b)
        ctrl.addStretch()
        self._lbl_time = QLabel("t = 0.00 s")
        self._lbl_time.setStyleSheet("font-size:11px;color:#555;min-width:90px;")
        ctrl.addWidget(self._lbl_time)
        lay.addLayout(ctrl)

        # ── estado interno ────────────────────────────────────────────────────
        self._df:       pd.DataFrame | None = None
        self._x:        np.ndarray   | None = None
        # scatter + smooth lines para RPY e Kg
        self._sc_roll   = self._sc_pitch = self._sc_yaw = self._sc_kg_line = None
        self._ln_roll   = self._ln_pitch = self._ln_yaw = self._ln_kg2    = None
        # ângulos suavizados usados no modelo 3D (elimina ruído e teleports)
        self._roll_s:  np.ndarray | None = None
        self._pitch_s: np.ndarray | None = None
        self._yaw_s:   np.ndarray | None = None
        self._vl_rpy    = self._vl_kg = None
        self._dragging  = False
        self._speed     = 1.0
        self._timer     = QTimer()
        self._timer.setInterval(33)
        self._timer.timeout.connect(self._on_tick)
        self._play_wall0 = self._play_data0 = 0.0

        # toggle connections (via _set_vis_rpy / _set_vis_kg que respeitam fit mode)
        self._btn_roll.toggled.connect(lambda v: self._set_vis_rpy("roll",  v))
        self._btn_pitch.toggled.connect(lambda v: self._set_vis_rpy("pitch", v))
        self._btn_yaw.toggled.connect(lambda v: self._set_vis_rpy("yaw",   v))
        self._btn_kg.toggled.connect(self._set_vis_kg)
        self._btn_fit.toggled.connect(self._on_fit)

        # slider
        self._slider.valueChanged.connect(self._on_slider)

        # mouse scrub: RPY canvas
        for sig, fn in [("button_press_event",   self._on_press),
                        ("motion_notify_event",   self._on_motion),
                        ("button_release_event",  self._on_release)]:
            self._cv_rpy.mpl_connect(sig, fn)
        # mouse scrub: Kg canvas
        for sig, fn in [("button_press_event",   self._on_press),
                        ("motion_notify_event",   self._on_motion),
                        ("button_release_event",  self._on_release)]:
            self._cv_kg2.mpl_connect(sig, fn)

    # ── helpers de visibilidade ───────────────────────────────────────────────

    def _set_vis_rpy(self, axis: str, v: bool):
        show_line = self._btn_fit.isChecked()
        sc = getattr(self, f"_sc_{axis}", None)
        ln = getattr(self, f"_ln_{axis}", None)
        if sc: sc.set_visible(v and not show_line)
        if ln: ln.set_visible(v and show_line)
        self._cv_rpy.draw_idle()

    def _set_vis_kg(self, v: bool):
        show_line = self._btn_fit.isChecked()
        if self._sc_kg_line: self._sc_kg_line.set_visible(v and not show_line)
        if self._ln_kg2:     self._ln_kg2.set_visible(v and show_line)
        self._cv_kg2.draw_idle()

    def _on_fit(self, show_line: bool):
        self._btn_fit.setText("Pontos" if show_line else "Linha")
        for axis, btn in [("roll", self._btn_roll),
                          ("pitch", self._btn_pitch),
                          ("yaw",   self._btn_yaw)]:
            v = btn.isChecked()
            sc = getattr(self, f"_sc_{axis}", None)
            ln = getattr(self, f"_ln_{axis}", None)
            if sc: sc.set_visible(v and not show_line)
            if ln: ln.set_visible(v and show_line)
        v = self._btn_kg.isChecked()
        if self._sc_kg_line: self._sc_kg_line.set_visible(v and not show_line)
        if self._ln_kg2:     self._ln_kg2.set_visible(v and show_line)
        self._cv_rpy.draw_idle()
        self._cv_kg2.draw_idle()

    def _update_3d(self, idx: int):
        """Usa ângulos suavizados → sem teleports por ruído ou wrap."""
        if self._roll_s is None:
            return
        draw_prosthetic(self._ax_3d,
                        float(self._roll_s[idx]),
                        float(self._pitch_s[idx]),
                        float(self._yaw_s[idx]))
        self._cv_3d.draw_idle()

    def _set_idx(self, idx: int):
        if self._x is None:
            return
        idx = max(0, min(idx, len(self._x) - 1))
        t = float(self._x[idx])
        if self._vl_rpy: self._vl_rpy.set_xdata([t, t])
        if self._vl_kg:  self._vl_kg.set_xdata([t, t])
        self._cv_rpy.draw_idle()
        self._cv_kg2.draw_idle()
        self._update_3d(idx)
        self._lbl_time.setText(f"t = {t:.2f} s")

    # ── carga de dados ────────────────────────────────────────────────────────

    def load(self, df: pd.DataFrame):
        self._stop_play()
        self._df = df
        x  = (df["sample_us"] - df["sample_us"].iloc[0]) / 1e6
        self._x = x.to_numpy()
        w  = max(5, len(df) // 60)
        sl = self._btn_fit.isChecked()

        # ── desembrulhar (unwrap) ângulos de Euler ────────────────────────────
        # np.unwrap remove descontinuidades de ±360° → curvas contínuas
        def _uw(col: str) -> np.ndarray:
            return (np.degrees(np.unwrap(np.radians(df[col].to_numpy(float))))
                    if col in df.columns else np.zeros(len(df)))

        roll_uw  = _uw("roll_deg")
        pitch_uw = _uw("pitch_deg")
        yaw_uw   = _uw("yaw_deg")

        # ângulos suavizados (média rolante) → usados no modelo 3D
        self._roll_s  = _smooth(roll_uw,  w)
        self._pitch_s = _smooth(pitch_uw, w)
        self._yaw_s   = _smooth(yaw_uw,   w)

        # ── Gráfico RPY ────────────────────────────────────────────────────────
        self._ax_rpy.cla()
        for uw_arr, btn, color, lbl, sc_attr, ln_attr in [
            (roll_uw,  self._btn_roll,  C["roll"],  "Roll",  "_sc_roll",  "_ln_roll"),
            (pitch_uw, self._btn_pitch, C["pitch"], "Pitch", "_sc_pitch", "_ln_pitch"),
            (yaw_uw,   self._btn_yaw,   C["yaw"],   "Yaw",   "_sc_yaw",   "_ln_yaw"),
        ]:
            sc = self._ax_rpy.scatter(
                x, uw_arr, s=_SCATTER_PT, c=color, label=lbl, alpha=0.85, linewidths=0)
            sc.set_visible(btn.isChecked() and not sl)
            setattr(self, sc_attr, sc)

            ln, = self._ax_rpy.plot(
                x, _smooth(uw_arr, w), color=color, linewidth=1.4, alpha=0.9)
            ln.set_visible(btn.isChecked() and sl)
            setattr(self, ln_attr, ln)

        self._ax_rpy.set_ylabel("Ângulo (°)", fontsize=9)
        self._ax_rpy.set_xlabel("Tempo (s)", fontsize=9)
        self._ax_rpy.legend(loc="upper right", fontsize=8, markerscale=4)
        self._ax_rpy.grid(True, linestyle="--", alpha=0.3)
        self._ax_rpy.set_title(
            "Roll / Pitch / Yaw  (unwrapped — sem descontinuidades)", fontsize=9)
        self._vl_rpy = self._ax_rpy.axvline(
            x=float(self._x[0]), color="black", lw=1.5, ls="--", alpha=0.7, zorder=5)

        # ── Gráfico Kg ─────────────────────────────────────────────────────────
        self._ax_kg2.cla()
        self._sc_kg_line = self._ln_kg2 = None
        if "load_cell_est_kg" in df.columns:
            y_kg = df["load_cell_est_kg"].to_numpy(float)
            sc = self._ax_kg2.scatter(
                x, y_kg, s=_SCATTER_PT, c=C["kg"], label="Kg", alpha=0.85, linewidths=0)
            sc.set_visible(self._btn_kg.isChecked() and not sl)
            self._sc_kg_line = sc

            ln, = self._ax_kg2.plot(
                x, _smooth(y_kg, w), color=C["kg"], linewidth=1.4, alpha=0.9)
            ln.set_visible(self._btn_kg.isChecked() and sl)
            self._ln_kg2 = ln

        self._ax_kg2.set_ylabel("Peso (kg)", fontsize=9, color=C["kg"])
        self._ax_kg2.set_xlabel("Tempo (s)", fontsize=9)
        self._ax_kg2.tick_params(axis="y", labelcolor=C["kg"])
        self._ax_kg2.grid(True, linestyle="--", alpha=0.3)
        self._ax_kg2.set_title("Força (kg)", fontsize=10)
        self._vl_kg = self._ax_kg2.axvline(
            x=float(self._x[0]), color="black", lw=1.5, ls="--", alpha=0.7, zorder=5)

        # slider
        self._slider.setMaximum(len(self._x) - 1)
        self._slider.setValue(0)
        self._lbl_s1.setText(f"{float(self._x[-1]):.1f} s")

        self._cv_rpy.draw_idle()
        self._cv_kg2.draw_idle()
        self._update_3d(0)

    # ── mouse scrubber nos gráficos 2D ────────────────────────────────────────

    def _x_from_event(self, event) -> float | None:
        """Devolve o xdata se o evento foi num eixo 2D conhecido."""
        if event.inaxes in (self._ax_rpy, self._ax_kg2) and event.xdata is not None:
            return float(event.xdata)
        return None

    def _on_press(self, event):
        xd = self._x_from_event(event)
        if xd is None or self._x is None:
            return
        self._dragging = True
        idx = int(np.searchsorted(self._x, xd))
        self._slider.setValue(max(0, min(idx, len(self._x) - 1)))

    def _on_motion(self, event):
        if not self._dragging:
            return
        xd = self._x_from_event(event)
        if xd is None or self._x is None:
            return
        idx = int(np.searchsorted(self._x, xd))
        self._slider.setValue(max(0, min(idx, len(self._x) - 1)))

    def _on_release(self, event):
        self._dragging = False

    # ── slider ────────────────────────────────────────────────────────────────

    def _on_slider(self, idx: int):
        self._set_idx(idx)

    # ── play / pause ──────────────────────────────────────────────────────────

    def _toggle_play(self, checked: bool):
        self._start_play() if checked else self._stop_play()

    def _start_play(self):
        if self._x is None:
            self._btn_play.setChecked(False); return
        cur = self._slider.value()
        if cur >= len(self._x) - 1:
            cur = 0; self._slider.setValue(0)
        self._play_data0 = float(self._x[cur])
        self._play_wall0 = self._time_mod.monotonic()
        self._btn_play.setText("⏸  Pausa")
        self._timer.start()

    def _stop_play(self):
        self._timer.stop()
        self._btn_play.setChecked(False)
        self._btn_play.setText("▶  Play")

    def _on_tick(self):
        if self._x is None:
            self._stop_play(); return
        data_t = self._play_data0 + (self._time_mod.monotonic() - self._play_wall0) * self._speed
        if data_t >= float(self._x[-1]):
            self._slider.setValue(len(self._x) - 1); self._stop_play(); return
        idx = min(int(np.searchsorted(self._x, data_t)), len(self._x) - 1)
        self._slider.setValue(idx)

    def _set_speed(self, spd: float):
        self._speed = spd
        for b in self._speed_group:
            b.setChecked(False)
        if self._timer.isActive() and self._x is not None:
            cur = self._slider.value()
            self._play_data0 = float(self._x[cur])
            self._play_wall0 = self._time_mod.monotonic()


# ══════════════════════════════════════════════════════════════════════════
#  Janela principal
# ══════════════════════════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("petBionic CSV Analyzer")
        self.setMinimumSize(1280, 780)

        # splitter horizontal: browser | conteúdo
        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.setCentralWidget(splitter)

        # --- painel esquerdo: browser ---------------------------------------
        self._browser = FileBrowserPanel()
        self._browser.file_selected.connect(self._load)
        splitter.addWidget(self._browser)

        # --- painel direito: info + tabs ------------------------------------
        right = QWidget()
        r_lay = QVBoxLayout(right)
        r_lay.setContentsMargins(4, 4, 4, 4)
        r_lay.setSpacing(4)

        self._info = QLabel("Faça duplo-clique num ficheiro para carregar")
        self._info.setStyleSheet(
            "color:#666;font-size:11px;padding:4px 6px;"
            "border-bottom:1px solid #ddd;")
        r_lay.addWidget(self._info)

        tabs = QTabWidget()

        self._tab_lc  = LoadCellTab()
        self._tab_acc = ImuTab(
            cols=("imu_ax", "imu_ay", "imu_az"),
            labels=("ax", "ay", "az"),
            title="Acelerómetro",
            ylabel="Aceleração (counts)",
        )
        self._tab_gyr = ImuTab(
            cols=("imu_gx", "imu_gy", "imu_gz"),
            labels=("gx", "gy", "gz"),
            title="Giroscópio",
            ylabel="Velocidade angular (counts)",
        )
        self._tab_mag = ImuTab(
            cols=("imu_mx", "imu_my", "imu_mz"),
            labels=("mx", "my", "mz"),
            title="Magnetómetro",
            ylabel="Campo magnético (counts)",
        )
        # Orientação — análise (zoom + unwrap, igual às outras tabs IMU)
        self._tab_ori_ana = ImuTab(
            cols=("roll_deg", "pitch_deg", "yaw_deg"),
            labels=("Roll", "Pitch", "Yaw"),
            title="Orientação — Análise",
            ylabel="Ângulo (°)",
            show_magnitude=False,
            unwrap=True,
        )
        # Tab principal: orientação 3D + Kg + scrubber + play
        self._tab_ori_3d = Orientation3DTab()

        # "Main" é a primeira tab (interface principal de análise)
        tabs.addTab(self._tab_ori_3d,  "Main")
        tabs.addTab(self._tab_lc,      "Célula de Carga")
        tabs.addTab(self._tab_acc,     "Acelerómetro")
        tabs.addTab(self._tab_gyr,     "Giroscópio")
        tabs.addTab(self._tab_mag,     "Magnetómetro")
        tabs.addTab(self._tab_ori_ana, "Orientação — Análise")

        r_lay.addWidget(tabs)
        splitter.addWidget(right)
        splitter.setSizes([260, 1020])

        self.statusBar().showMessage("Pronto")

    # ── carregamento ─────────────────────────────────────────────────────────

    def _load(self, path: str):
        try:
            df = pd.read_csv(path)
            df.columns = df.columns.str.strip()

            if "sample_us" not in df.columns:
                raise ValueError("Coluna 'sample_us' não encontrada — ficheiro inválido.")

            fname = Path(path).name
            n     = len(df)
            dur   = (df["sample_us"].iloc[-1] - df["sample_us"].iloc[0]) / 1e6

            self._info.setText(
                f"<b>{fname}</b>  &nbsp;·&nbsp;  {n:,} amostras  "
                f"&nbsp;·&nbsp;  {dur:.2f} s"
            )
            self._info.setStyleSheet(
                "color:#111;font-size:11px;padding:4px 6px;"
                "border-bottom:1px solid #ddd;")
            self.setWindowTitle(f"petBionic — {fname}")

            self._tab_lc.load(df)
            self._tab_acc.load(df)
            self._tab_gyr.load(df)
            self._tab_mag.load(df)
            self._tab_ori_ana.load(df)
            self._tab_ori_3d.load(df)

            self.statusBar().showMessage(
                f"Carregado: {fname}  |  {n:,} amostras  |  {dur:.2f} s")

        except Exception as exc:
            QMessageBox.critical(self, "Erro ao carregar", str(exc))


# ══════════════════════════════════════════════════════════════════════════
#  Entrada
# ══════════════════════════════════════════════════════════════════════════

def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = MainWindow()
    if len(sys.argv) > 1 and Path(sys.argv[1]).exists():
        win._load(sys.argv[1])
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
