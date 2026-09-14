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
    normalized = unit.strip().replace("µ", "u").lower()
    scales = {"v": 1_000_000.0, "mv": 1_000.0, "uv": 1.0, "nv": 0.001}
    if normalized not in scales:
        raise ValueError(f"Unsupported ECG unit {unit!r}; expected V, mV, uV or nV")
    return np.asarray(values, dtype=np.float32) * scales[normalized]


def _validated_rate(value: float) -> int:
    rounded = int(round(value))
    if abs(value - rounded) > 1e-6:
        raise ValueError(f"The C detector requires an integer sample rate, got {value}")
    if not 125 <= rounded <= 1000:
        raise ValueError(f"The detector supports 125..1000 Hz, got {rounded} Hz")
    return rounded


def _load_wfdb(path: Path) -> EcgRecord:
    try:
        import wfdb
    except ImportError as exc:
        raise RuntimeError("WFDB input requires: python3 -m pip install wfdb") from exc

    record_name = str(path.with_suffix("")) if path.suffix.lower() == ".hea" else str(path)
    record = wfdb.rdrecord(record_name, physical=True)
    if record.p_signal is None:
        raise ValueError("WFDB record has no physical signal data")
    units = record.units or ["mV"] * record.n_sig
    columns = [_scale_to_uv(record.p_signal[:, i], units[i])
               for i in range(record.n_sig)]
    names = tuple(record.sig_name or [f"Lead {i + 1}" for i in range(record.n_sig)])
    return EcgRecord(np.column_stack(columns), _validated_rate(float(record.fs)),
                     names, path)


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
        return EcgRecord(np.column_stack(columns), _validated_rate(float(rates[0])),
                         names, path)
    finally:
        reader.close()


def load_record(path: str | Path) -> EcgRecord:
    source = Path(path).expanduser().resolve()
    suffix = source.suffix.lower()
    if suffix in {".edf", ".edf+"}:
        return _load_edf(source)
    if suffix == ".hea" or source.with_suffix(".hea").exists():
        return _load_wfdb(source)
    raise ValueError("Unsupported ECG file. Select a WFDB .hea record or EDF/EDF+ file.")
