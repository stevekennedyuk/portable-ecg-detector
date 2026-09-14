from __future__ import annotations

import math
import unittest

import numpy as np

from ecg_viewer.detector import CDetector


class CDetectorBridgeTests(unittest.TestCase):
    def test_regular_waveform_produces_qrs_events(self) -> None:
        sample_rate = 250
        samples = np.zeros(12 * sample_rate, dtype=np.float32)
        for beat in range(12):
            center = beat * sample_rate + sample_rate // 5
            for offset in range(-8, 9):
                index = center + offset
                if 0 <= index < len(samples):
                    samples[index] += 1100.0 * (1.0 - abs(offset) / 9.0)
        time = np.arange(len(samples), dtype=np.float32) / sample_rate
        samples += 35.0 * np.sin(2.0 * math.pi * 0.25 * time)

        events = CDetector(sample_rate, mains_hz=0).process(samples)
        qrs_events = [event for event in events if event.name == "QRS"]
        self.assertGreaterEqual(len(qrs_events), 8)
        self.assertLessEqual(len(qrs_events), 12)


if __name__ == "__main__":
    unittest.main()
