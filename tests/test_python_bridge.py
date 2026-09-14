from __future__ import annotations

import math
import unittest

import numpy as np

from ecg_viewer.detector import CDetector


class CDetectorBridgeTests(unittest.TestCase):
    def test_rejects_nonfinite_and_malformed_input(self) -> None:
        detector = CDetector(250, mains_hz=0)
        with self.assertRaises(ValueError):
            detector.process(np.array([0.0, np.nan], dtype=np.float32))
        with self.assertRaises(ValueError):
            detector.process(np.zeros((4, 2), dtype=np.float32))
        with self.assertRaises(ValueError):
            detector.process(np.zeros(4, dtype=np.float32), chunk_size=0)

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
        phase = np.mod(time, 1.0)
        samples += 100.0 * np.exp(-0.5 * ((phase - 0.08) / 0.025) ** 2)
        samples += 240.0 * np.exp(-0.5 * ((phase - 0.48) / 0.060) ** 2)

        events = CDetector(sample_rate, mains_hz=0).process(samples)
        qrs_events = [event for event in events if event.name == "QRS"]
        p_events = [event for event in events if event.name == "P wave"]
        t_events = [event for event in events if event.name == "T wave"]
        self.assertGreaterEqual(len(qrs_events), 8)
        self.assertLessEqual(len(qrs_events), 12)
        self.assertGreaterEqual(len(p_events), 7)
        self.assertGreaterEqual(len(t_events), 7)
        self.assertTrue(all(event.sample >= 0 for event in p_events + t_events))


if __name__ == "__main__":
    unittest.main()
