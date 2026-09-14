#ifndef ECG_DETECTOR_FFI_H
#define ECG_DETECTOR_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t sample_index;
    float filtered_uv;
    float heart_rate_bpm;
    uint16_t last_rr_ms;
    uint16_t qrs_width_ms;
    uint8_t signal_quality;
    uint8_t rhythm;
    uint8_t beat_type;
    uint8_t reserved;
    uint32_t active_events;
    uint32_t new_events;
} ecg_detector_ffi_output_t;

size_t ecg_detector_ffi_state_size(void);
size_t ecg_detector_ffi_state_alignment(void);
int ecg_detector_ffi_init(void *state, uint16_t sample_rate_hz,
                          uint8_t mains_hz);
void ecg_detector_ffi_process(void *state, float sample_uv,
                              uint32_t input_flags,
                              ecg_detector_ffi_output_t *output);
void ecg_detector_ffi_process_buffer(void *state, const float *samples_uv,
                                     size_t count, uint32_t input_flags,
                                     ecg_detector_ffi_output_t *outputs);

#ifdef __cplusplus
}
#endif

#endif
