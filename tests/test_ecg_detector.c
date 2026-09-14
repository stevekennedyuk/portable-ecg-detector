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

int main(void)
{
    test_invalid_config();
    test_regular_and_asystole();
    test_lead_off_suppresses_rhythm();
    test_rapid_regular_candidate();
    test_af_candidate();
    test_vf_candidate();
    puts("all tests passed");
    return 0;
}
