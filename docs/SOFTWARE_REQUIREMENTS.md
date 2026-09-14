# Software requirements baseline

Status: draft engineering baseline. Approval, ownership, safety classification,
and bidirectional traceability in the manufacturer's quality system are pending.

| ID | Requirement | Current verification |
|---|---|---|
| ECG-SWR-001 | The core shall accept uniformly sampled ECG in microvolts at an integer 125–1000 Hz. | `test_invalid_config`; format tests |
| ECG-SWR-002 | The core shall reject non-finite samples and unknown input flags without propagating non-finite internal state. | `test_fail_closed_inputs`; sanitizer |
| ECG-SWR-003 | Lead-off, ADC clipping, and pacer flags shall suppress all rhythm, beat, and waveform candidates and force unanalysable output. | `test_fail_closed_inputs`; `test_lead_off_suppresses_rhythm` |
| ECG-SWR-004 | After an unavailable/invalid sample, accumulated analysis state shall be cleared and the warm-up interval repeated while the monotonic sample time is preserved. | `test_fail_closed_inputs` |
| ECG-SWR-005 | Invalid, non-finite, contradictory, or unsupported configuration shall be rejected before processing. | `test_invalid_config` |
| ECG-SWR-006 | Host bindings shall verify API version and structure size before processing. | Python bridge initialization and bridge tests |
| ECG-SWR-007 | The implementation shall use no heap allocation or operating-system services in the C core. | Code review; linker/map-file evidence pending |
| ECG-SWR-008 | P, QRS, and T landmarks shall reference actual input sample positions; absent threshold-qualified P/T extrema shall produce no marker. | Synthetic waveform tests; annotated clinical validation pending |
| ECG-SWR-009 | Candidate detections shall be suppressed while signal quality is unavailable. | C unit tests |
| ECG-SWR-010 | The same source shall compile and pass tests with GCC and Clang under strict diagnostics. | GitHub Actions `c-verification` |
| ECG-SWR-011 | Long-running bounded input and repeated invalid/unavailable input shall preserve finite outputs, monotonic sample time, bounded landmark indexes, and fail-closed event output at all supported rate classes. Exhausted timestamp range shall fail closed without wrapping. | `test_deterministic_stress_invariants`; `test_fail_closed_inputs`; sanitizer |
| ECG-SWR-012 | File input shall reject missing, empty, non-finite, dimensionally inconsistent, or unsupported-unit signals and shall deterministically disambiguate duplicate channel labels. | `test_record_formats.py` |

## Missing product requirements

The manufacturer must define and approve intended use, target population,
operators, environment, supported lead sets and placements, sampling/ADC
specification, alarm behaviour and priority, detection latency, minimum
sensitivity and positive predictive value by rhythm, maximum false alarms per
hour, data retention, update policy, and safe-state behaviour. No algorithm can
be clinically validated until those acceptance criteria are fixed before the
validation dataset is examined.
