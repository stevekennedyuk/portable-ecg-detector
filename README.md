# Portable ECG detector core

This directory contains a dependency-free, streaming C99 reference core for
ECG signal conditioning and candidate rhythm detection. It performs no dynamic
allocation, makes no operating-system calls, and keeps all state in an
`ecg_detector_t` supplied by the caller.

It is intended as a development baseline for MCU and application-processor
ports. It is **not a clinically validated algorithm and must not be used to
diagnose, treat, or provide a safety-critical alarm without a complete medical
device development and validation process**.

## Included

- 0.5 Hz high-pass and configurable 50/60 Hz notch filtering
- low-pass filtering for the QRS/detection path
- adaptive streaming QRS detection
- signal-derived P- and T-wave peak delineation with sample index and amplitude
- heart rate and RR interval tracking
- input lead-off, clipping, slew and flat-signal quality checks
- bradycardia, tachycardia, pause and asystole candidate states
- RR-irregularity AF candidate state
- QRS-width-assisted VT versus narrow-complex SVT candidate states
- PAC, PVC, ventricular-couplet, bigeminy and trigeminy candidates
- conservative four-second VF candidate heuristic
- active-event and rising-edge event bitmasks

The AF implementation uses RR irregularity and has no P-wave analysis. PAC/PVC
and VT/SVT separation uses estimated QRS width and prematurity; a production
implementation also needs lead-specific morphology/template and atrial-activity
logic. The VF code is a deliberately conservative placeholder suitable for
building the data and validation interfaces, not a medical alarm detector.

## Build and test

```sh
make test
```

The test uses only the standard C library and `libm`.

## macOS ECG viewer

The Python viewer reads standard PhysioNet/WFDB (`.hea` plus signal files) and
EDF/EDF+ recordings. It displays all leads in synchronized 5, 10, 20 or
30-second windows and overlays the events produced by this C detector. QRS
locations are shown as dots; arrhythmia onsets are labelled vertical markers.
Detected P and T peaks are placed on the sampled waveform as green `P` and blue
`T` landmarks. They are not generated from nominal beat timing.

On macOS:

```sh
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements-macos.txt
python3 -m ecg_viewer /path/to/record.hea
```

For an EDF recording:

```sh
python3 -m ecg_viewer /path/to/record.edf --mains 50
```

Running without a path opens a native file chooser:

```sh
python3 -m ecg_viewer
```

The repository includes a small real AF recording for an immediate test:

```sh
python3 -m ecg_viewer samples/aftdb-n01/n01.hea --mains 60
```

See [`samples/README.md`](samples/README.md) for its source, license and
additional official arrhythmia dataset links.

The bridge automatically builds `build/libecg_detector.dylib` with Clang when
the C source changes. Detection is performed independently for every displayed
lead. That is helpful during development, but a production multi-lead monitor
should add a lead-fusion policy so one physiological event does not become 12
independent alarms.

## Minimal integration

```c
#include "ecg_detector.h"

static ecg_detector_t detector;

void app_init(void)
{
    ecg_detector_config_t config;
    ecg_detector_default_config(&config, 500);
    config.mains_hz = 50;
    if (!ecg_detector_init(&detector, &config)) {
        /* configuration error */
    }
}

void on_afe_sample(int32_t sample_uv, bool lead_off)
{
    ecg_detector_output_t result;
    uint32_t flags = lead_off ? ECG_INPUT_LEAD_OFF : 0u;
    ecg_detector_process(&detector, (float)sample_uv, flags, &result);

    if ((result.new_events & ECG_EVENT_QRS) != 0u) {
        /* update beat indicator */
    }
    if ((result.new_events & ECG_EVENT_AF_CANDIDATE) != 0u) {
        /* preserve the pre/post-event strip; do not diagnose from this alone */
    }
}
```

Call `ecg_detector_process` exactly once for each uniformly sampled channel
sample. Use one detector instance per analysed lead. The sample value must be in
microvolts after applying the AFE gain and ADC scale.

## Porting notes

- RAM use is fixed and dominated by the four-second VF buffer and waveform
  history: approximately 22 KiB with the default compile-time limits.
- Stack use per call is small. The detector object should normally be static.
- `sinf`, `cosf`, `sqrtf`, `fabsf` and `fmodf` are used; `fmodf` is only used by
  the host test. Filter coefficients are calculated only during init.
- For an MCU without efficient floating point, retain this API and replace the
  filter and statistics internals with Q31/CMSIS-DSP implementations.
- The input status flags should come directly from the AFE driver. Do not infer
  lead-off solely from the ECG waveform.
- Rendering, file I/O, networking and alarm sounds belong outside this library.

## Required validation work

Before considering product use, replace or validate each candidate detector
against a written algorithm specification. Add WFDB-based replay tests using
patient-wise held-out MIT-BIH AF, Arrhythmia, VF and Noise Stress Test records;
measure event sensitivity, positive predictive value, detection latency and
false alarms per hour. Then validate on recordings made with the final AFE,
electrodes, lead placement and target population.
