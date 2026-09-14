#include "ecg_detector.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846f

static float synthetic_ecg(uint64_t sample, uint16_t fs, float bpm)
{
    const float period = 60.0f * (float)fs / bpm;
    const float phase = fmodf((float)sample, period);
    const float qrs_center = 0.20f * (float)fs;
    const float qrs_width = 0.035f * (float)fs;
    const float distance = fabsf(phase - qrs_center);
    const float baseline = 35.0f * sinf(2.0f * TEST_PI * 0.25f *
                                       (float)sample / (float)fs);
    const float p_distance = (phase - 0.08f * (float)fs) /
                             (0.025f * (float)fs);
    const float t_distance = (phase - 0.48f * (float)fs) /
                             (0.060f * (float)fs);
    const float p_wave = 100.0f * expf(-0.5f * p_distance * p_distance);
    const float t_wave = 240.0f * expf(-0.5f * t_distance * t_distance);
    float qrs = 0.0f;
    if (distance < qrs_width)
        qrs = 1100.0f * (1.0f - distance / qrs_width);
    return baseline + p_wave + qrs + t_wave;
}

static void test_invalid_config(void)
{
    ecg_detector_t detector;
    ecg_detector_config_t config;
    ecg_detector_default_config(&config, 100u);
    assert(!ecg_detector_init(&detector, &config));
    ecg_detector_default_config(&config, 250u);
    config.lowpass_hz = NAN;
    assert(!ecg_detector_init(&detector, &config));
    ecg_detector_default_config(&config, 250u);
    config.highpass_hz = config.lowpass_hz;
    assert(!ecg_detector_init(&detector, &config));
    ecg_detector_default_config(&config, 250u);
    config.brady_bpm = config.tachy_bpm;
    assert(!ecg_detector_init(&detector, &config));
    ecg_detector_default_config(&config, 128u);
    config.mains_hz = 60u;
    assert(ecg_detector_init(&detector, &config));
}

static void test_fail_closed_inputs(void)
{
    ecg_detector_t detector;
    ecg_detector_t uninitialized = {0};
    ecg_detector_config_t config;
    ecg_detector_output_t output;
    uint64_t previous_index;
    uint32_t i;

    assert(ecg_detector_process_checked(NULL, 0.0f, 0u, &output) ==
           ECG_STATUS_INVALID_ARGUMENT);
    assert(ecg_detector_process_checked(&uninitialized, 0.0f, 0u, &output) ==
           ECG_STATUS_NOT_INITIALIZED);

    ecg_detector_default_config(&config, 250u);
    config.mains_hz = 0u;
    assert(ecg_detector_init(&detector, &config));
    for (i = 0u; i < 750u; ++i)
        assert(ecg_detector_process_checked(&detector, 100.0f, 0u, &output) ==
               ECG_STATUS_OK);
    previous_index = output.sample_index;

    assert(ecg_detector_process_checked(&detector, NAN, 0u, &output) ==
           ECG_STATUS_INVALID_SAMPLE);
    assert(output.sample_index == previous_index + 1u);
    assert(output.rhythm == ECG_RHYTHM_UNANALYSABLE);
    assert((output.new_events & ECG_EVENT_INPUT_INVALID) != 0u);
    assert((output.new_events & ECG_EVENT_QRS) == 0u);

    assert(ecg_detector_process_checked(&detector, 0.0f, 0x80000000u,
                                        &output) ==
           ECG_STATUS_INVALID_SAMPLE);
    assert(ecg_detector_process_checked(&detector, 0.0f,
                                        ECG_INPUT_LEAD_OFF, &output) ==
           ECG_STATUS_SIGNAL_UNAVAILABLE);
    assert(output.signal_quality == 0u);
    assert((output.active_events & ECG_EVENT_SIGNAL_POOR) != 0u);
    assert((output.active_events & ~(uint32_t)ECG_EVENT_SIGNAL_POOR) == 0u);

    assert(ecg_detector_process_checked(&detector, config.adc_clip_uv,
                                        0u, &output) ==
           ECG_STATUS_SIGNAL_UNAVAILABLE);
    assert(isfinite(output.filtered_uv));

    detector.sample_index = UINT64_MAX;
    assert(ecg_detector_process_checked(&detector, 0.0f, 0u, &output) ==
           ECG_STATUS_TIMEBASE_EXHAUSTED);
    assert(output.sample_index == UINT64_MAX);
    assert(output.rhythm == ECG_RHYTHM_UNANALYSABLE);
}

static void test_regular_and_asystole(void)
{
    ecg_detector_t detector;
    ecg_detector_config_t config;
    ecg_detector_output_t output;
    const uint16_t fs = 250u;
    uint64_t i;
    unsigned qrs_count = 0u;
    unsigned p_count = 0u;
    unsigned t_count = 0u;

    ecg_detector_default_config(&config, fs);
    config.mains_hz = 0u;
    assert(ecg_detector_init(&detector, &config));

    for (i = 0u; i < 12u * fs; ++i) {
        ecg_detector_process(&detector, synthetic_ecg(i, fs, 60.0f), 0u,
                             &output);
        if ((output.new_events & ECG_EVENT_QRS) != 0u) qrs_count++;
        if ((output.new_events & ECG_EVENT_P_WAVE) != 0u) {
            assert(output.p_peak_sample_index < output.sample_index);
            p_count++;
        }
        if ((output.new_events & ECG_EVENT_T_WAVE) != 0u) {
            assert(output.t_peak_sample_index < output.sample_index);
            t_count++;
        }
    }
    assert(qrs_count >= 8u && qrs_count <= 12u);
    assert(p_count >= 7u);
    assert(t_count >= 7u);
    assert(output.heart_rate_bpm > 55.0f && output.heart_rate_bpm < 65.0f);
    assert(output.rhythm == ECG_RHYTHM_REGULAR);

    for (i = 0u; i < 4u * fs; ++i)
        ecg_detector_process(&detector, 120.0f * sinf(2.0f * TEST_PI * 0.4f *
                             (float)i / (float)fs), 0u, &output);
    assert((output.active_events & ECG_EVENT_ASYSTOLE_CANDIDATE) != 0u);
}

static void test_lead_off_suppresses_rhythm(void)
{
    ecg_detector_t detector;
    ecg_detector_config_t config;
    ecg_detector_output_t output;
    uint32_t i;

    ecg_detector_default_config(&config, 250u);
    assert(ecg_detector_init(&detector, &config));
    for (i = 0u; i < 500u; ++i)
        ecg_detector_process(&detector, 0.0f, ECG_INPUT_LEAD_OFF, &output);
    assert(output.signal_quality == 0u);
    assert(output.rhythm == ECG_RHYTHM_UNANALYSABLE);
    assert((output.active_events & ECG_EVENT_SIGNAL_POOR) != 0u);
    assert((output.active_events & ECG_EVENT_ASYSTOLE_CANDIDATE) == 0u);
}

static void test_rapid_regular_candidate(void)
{
    ecg_detector_t detector;
    ecg_detector_config_t config;
    ecg_detector_output_t output;
    const uint16_t fs = 250u;
    uint64_t i;

    ecg_detector_default_config(&config, fs);
    config.mains_hz = 0u;
    assert(ecg_detector_init(&detector, &config));
    for (i = 0u; i < 12u * fs; ++i)
        ecg_detector_process(&detector, synthetic_ecg(i, fs, 150.0f), 0u,
                             &output);

    assert(output.heart_rate_bpm > 140.0f && output.heart_rate_bpm < 160.0f);
    assert((output.active_events & ECG_EVENT_TACHYCARDIA) != 0u);
    assert((output.active_events & ECG_EVENT_SVT_CANDIDATE) != 0u);
    assert((output.active_events & ECG_EVENT_VT_CANDIDATE) == 0u);
    assert(output.rhythm == ECG_RHYTHM_SVT_CANDIDATE);
}

static void test_af_candidate(void)
{
    static const uint16_t intervals_ms[] = {
        620u, 1050u, 710u, 1280u, 540u, 900u, 760u, 1150u
    };
    ecg_detector_t detector;
    ecg_detector_config_t config;
    ecg_detector_output_t output;
    const uint16_t fs = 250u;
    uint64_t next_beat = 50u;
    uint16_t pulse_remaining = 0u;
    uint32_t interval_index = 0u;
    uint64_t i;

    ecg_detector_default_config(&config, fs);
    config.mains_hz = 0u;
    assert(ecg_detector_init(&detector, &config));
    for (i = 0u; i < 45u * fs; ++i) {
        float sample = 20.0f * sinf(2.0f * TEST_PI * 0.3f *
                                    (float)i / (float)fs);
        if (i == next_beat) {
            pulse_remaining = 8u;
            next_beat += ((uint64_t)intervals_ms[interval_index % 8u] * fs) /
                         1000u;
            interval_index++;
        }
        if (pulse_remaining != 0u) {
            sample += 1000.0f * (float)pulse_remaining / 8.0f;
            pulse_remaining--;
        }
        ecg_detector_process(&detector, sample, 0u, &output);
    }
    assert((output.active_events & ECG_EVENT_AF_CANDIDATE) != 0u);
    assert(output.rhythm == ECG_RHYTHM_AF_CANDIDATE);
}

static void test_vf_candidate(void)
{
    ecg_detector_t detector;
    ecg_detector_config_t config;
    ecg_detector_output_t output;
    const uint16_t fs = 250u;
    uint64_t i;
    bool seen = false;

    ecg_detector_default_config(&config, fs);
    config.mains_hz = 0u;
    assert(ecg_detector_init(&detector, &config));
    for (i = 0u; i < 10u * fs; ++i) {
        const float t = (float)i / (float)fs;
        const float sample = 280.0f * sinf(2.0f * TEST_PI * 4.1f * t) +
                             190.0f * sinf(2.0f * TEST_PI * 6.7f * t +
                                           0.5f * sinf(0.7f * t));
        ecg_detector_process(&detector, sample, 0u, &output);
        if ((output.active_events & ECG_EVENT_VF_CANDIDATE) != 0u) seen = true;
    }
    assert(seen);
}

static void test_deterministic_stress_invariants(void)
{
    static const uint16_t rates[] = {125u, 250u, 360u, 500u, 1000u};
    uint32_t random_state = UINT32_C(0x6d2b79f5);
    size_t rate_index;

    for (rate_index = 0u; rate_index < sizeof(rates) / sizeof(rates[0]);
         ++rate_index) {
        ecg_detector_t detector;
        ecg_detector_config_t config;
        ecg_detector_output_t output;
        uint64_t expected_sample = 0u;
        uint32_t i;

        ecg_detector_default_config(&config, rates[rate_index]);
        assert(ecg_detector_init(&detector, &config));
        for (i = 0u; i < 50000u; ++i) {
            float sample;
            uint32_t flags = 0u;
            ecg_detector_status_t status;

            random_state = random_state * UINT32_C(1664525) +
                           UINT32_C(1013904223);
            sample = ((float)(random_state & UINT32_C(0xffff)) - 32768.0f) *
                     (3000.0f / 32768.0f);
            if (i % 9973u == 0u) flags = ECG_INPUT_LEAD_OFF;
            if (i % 12007u == 0u) flags = ECG_INPUT_ADC_CLIPPED;
            if (i % 15013u == 0u) sample = INFINITY;

            status = ecg_detector_process_checked(&detector, sample, flags,
                                                   &output);
            expected_sample++;
            assert(output.sample_index == expected_sample);
            assert(isfinite(output.filtered_uv));
            assert(isfinite(output.heart_rate_bpm));
            assert(isfinite(output.p_peak_uv));
            assert(isfinite(output.t_peak_uv));
            assert(output.signal_quality <= 100u);
            assert(output.qrs_peak_sample_index <= output.sample_index);
            assert(output.p_peak_sample_index <= output.sample_index);
            assert(output.t_peak_sample_index <= output.sample_index);
            if (status != ECG_STATUS_OK) {
                const uint32_t permitted = ECG_EVENT_SIGNAL_POOR |
                                           ECG_EVENT_INPUT_INVALID;
                assert((output.active_events & ~permitted) == 0u);
                assert(output.rhythm == ECG_RHYTHM_UNANALYSABLE);
            }
        }
    }
}

int main(void)
{
    test_invalid_config();
    test_fail_closed_inputs();
    test_regular_and_asystole();
    test_lead_off_suppresses_rhythm();
    test_rapid_regular_candidate();
    test_af_candidate();
    test_vf_candidate();
    test_deterministic_stress_invariants();
    puts("all tests passed");
    return 0;
}
