from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

from ecg_viewer.record import _make_record, _scale_to_uv, load_record


class RecordFormatTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.sample_rate = 250
        time = np.arange(2500, dtype=np.float64) / self.sample_rate
        lead_i = np.sin(2.0 * np.pi * time)
        self.signals_mv = np.column_stack((lead_i, 0.8 * lead_i))

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def test_wfdb_round_trip_and_unit_conversion(self) -> None:
        import wfdb

        wfdb.wrsamp(
            "sample", fs=self.sample_rate, units=["mV", "mV"],
            sig_name=["I", "II"], p_signal=self.signals_mv,
            write_dir=str(self.directory), fmt=["16", "16"])
        record = load_record(self.directory / "sample.hea")
        self.assertEqual(record.lead_names, ("I", "II"))
        self.assertEqual(record.sample_rate_hz, self.sample_rate)
        self.assertEqual(record.signals_uv.shape, (2500, 2))
        self.assertGreater(np.max(record.signals_uv[:, 0]), 990.0)

    def test_edf_round_trip_and_unit_conversion(self) -> None:
        from pyedflib import highlevel

        path = self.directory / "sample.edf"
        headers = highlevel.make_signal_headers(
            ["I", "II"], sample_frequency=self.sample_rate,
            physical_min=-2.0, physical_max=2.0,
            digital_min=-32768, digital_max=32767, dimension="mV")
        highlevel.write_edf(
            str(path), [self.signals_mv[:, 0], self.signals_mv[:, 1]],
            headers, digital=False)
        record = load_record(path)
        self.assertEqual(record.lead_names, ("I", "II"))
        self.assertEqual(record.sample_rate_hz, self.sample_rate)
        self.assertEqual(record.signals_uv.shape, (2500, 2))
        self.assertGreater(np.max(record.signals_uv[:, 0]), 990.0)

    def test_missing_record_and_invalid_values_are_rejected(self) -> None:
        with self.assertRaises(FileNotFoundError):
            load_record(self.directory / "missing.hea")
        with self.assertRaises(ValueError):
            _scale_to_uv(np.array([0.0, np.nan]), "mV")
        with self.assertRaises(ValueError):
            _scale_to_uv(np.array([0.0, np.inf]), "uV")
        with self.assertRaises(ValueError):
            _scale_to_uv(np.array([0.0]), "unknown")

    def test_wfdb_base_name_is_supported(self) -> None:
        import wfdb

        wfdb.wrsamp(
            "base", fs=self.sample_rate, units=["mV", "mV"],
            sig_name=["I", "II"], p_signal=self.signals_mv,
            write_dir=str(self.directory), fmt=["16", "16"])
        record = load_record(self.directory / "base")
        self.assertEqual(record.signals_uv.shape, (2500, 2))

    def test_duplicate_channel_names_are_disambiguated(self) -> None:
        columns = [np.zeros(10, dtype=np.float32),
                   np.ones(10, dtype=np.float32)]
        record = _make_record(columns, 250.0, ("ECG", "ECG"),
                              self.directory / "duplicate.hea")
        self.assertEqual(record.lead_names, ("ECG", "ECG [2]"))


if __name__ == "__main__":
    unittest.main()
