#include "ecg_detector_ffi.h"

#include "ecg_detector.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    char prefix;
    ecg_detector_t state;
} ecg_detector_alignment_probe_t;

size_t ecg_detector_ffi_state_size(void)
{
    return sizeof(ecg_detector_t);
}

size_t ecg_detector_ffi_state_alignment(void)
{
    return offsetof(ecg_detector_alignment_probe_t, state);
}

int ecg_detector_ffi_init(void *state, uint16_t sample_rate_hz,
                          uint8_t mains_hz)
{
    ecg_detector_config_t config;
    if (state == NULL) return 0;
    ecg_detector_default_config(&config, sample_rate_hz);
    config.mains_hz = mains_hz;
    return ecg_detector_init((ecg_detector_t *)state, &config) ? 1 : 0;
}

void ecg_detector_ffi_process(void *state, float sample_uv,
                              uint32_t input_flags,
                              ecg_detector_ffi_output_t *output)
{
    ecg_detector_output_t result;
    if (state == NULL || output == NULL) return;
    ecg_detector_process((ecg_detector_t *)state, sample_uv, input_flags,
                         &result);
    memset(output, 0, sizeof(*output));
    output->sample_index = result.sample_index;
    output->filtered_uv = result.filtered_uv;
    output->heart_rate_bpm = result.heart_rate_bpm;
    output->last_rr_ms = result.last_rr_ms;
    output->qrs_width_ms = result.qrs_width_ms;
    output->signal_quality = result.signal_quality;
    output->rhythm = (uint8_t)result.rhythm;
    output->beat_type = (uint8_t)result.beat_type;
    output->active_events = result.active_events;
    output->new_events = result.new_events;
}

void ecg_detector_ffi_process_buffer(void *state, const float *samples_uv,
                                     size_t count, uint32_t input_flags,
                                     ecg_detector_ffi_output_t *outputs)
{
    size_t i;
    if (state == NULL || samples_uv == NULL || outputs == NULL) return;
    for (i = 0u; i < count; ++i)
        ecg_detector_ffi_process(state, samples_uv[i], input_flags,
                                 &outputs[i]);
}
