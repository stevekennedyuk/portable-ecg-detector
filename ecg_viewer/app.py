from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

from .detector import DetectionEvent, analyse_all_leads
from .record import EcgRecord, load_record


EVENT_COLOURS = {
    1 << 0: "#9ca3af", 1 << 1: "#64748b", 1 << 2: "#60a5fa",
    1 << 3: "#fbbf24", 1 << 4: "#fb923c", 1 << 5: "#e879f9",
    1 << 6: "#ef4444", 1 << 7: "#dc2626", 1 << 8: "#facc15",
    1 << 9: "#a78bfa", 1 << 10: "#2dd4bf", 1 << 11: "#c084fc",
    1 << 12: "#d946ef", 1 << 13: "#f472b6", 1 << 14: "#fb7185",
}


def run_viewer(record: EcgRecord, detections: list[list[DetectionEvent]]) -> int:
    try:
        import pyqtgraph as pg
        from PySide6 import QtCore, QtWidgets
    except ImportError as exc:
        raise RuntimeError(
            "The viewer requires PySide6 and pyqtgraph. Run: "
            "python3 -m pip install -r requirements-macos.txt") from exc

    pg.setConfigOptions(antialias=False, background="#071018", foreground="#cbd5e1")

    class MainWindow(QtWidgets.QMainWindow):
        def __init__(self) -> None:
            super().__init__()
            self.setWindowTitle(f"Portable ECG — {record.source.name}")
            self.resize(1400, 900)
            self.window_seconds = 10

            root = QtWidgets.QWidget()
            layout = QtWidgets.QVBoxLayout(root)
            controls = QtWidgets.QHBoxLayout()
            self.play_button = QtWidgets.QPushButton("Play")
            self.play_button.setCheckable(True)
            self.play_button.toggled.connect(self._toggle_play)
            controls.addWidget(self.play_button)
            controls.addWidget(QtWidgets.QLabel("Window"))
            self.window_box = QtWidgets.QComboBox()
            self.window_box.addItems(["5 s", "10 s", "20 s", "30 s"])
            self.window_box.setCurrentText("10 s")
            self.window_box.currentTextChanged.connect(self._change_window)
            controls.addWidget(self.window_box)
            self.position_label = QtWidgets.QLabel()
            controls.addWidget(self.position_label)
            controls.addStretch(1)
            self.event_label = QtWidgets.QLabel()
            controls.addWidget(self.event_label)
            layout.addLayout(controls)

            self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
            self.slider.valueChanged.connect(self.render)
            layout.addWidget(self.slider)

            self.graphics = pg.GraphicsLayoutWidget()
            self.graphics.setMinimumHeight(max(260, 180 * len(record.lead_names)))
            scroll = QtWidgets.QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setWidget(self.graphics)
            layout.addWidget(scroll, 1)
            self.setCentralWidget(root)

            self.plots = []
            for index, lead_name in enumerate(record.lead_names):
                plot = self.graphics.addPlot(row=index, col=0)
                plot.setLabel("left", lead_name, units="µV")
                plot.showGrid(x=True, y=True, alpha=0.16)
                plot.setMouseEnabled(x=False, y=True)
                if index == len(record.lead_names) - 1:
                    plot.setLabel("bottom", "Time", units="s")
                self.plots.append(plot)

            self.timer = QtCore.QTimer(self)
            self.timer.setInterval(100)
            self.timer.timeout.connect(self._advance)
            self._configure_slider()
            self.render(0)

        def _configure_slider(self) -> None:
            maximum = max(0, record.signals_uv.shape[0] -
                          self.window_seconds * record.sample_rate_hz)
            self.slider.setRange(0, maximum)
            self.slider.setSingleStep(max(1, record.sample_rate_hz // 10))
            self.slider.setPageStep(self.window_seconds * record.sample_rate_hz)

        def _change_window(self, text: str) -> None:
            self.window_seconds = int(text.split()[0])
            self._configure_slider()
            self.render(self.slider.value())

        def _toggle_play(self, playing: bool) -> None:
            self.play_button.setText("Pause" if playing else "Play")
            if playing:
                self.timer.start()
            else:
                self.timer.stop()

        def _advance(self) -> None:
            step = max(1, record.sample_rate_hz // 10)
            value = self.slider.value() + step
            if value > self.slider.maximum():
                self.play_button.setChecked(False)
                value = self.slider.maximum()
            self.slider.setValue(value)

        def render(self, start: int) -> None:
            stop = min(record.signals_uv.shape[0],
                       start + self.window_seconds * record.sample_rate_hz)
            times = np.arange(start, stop, dtype=np.float64) / record.sample_rate_hz
            visible_names: set[str] = set()
            for lead, plot in enumerate(self.plots):
                plot.clear()
                signal = record.signals_uv[start:stop, lead]
                plot.plot(times, signal, pen=pg.mkPen("#55d6be", width=1.2))
                lead_events = [event for event in detections[lead]
                               if start <= event.sample < stop]
                qrs = [event for event in lead_events if event.event_bit == 1]
                if qrs:
                    samples = np.asarray([event.sample for event in qrs], dtype=int)
                    plot.addItem(pg.ScatterPlotItem(
                        x=samples / record.sample_rate_hz,
                        y=record.signals_uv[samples, lead], size=5,
                        brush=pg.mkBrush(EVENT_COLOURS[1]), pen=None))
                for event in lead_events:
                    if event.event_bit == 1:
                        continue
                    visible_names.add(event.name)
                    line = pg.InfiniteLine(
                        pos=event.sample / record.sample_rate_hz, angle=90,
                        pen=pg.mkPen(EVENT_COLOURS[event.event_bit], width=1.5),
                        label=event.name,
                        labelOpts={"position": 0.92, "color": EVENT_COLOURS[event.event_bit]})
                    plot.addItem(line)
                plot.setXRange(start / record.sample_rate_hz,
                               stop / record.sample_rate_hz, padding=0)
            self.position_label.setText(
                f"{start / record.sample_rate_hz:.1f}–{stop / record.sample_rate_hz:.1f} s "
                f"of {record.duration_seconds:.1f} s · {record.sample_rate_hz} Hz")
            self.event_label.setText("Events: " +
                                     (", ".join(sorted(visible_names))
                                      if visible_names else "none in window"))

    qt_app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return qt_app.exec()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Display and analyse WFDB or EDF ECGs")
    parser.add_argument("record", nargs="?", help="WFDB .hea/base name or EDF file")
    parser.add_argument("--mains", type=int, choices=(0, 50, 60), default=50,
                        help="notch frequency; 0 disables it (default: 50)")
    args = parser.parse_args(argv)
    path = args.record
    if path is None:
        try:
            from PySide6 import QtWidgets
        except ImportError as exc:
            raise RuntimeError("Install requirements-macos.txt before using the file dialog") from exc
        qt_app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv)
        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            None, "Open ECG", str(Path.home()),
            "ECG records (*.hea *.edf);;WFDB headers (*.hea);;EDF files (*.edf)")
        if not path:
            return 0
    record = load_record(path)
    detections = analyse_all_leads(record.signals_uv, record.sample_rate_hz,
                                   args.mains)
    return run_viewer(record, detections)
