from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class EcgRecord:
    signals_uv: np.ndarray
    sample_rate_hz: int
    lead_names: tuple[str, ...]
    source: Path

    @property
    def duration_seconds(self) -> float:
        return self.signals_uv.shape[0] / self.sample_rate_hz


def _scale_to_uv(values: np.ndarray, unit: str) -> np.ndarray:
    normalized = unit.strip().replace("µ", "u").replace("μ", "u").lower()
    scales = {"v": 1_000_000.0, "mv": 1_000.0, "uv": 1.0, "nv": 0.001}
    if normalized not in scales:
        raise ValueError(f"Unsupported ECG unit {unit!r}; expected V, mV, uV or nV")
    source = np.asarray(values, dtype=np.float64)
    if source.ndim != 1 or source.size == 0:
        raise ValueError("Each ECG lead must be a non-empty one-dimensional signal")
    scaled = source * scales[normalized]
    if not np.all(np.isfinite(scaled)):
        raise ValueError("ECG signal contains NaN, infinity, or an out-of-range value")
    limit = np.finfo(np.float32).max
    if np.max(np.abs(scaled)) > limit:
        raise ValueError("ECG signal exceeds the detector numeric range")
    return scaled.astype(np.float32)


def _validated_rate(value: float) -> int:
    if not np.isfinite(value):
        raise ValueError("ECG sample rate must be finite")
    rounded = int(round(value))
    if abs(value - rounded) > 1e-6:
        raise ValueError(f"The C detector requires an integer sample rate, got {value}")
    if not 125 <= rounded <= 1000:
        raise ValueError(f"The detector supports 125..1000 Hz, got {rounded} Hz")
    return rounded


def _make_record(columns: list[np.ndarray], rate: float,
                 names: tuple[str, ...], source: Path) -> EcgRecord:
    if not columns or len(columns) != len(names):
        raise ValueError("ECG channel metadata does not match the signal data")
    lengths = {len(column) for column in columns}
    if len(lengths) != 1:
        raise ValueError("All ECG leads must contain the same number of samples")
    stripped_names = tuple(name.strip() for name in names)
    if any(not name for name in stripped_names):
        raise ValueError("Every ECG channel must have a non-empty name")
    occurrences: dict[str, int] = {}
    unique_names = []
    for name in stripped_names:
        occurrences[name] = occurrences.get(name, 0) + 1
        suffix = f" [{occurrences[name]}]" if occurrences[name] > 1 else ""
        unique_names.append(name + suffix)
    cleaned_names = tuple(unique_names)
    signals = np.ascontiguousarray(np.column_stack(columns), dtype=np.float32)
    if signals.ndim != 2 or signals.shape[0] == 0 or signals.shape[1] == 0:
        raise ValueError("ECG record contains no signal samples")
    if not np.all(np.isfinite(signals)):
        raise ValueError("ECG record contains non-finite signal samples")
    return EcgRecord(signals, _validated_rate(float(rate)), cleaned_names, source)


def _load_wfdb(path: Path) -> EcgRecord:
    try:
        import wfdb
    except ImportError as exc:
        raise RuntimeError("WFDB input requires: python3 -m pip install wfdb") from exc

    record_name = str(path.with_suffix("")) if path.suffix.lower() == ".hea" else str(path)
    record = wfdb.rdrecord(record_name, physical=True)
    if record.p_signal is None:
        raise ValueError("WFDB record has no physical signal data")
    if not record.units or len(record.units) != record.n_sig:
        raise ValueError("WFDB record must declare physical units for every channel")
    units = record.units
    columns = [_scale_to_uv(record.p_signal[:, i], units[i])
               for i in range(record.n_sig)]
    names = tuple(record.sig_name or [f"Lead {i + 1}" for i in range(record.n_sig)])
    return _make_record(columns, float(record.fs), names, path)


def _load_edf(path: Path) -> EcgRecord:
    try:
        import pyedflib
    except ImportError as exc:
        raise RuntimeError("EDF input requires: python3 -m pip install pyedflib") from exc

    reader = pyedflib.EdfReader(str(path))
    try:
        rates = np.asarray(reader.getSampleFrequencies(), dtype=float)
        if rates.size == 0 or not np.allclose(rates, rates[0]):
            raise ValueError("All displayed EDF ECG channels must have the same sample rate")
        names = tuple(str(value).strip() for value in reader.getSignalLabels())
        columns = []
        for index in range(reader.signals_in_file):
            columns.append(_scale_to_uv(reader.readSignal(index),
                                        reader.getPhysicalDimension(index)))
        return _make_record(columns, float(rates[0]), names, path)
    finally:
        reader.close()


def load_record(path: str | Path) -> EcgRecord:
    source = Path(path).expanduser().resolve()
    suffix = source.suffix.lower()
    if not source.exists():
        header = source.with_suffix(".hea")
        if header.exists() and header.is_file():
            source = header
            suffix = ".hea"
        else:
            raise FileNotFoundError(f"ECG record does not exist: {source}")
    if not source.is_file():
        raise ValueError(f"ECG record is not a regular file: {source}")
    if suffix in {".edf", ".edf+"}:
        return _load_edf(source)
    if suffix == ".hea" or source.with_suffix(".hea").exists():
        return _load_wfdb(source)
    raise ValueError("Unsupported ECG file. Select a WFDB .hea record or EDF/EDF+ file.")
