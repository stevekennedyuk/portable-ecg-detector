# Preliminary software risk register

This is an engineering input to, not a substitute for, an ISO 14971 risk
management file. Severity, probability, acceptability, control ownership, and
residual-risk approval must be assigned by the legal manufacturer.

| Hazardous situation | Example cause | Current control | Required evidence / remaining control |
|---|---|---|---|
| Missed lethal rhythm | Low sensitivity, poor electrode contact, paced rhythm, lead-specific morphology | Signal-unavailable state; candidate wording | Pre-specified VF/VT/asystole clinical performance; independent alarm path; end-to-end fault injection |
| False lethal-rhythm alarm | Motion, mains interference, muscle noise, heuristic VF detector | Signal quality gating; delayed VF window | Noise Stress Test and final-hardware recordings; false alarms/hour limits; multi-lead confirmation |
| Incorrect AF indication | RR irregularity from ectopy/noise; no validated atrial analysis | Warm-up, RR window, signal gating | Patient-wise external validation; ectopy rejection; validated P-wave/atrial features |
| Incorrect P/T marker | Fibrillatory or noise extremum selected in timing window | Minimum prominence and exact-sample output; marker omitted when absent | Expert-annotated delineation dataset; per-lead morphology; uncertainty output |
| Stale result shown as current | Input stops, lead disconnects, numeric error | Checked status clears analysis and forces unanalysable | Watchdog and sample-timestamp continuity in acquisition software |
| Numeric corruption | NaN/infinity, invalid configuration, ABI mismatch | Finite/range checks, initialization cookie, ABI version/size check, sanitizers | Static analysis, MC/DC as justified, target compiler qualification strategy |
| Time-base error | Dropped/duplicated samples or wrong sample rate | Rate validation only | Sequence numbers, monotonic hardware timestamps, clock-tolerance monitoring |
| Cross-lead alarm duplication/conflict | Independent per-lead detector instances | Documented limitation | Validated lead selection/fusion and alarm arbitration |
| Electrical harm | Isolation/barrier or lead protection failure | Outside this software repository | IEC 60601 safety architecture, leakage/defibrillation/EMC testing by accredited lab |
| Alarm not perceived or acted on | UI freeze, wrong priority, excessive nuisance alarms | None in prototype viewer | IEC 60601-1-8 alarm design where applicable; usability validation; independent watchdog |
| Cybersecurity compromise | Unsigned software/update, vulnerable host/dependency | Read-only CI permissions; direct dependency pins | Threat model, SBOM, signed reproducible releases, secure boot/update, vulnerability process |

Until residual risks are formally evaluated and accepted, every detector output
is a candidate for review and must not independently drive therapy.
