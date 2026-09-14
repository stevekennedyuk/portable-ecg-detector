#ifndef ECG_DETECTOR_H
#define ECG_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECG_DETECTOR_MAX_SAMPLE_RATE_HZ 1000u
#define ECG_DETECTOR_MAX_RR_INTERVALS 32u
#define ECG_DETECTOR_MAX_MWI_SAMPLES 200u
#define ECG_DETECTOR_MAX_VF_SAMPLES (4u * ECG_DETECTOR_MAX_SAMPLE_RATE_HZ)
#define ECG_DETECTOR_MAX_HISTORY_SAMPLES 1200u

typedef enum {
    ECG_RHYTHM_WARMUP = 0,
    ECG_RHYTHM_UNANALYSABLE,
    ECG_RHYTHM_UNKNOWN,
    ECG_RHYTHM_REGULAR,
    ECG_RHYTHM_BRADYCARDIA_CANDIDATE,
    ECG_RHYTHM_TACHYCARDIA_CANDIDATE,
    ECG_RHYTHM_AF_CANDIDATE,
    ECG_RHYTHM_SVT_CANDIDATE,
    ECG_RHYTHM_VT_CANDIDATE,
    ECG_RHYTHM_VF_CANDIDATE,
    ECG_RHYTHM_ASYSTOLE_CANDIDATE
} ecg_rhythm_t;

typedef enum {
    ECG_BEAT_NONE = 0,
    ECG_BEAT_NORMAL_CANDIDATE,
    ECG_BEAT_PAC_CANDIDATE,
    ECG_BEAT_PVC_CANDIDATE,
    ECG_BEAT_WIDE_COMPLEX_CANDIDATE,
    ECG_BEAT_UNCLASSIFIED
} ecg_beat_type_t;

enum {
    ECG_EVENT_QRS                 = 1u << 0,
    ECG_EVENT_SIGNAL_POOR         = 1u << 1,
    ECG_EVENT_BRADYCARDIA         = 1u << 2,
    ECG_EVENT_TACHYCARDIA         = 1u << 3,
    ECG_EVENT_AF_CANDIDATE        = 1u << 4,
    ECG_EVENT_VT_CANDIDATE        = 1u << 5,
    ECG_EVENT_VF_CANDIDATE        = 1u << 6,
    ECG_EVENT_ASYSTOLE_CANDIDATE  = 1u << 7,
    ECG_EVENT_SVT_CANDIDATE       = 1u << 8,
    ECG_EVENT_PAUSE               = 1u << 9,
    ECG_EVENT_PAC_CANDIDATE       = 1u << 10,
    ECG_EVENT_PVC_CANDIDATE       = 1u << 11,
    ECG_EVENT_VENTRICULAR_COUPLET = 1u << 12,
    ECG_EVENT_BIGEMINY_CANDIDATE  = 1u << 13,
    ECG_EVENT_TRIGEMINY_CANDIDATE = 1u << 14,
    ECG_EVENT_P_WAVE              = 1u << 15,
    ECG_EVENT_T_WAVE              = 1u << 16
};

enum {
    ECG_INPUT_LEAD_OFF    = 1u << 0,
    ECG_INPUT_ADC_CLIPPED = 1u << 1,
    ECG_INPUT_PACER_SEEN  = 1u << 2
};

typedef struct {
    uint16_t sample_rate_hz;
    uint8_t mains_hz;                 /* 0 disables notch; otherwise 50 or 60. */
    float highpass_hz;
    float lowpass_hz;
    float notch_q;
    uint16_t qrs_refractory_ms;
    uint16_t asystole_ms;
    uint16_t brady_bpm;
    uint16_t tachy_bpm;
    uint16_t vt_bpm;
    float min_signal_range_uv;
    float max_sample_slew_uv;
    float adc_clip_uv;
    float vf_min_rms_uv;
    float p_wave_min_uv;
    float t_wave_min_uv;
} ecg_detector_config_t;

typedef struct {
    uint64_t sample_index;
    float filtered_uv;
    float heart_rate_bpm;
    uint16_t last_rr_ms;
    uint16_t qrs_width_ms;
    uint64_t qrs_peak_sample_index;
    uint64_t p_peak_sample_index;
    uint64_t t_peak_sample_index;
    float p_peak_uv;
    float t_peak_uv;
    ecg_beat_type_t beat_type;
    uint8_t signal_quality;            /* 0..100; not a clinical quality score. */
    ecg_rhythm_t rhythm;
    uint32_t active_events;
    uint32_t new_events;
    uint32_t input_flags;
} ecg_detector_output_t;

typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} ecg_biquad_t;

typedef struct {
    ecg_detector_config_t config;
    uint64_t sample_index;

    float hp_alpha;
    float hp_prev_x;
    float hp_prev_y;
    float lp_alpha;
    float lp_prev_y;
    ecg_biquad_t notch;
    bool notch_enabled;

    float previous_filtered;
    float mwi[ECG_DETECTOR_MAX_MWI_SAMPLES];
    uint16_t mwi_length;
    uint16_t mwi_position;
    float mwi_sum;
    float mwi_prev2;
    float mwi_prev1;
    float qrs_signal_level;
    float qrs_noise_level;
    float qrs_threshold;
    float warmup_peak;
    uint64_t warmup_samples;
    uint64_t last_qrs_sample;
    bool has_qrs;

    uint16_t rr_ms[ECG_DETECTOR_MAX_RR_INTERVALS];
    uint8_t rr_count;
    uint8_t rr_position;
    float heart_rate_bpm;
    uint16_t last_rr_ms;
    uint16_t qrs_width_ms[ECG_DETECTOR_MAX_RR_INTERVALS];
    uint8_t qrs_width_position;
    uint8_t qrs_width_count;
    ecg_beat_type_t beat_history[6];
    uint8_t beat_history_count;
    uint8_t beat_history_position;
    ecg_beat_type_t last_beat_type;
    uint8_t af_evidence;
    bool brady_active;
    bool tachy_active;
    bool af_active;
    bool vt_active;
    bool svt_active;

    float filtered_history[ECG_DETECTOR_MAX_HISTORY_SAMPLES];
    uint16_t history_length;
    uint16_t history_position;
    uint16_t history_count;
    uint64_t last_r_peak_sample;
    uint64_t pending_t_r_sample;
    bool has_r_peak;
    bool t_wave_pending;

    float quality_min;
    float quality_max;
    float quality_prev;
    uint32_t quality_samples;
    uint32_t quality_artifacts;
    uint32_t quality_clipped;
    uint8_t signal_quality;
    bool signal_good;

    float vf_samples[ECG_DETECTOR_MAX_VF_SAMPLES];
    uint16_t vf_length;
    uint16_t vf_position;
    uint16_t vf_qrs_count;
    uint32_t vf_hold_samples;

    uint32_t previous_active_events;
    uint32_t input_flags;
} ecg_detector_t;

/* Populates conservative monitoring defaults. */
void ecg_detector_default_config(ecg_detector_config_t *config,
                                 uint16_t sample_rate_hz);

/* Returns false for invalid configuration. No allocation is performed. */
bool ecg_detector_init(ecg_detector_t *detector,
                       const ecg_detector_config_t *config);

void ecg_detector_reset(ecg_detector_t *detector);

/*
 * Processes one ECG sample expressed in microvolts. The caller supplies AFE
 * status bits in input_flags. The returned pointer remains owned by the caller.
 */
void ecg_detector_process(ecg_detector_t *detector,
                          float sample_uv,
                          uint32_t input_flags,
                          ecg_detector_output_t *output);

const char *ecg_detector_rhythm_name(ecg_rhythm_t rhythm);

#ifdef __cplusplus
}
#endif

#endif
