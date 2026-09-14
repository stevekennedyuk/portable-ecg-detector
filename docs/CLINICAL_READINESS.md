# Clinical readiness gate

## Current status

This repository is a robust research/development baseline, not a clinical-grade
device and not cleared for patient diagnosis or safety-critical alarming.
Clinical grade is a property of the complete released device, evidence, quality
system, manufacturing controls, and intended use—not of an algorithm alone.

## Standards and regulatory planning

Applicability must be determined for the chosen market and intended use with a
qualified regulatory professional. Likely inputs include:

- IEC 62304:2006+A1:2015 for medical-device software lifecycle processes:
  https://webstore.iec.ch/en/publication/6792
- ISO 14971:2019 for medical-device risk management:
  https://www.iso.org/standard/72704.html
- ISO 13485:2016 for the manufacturer's quality management system:
  https://www.iso.org/standard/59752.html
- IEC 60601-2-27:2011 for ECG monitoring essential performance where its scope
  applies. The standard excludes home-use and Holter monitors, so the intended
  use is decisive: https://webstore.iec.ch/en/publication/2638
- FDA Content of Premarket Submissions for Device Software Functions and
  cybersecurity guidance, selected from:
  https://www.fda.gov/medical-devices/digital-health-center-excellence/guidances-digital-health-content

Also assess IEC 60601-1, IEC 60601-1-2, IEC 60601-1-8, IEC 60601-1-11,
IEC 62366-1, IEC 81001-5-1, electrode biocompatibility, and regional
requirements according to the final architecture and use environment.

## Release-blocking evidence

1. Freeze intended use, indications, contraindications, patient population,
   environment, lead configuration, users, and whether output is diagnostic,
   advisory, or an alarm.
2. Establish an ISO 13485 design-control process and IEC 62304 safety
   classification, plans, reviews, configuration management, problem
   resolution, and traceability.
3. Complete ISO 14971 hazard analysis spanning AFE, isolation, electrodes,
   acquisition timing, CPU/GPU, power, display, storage, alarms, networking,
   updates, and use error.
4. Replace research heuristics or freeze a versioned algorithm specification.
   Lock all thresholds before clinical validation.
5. Validate on independent, patient-separated datasets representative of the
   intended population and final hardware. Report confidence intervals,
   sensitivity, positive predictive value, specificity where meaningful,
   false alarms/hour, and detection latency for every claimed rhythm.
6. Validate P/QRS/T timing against expert consensus annotations, including
   absent waves; never score an unannotated timing-window extremum as truth.
7. Perform noise, motion, lead-off, pacing, defibrillation recovery, amplitude,
   rate, bandwidth, sampling-clock, dropped-sample, and multi-lead fault tests
   derived from applicable essential-performance requirements.
8. Add independent acquisition watchdogs, time-base continuity checks,
   multi-lead fusion, alarm arbitration, persistent event strips, and a safe
   degraded mode. The Python viewer is not a clinical runtime.
9. Produce architecture, detailed design, SOUP inventory/SBOM, threat model,
   static-analysis results, code reviews, unit/integration/system tests,
   coverage rationale, reproducible signed builds, and release records.
10. Complete electrical safety, EMC, environmental, usability, cybersecurity,
    clinical, and regulatory assessment on production-equivalent devices.

No release should be described as clinical grade until these gates have
approved objective evidence.
