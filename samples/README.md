# Sample ECG recordings

## Included AF recording

`aftdb-n01/` contains an unmodified, one-minute, two-channel WFDB recording of
atrial fibrillation sampled at 128 Hz. Record `n01` belongs to group N in the
PhysioNet AF Termination Challenge Database: AF was not observed to terminate
for at least one hour after this segment.

Open it in the macOS viewer from the repository root:

```sh
source .venv/bin/activate
python3 -m ecg_viewer samples/aftdb-n01/n01.hea --mains 60
```

Files:

- `n01.hea`: WFDB header.
- `n01.dat`: two-channel ECG samples.
- `n01.qrs`: original machine-generated QRS annotations. The viewer does not
  consume these annotations; its markers come from the C detector. PhysioNet
  states that these QRS annotations are unaudited and can contain errors.

The files were downloaded unchanged from the [AF Termination Challenge
Database](https://physionet.org/content/aftdb/1.0.0/learning-set/) and verified
against PhysioNet's published SHA-256 checksums. The database files are made
available under the [Open Data Commons Attribution License
1.0](https://physionet.org/content/aftdb/view-license/1.0.0/).

Please cite:

> Moody GB. Spontaneous Termination of Atrial Fibrillation: A Challenge from
> PhysioNet and Computers in Cardiology 2004. Computers in Cardiology
> 31:101-104 (2004). DOI: 10.13026/C2CC7Z.

These research recordings must not be interpreted as clinical test evidence
for this detector.

## Additional official datasets

The following datasets are intentionally linked rather than copied because
they are substantially larger:

- [MIT-BIH Arrhythmia Database](https://physionet.org/content/mitdb/1.0.0/):
  48 half-hour, two-channel recordings with beat annotations. Useful for QRS,
  PAC, PVC and rhythm evaluation.
- [MIT-BIH Atrial Fibrillation Database](https://physionet.org/content/afdb/1.0.0/):
  long-term recordings with rhythm annotations; approximately 606 MB
  uncompressed.
- [MIT-BIH Malignant Ventricular Ectopy Database](https://physionet.org/content/vfdb/1.0.0/):
  recordings containing sustained VT, ventricular flutter and VF.
- [MIT-BIH Noise Stress Test Database](https://physionet.org/content/nstdb/1.0.0/):
  ECG with calibrated baseline wander, muscle artefact and electrode-motion
  noise.
- [PTB-XL](https://physionet.org/content/ptb-xl/1.0.3/): more than 21,000
  clinical 12-lead ECGs with standardized diagnostic statements.

Use the `wfdb` package installed in `.venv` to download selected records rather
than entire databases. For example:

```sh
source .venv/bin/activate
python3 -c "import wfdb; wfdb.dl_database('mitdb', 'samples/mitdb', records=['100', '200'])"
```

Record `100` is a compact general example; record `200` contains frequent
ventricular ectopy. Consult each dataset page for its license, annotations and
required citation.
