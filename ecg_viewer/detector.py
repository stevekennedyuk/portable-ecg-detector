from __future__ import annotations

import ctypes
import platform
import subprocess
from dataclasses import dataclass
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"

EVENT_NAMES = {
    1 << 0: "QRS",
    1 << 1: "Poor signal",
    1 << 2: "Bradycardia",
    1 << 3: "Tachycardia",
    1 << 4: "AF candidate",
    1 << 5: "VT candidate",
    1 << 6: "VF candidate",
    1 << 7: "Asystole candidate",
    1 << 8: "SVT candidate",
    1 << 9: "Pause",
    1 << 10: "PAC candidate",
    1 << 11: "PVC candidate",
    1 << 12: "Ventricular couplet",
    1 << 13: "Bigeminy candidate",
    1 << 14: "Trigeminy candidate",
}

RHYTHM_NAMES = (
    "Warm-up", "Unanalysable", "Unknown", "Regular",
    "Bradycardia candidate", "Tachycardia candidate", "AF candidate",
    "SVT candidate", "VT candidate", "VF candidate", "Asystole candidate",
)


class _Output(ctypes.Structure):
    _fields_ = [
        ("sample_index", ctypes.c_uint64),
        ("filtered_uv", ctypes.c_float),
        ("heart_rate_bpm", ctypes.c_float),
        ("last_rr_ms", ctypes.c_uint16),
        ("qrs_width_ms", ctypes.c_uint16),
        ("signal_quality", ctypes.c_uint8),
        ("rhythm", ctypes.c_uint8),
        ("beat_type", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8),
        ("active_events", ctypes.c_uint32),
        ("new_events", ctypes.c_uint32),
    ]


@dataclass(frozen=True)
class DetectionEvent:
    sample: int
    event_bit: int
    name: str
    heart_rate_bpm: float
    qrs_width_ms: int
    signal_quality: int
    rhythm: str


def _library_path() -> Path:
    extension = ".dylib" if platform.system() == "Darwin" else ".so"
    return BUILD_DIR / f"libecg_detector{extension}"


def build_library(force: bool = False) -> Path:
    output = _library_path()
    sources = [ROOT / "src/ecg_detector.c", ROOT / "src/ecg_detector_ffi.c",
               ROOT / "include/ecg_detector.h", ROOT / "include/ecg_detector_ffi.h"]
    stale = not output.exists() or any(item.stat().st_mtime > output.stat().st_mtime
                                       for item in sources)
    if force or stale:
        BUILD_DIR.mkdir(exist_ok=True)
        mode = ["-dynamiclib"] if platform.system() == "Darwin" else ["-shared", "-fPIC"]
        command = ["clang", *mode, "-O2", "-std=c99", "-Wall", "-Wextra",
                   "-I", str(ROOT / "include"), str(ROOT / "src/ecg_detector.c"),
                   str(ROOT / "src/ecg_detector_ffi.c"), "-lm", "-o", str(output)]
        subprocess.run(command, check=True)
    return output


class CDetector:
    def __init__(self, sample_rate_hz: int, mains_hz: int = 50):
        self.sample_rate_hz = sample_rate_hz
        self.library = ctypes.CDLL(str(build_library()))
        self.library.ecg_detector_ffi_state_size.restype = ctypes.c_size_t
        self.library.ecg_detector_ffi_state_alignment.restype = ctypes.c_size_t
        self.library.ecg_detector_ffi_init.argtypes = [ctypes.c_void_p,
                                                       ctypes.c_uint16,
                                                       ctypes.c_uint8]
        self.library.ecg_detector_ffi_init.restype = ctypes.c_int
        self.library.ecg_detector_ffi_process_buffer.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_size_t,
            ctypes.c_uint32, ctypes.POINTER(_Output)
        ]
        size = self.library.ecg_detector_ffi_state_size()
        alignment = self.library.ecg_detector_ffi_state_alignment()
        self._storage = ctypes.create_string_buffer(size + alignment - 1)
        base = ctypes.addressof(self._storage)
        self.state = ctypes.c_void_p(((base + alignment - 1) // alignment) * alignment)
        if not self.library.ecg_detector_ffi_init(self.state, sample_rate_hz,
                                                   mains_hz):
            raise ValueError("C detector rejected the sample rate or mains setting")

    def process(self, samples_uv: np.ndarray, chunk_size: int = 50_000) -> list[DetectionEvent]:
        values = np.ascontiguousarray(samples_uv, dtype=np.float32)
        events: list[DetectionEvent] = []
        for offset in range(0, len(values), chunk_size):
            chunk = values[offset:offset + chunk_size]
            outputs = (_Output * len(chunk))()
            pointer = chunk.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            self.library.ecg_detector_ffi_process_buffer(
                self.state, pointer, len(chunk), 0, outputs)
            for index, result in enumerate(outputs):
                bits = int(result.new_events)
                for bit, name in EVENT_NAMES.items():
                    if bits & bit:
                        event_sample = offset + index
                        if bit in {1 << 0, 1 << 10, 1 << 11, 1 << 12,
                                   1 << 13, 1 << 14}:
                            search_start = max(
                                0, event_sample - self.sample_rate_hz // 4)
                            search = values[search_start:event_sample + 1]
                            if len(search):
                                event_sample = search_start + int(
                                    np.argmax(np.abs(search)))
                        rhythm_index = int(result.rhythm)
                        rhythm = (RHYTHM_NAMES[rhythm_index]
                                  if rhythm_index < len(RHYTHM_NAMES) else "Invalid")
                        events.append(DetectionEvent(
                            sample=event_sample,
                            event_bit=bit,
                            name=name,
                            heart_rate_bpm=float(result.heart_rate_bpm),
                            qrs_width_ms=int(result.qrs_width_ms),
                            signal_quality=int(result.signal_quality),
                            rhythm=rhythm,
                        ))
        return events


def analyse_all_leads(signals_uv: np.ndarray, sample_rate_hz: int,
                      mains_hz: int = 50) -> list[list[DetectionEvent]]:
    return [CDetector(sample_rate_hz, mains_hz).process(signals_uv[:, lead])
            for lead in range(signals_uv.shape[1])]
