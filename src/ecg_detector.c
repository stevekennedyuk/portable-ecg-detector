#include "ecg_detector.h"

#include <math.h>
#include <string.h>

#define ECG_PI 3.14159265358979323846f
#define ECG_INITIALIZATION_COOKIE UINT32_C(0x45434732)

static float clampf(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static float biquad_process(ecg_biquad_t *filter, float x)
{
    const float y = filter->b0 * x + filter->b1 * filter->x1 +
                    filter->b2 * filter->x2 - filter->a1 * filter->y1 -
                    filter->a2 * filter->y2;
    filter->x2 = filter->x1;
    filter->x1 = x;
    filter->y2 = filter->y1;
    filter->y1 = y;
    return y;
}

static void configure_notch(ecg_detector_t *detector)
{
    ecg_biquad_t *f = &detector->notch;
    const float fs = (float)detector->config.sample_rate_hz;
    const float w0 = 2.0f * ECG_PI * (float)detector->config.mains_hz / fs;
    const float alpha = sinf(w0) / (2.0f * detector->config.notch_q);
    const float a0 = 1.0f + alpha;

    f->b0 = 1.0f / a0;
    f->b1 = -2.0f * cosf(w0) / a0;
    f->b2 = 1.0f / a0;
    f->a1 = f->b1;
    f->a2 = (1.0f - alpha) / a0;
}

void ecg_detector_default_config(ecg_detector_config_t *config,
                                 uint16_t sample_rate_hz)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->sample_rate_hz = sample_rate_hz;
    config->mains_hz = 50u;
    config->highpass_hz = 0.5f;
    config->lowpass_hz = 40.0f;
    config->notch_q = 30.0f;
    config->qrs_refractory_ms = 220u;
    config->asystole_ms = 3000u;
    config->brady_bpm = 45u;
    config->tachy_bpm = 120u;
    config->vt_bpm = 140u;
    config->min_signal_range_uv = 80.0f;
    config->max_sample_slew_uv = 2500.0f;
    config->adc_clip_uv = 5000.0f;
    config->vf_min_rms_uv = 120.0f;
    config->p_wave_min_uv = 40.0f;
    config->t_wave_min_uv = 75.0f;
}

static bool config_valid(const ecg_detector_config_t *config)
{
    if (config == NULL || config->sample_rate_hz < 125u ||
        config->sample_rate_hz > ECG_DETECTOR_MAX_SAMPLE_RATE_HZ) return false;
    if (config->mains_hz != 0u && config->mains_hz != 50u &&
        config->mains_hz != 60u) return false;
    if (!isfinite(config->highpass_hz) || !isfinite(config->lowpass_hz) ||
        !isfinite(config->notch_q) ||
        !isfinite(config->min_signal_range_uv) ||
        !isfinite(config->max_sample_slew_uv) ||
        !isfinite(config->adc_clip_uv) || !isfinite(config->vf_min_rms_uv) ||
        !isfinite(config->p_wave_min_uv) ||
        !isfinite(config->t_wave_min_uv)) return false;
    if (config->highpass_hz <= 0.0f || config->lowpass_hz <= 0.0f ||
        config->highpass_hz >= config->lowpass_hz ||
        config->lowpass_hz >= 0.45f * (float)config->sample_rate_hz) return false;
    if (config->notch_q <= 0.0f || config->qrs_refractory_ms < 150u ||
        config->asystole_ms < 1000u) return false;
    if (config->mains_hz != 0u &&
        (uint32_t)config->mains_hz * 2u >=
        (uint32_t)config->sample_rate_hz)
        return false;
    if (config->brady_bpm < 20u || config->brady_bpm >= config->tachy_bpm ||
        config->tachy_bpm >= config->vt_bpm || config->vt_bpm > 300u)
        return false;
    if (config->min_signal_range_uv <= 0.0f ||
        config->max_sample_slew_uv <= 0.0f || config->adc_clip_uv <= 0.0f ||
        config->vf_min_rms_uv <= 0.0f || config->p_wave_min_uv <= 0.0f ||
        config->t_wave_min_uv <= 0.0f) return false;
    return true;
}

bool ecg_detector_init(ecg_detector_t *detector,
                       const ecg_detector_config_t *config)
{
    float dt;
    float rc;
    if (detector == NULL || !config_valid(config)) return false;

    memset(detector, 0, sizeof(*detector));
    detector->initialization_cookie = ECG_INITIALIZATION_COOKIE;
    detector->config = *config;
    dt = 1.0f / (float)config->sample_rate_hz;
    rc = 1.0f / (2.0f * ECG_PI * config->highpass_hz);
    detector->hp_alpha = rc / (rc + dt);
    rc = 1.0f / (2.0f * ECG_PI * config->lowpass_hz);
    detector->lp_alpha = dt / (rc + dt);
    detector->notch_enabled = config->mains_hz != 0u;
    if (detector->notch_enabled) configure_notch(detector);

    detector->mwi_length = (uint16_t)((config->sample_rate_hz * 150u) / 1000u);
    if (detector->mwi_length < 8u) detector->mwi_length = 8u;
    if (detector->mwi_length > ECG_DETECTOR_MAX_MWI_SAMPLES)
        detector->mwi_length = ECG_DETECTOR_MAX_MWI_SAMPLES;

    detector->vf_length = (uint16_t)(4u * config->sample_rate_hz);
    detector->history_length = (uint16_t)((1100u * config->sample_rate_hz) /
                                          1000u);
    if (detector->history_length < 64u) detector->history_length = 64u;
    if (detector->history_length > ECG_DETECTOR_MAX_HISTORY_SAMPLES)
        detector->history_length = ECG_DETECTOR_MAX_HISTORY_SAMPLES;
    detector->quality_min = INFINITY;
    detector->quality_max = -INFINITY;
    detector->signal_quality = 0u;
    return true;
}

void ecg_detector_reset(ecg_detector_t *detector)
{
    ecg_detector_config_t config;
    if (detector == NULL ||
        detector->initialization_cookie != ECG_INITIALIZATION_COOKIE) return;
    config = detector->config;
    (void)ecg_detector_init(detector, &config);
}

static float filter_sample(ecg_detector_t *d, float x)
{
    const float hp = d->hp_alpha * (d->hp_prev_y + x - d->hp_prev_x);
    float value;
    d->hp_prev_x = x;
    d->hp_prev_y = hp;
    value = d->notch_enabled ? biquad_process(&d->notch, hp) : hp;
    d->lp_prev_y += d->lp_alpha * (value - d->lp_prev_y);
    return d->lp_prev_y;
}

static void update_quality(ecg_detector_t *d, float sample, uint32_t flags)
{
    const float slew = fabsf(sample - d->quality_prev);
    d->quality_prev = sample;
    if (sample < d->quality_min) d->quality_min = sample;
    if (sample > d->quality_max) d->quality_max = sample;
    if (slew > d->config.max_sample_slew_uv) d->quality_artifacts++;
    if (fabsf(sample) >= d->config.adc_clip_uv ||
        (flags & ECG_INPUT_ADC_CLIPPED) != 0u) d->quality_clipped++;
    d->quality_samples++;

    if (d->quality_samples >= d->config.sample_rate_hz) {
        const float range = d->quality_max - d->quality_min;
        float score = 100.0f;
        if (range < d->config.min_signal_range_uv) score -= 70.0f;
        score -= 100.0f * (float)d->quality_artifacts /
                 (float)d->quality_samples;
        if (d->quality_clipped != 0u) score -= 70.0f;
        if ((flags & ECG_INPUT_LEAD_OFF) != 0u) score = 0.0f;
        d->signal_quality = (uint8_t)clampf(score, 0.0f, 100.0f);
        d->signal_good = d->signal_quality >= 50u;
        d->quality_min = INFINITY;
        d->quality_max = -INFINITY;
        d->quality_samples = 0u;
        d->quality_artifacts = 0u;
        d->quality_clipped = 0u;
    }
    if ((flags & ECG_INPUT_LEAD_OFF) != 0u) {
        d->signal_quality = 0u;
        d->signal_good = false;
    }
}

static void rr_statistics(const ecg_detector_t *d, uint8_t count,
                          float *mean, float *cv, float *rmssd_ratio)
{
    float sum = 0.0f;
    float variance = 0.0f;
    float squared_diffs = 0.0f;
    uint8_t i;
    if (count > d->rr_count) count = d->rr_count;
    for (i = 0u; i < count; ++i) {
        const uint8_t index = (uint8_t)((d->rr_position +
                              ECG_DETECTOR_MAX_RR_INTERVALS - 1u - i) %
                              ECG_DETECTOR_MAX_RR_INTERVALS);
        sum += (float)d->rr_ms[index];
    }
    *mean = count != 0u ? sum / (float)count : 0.0f;
    for (i = 0u; i < count; ++i) {
        const uint8_t index = (uint8_t)((d->rr_position +
                              ECG_DETECTOR_MAX_RR_INTERVALS - 1u - i) %
                              ECG_DETECTOR_MAX_RR_INTERVALS);
        const float delta = (float)d->rr_ms[index] - *mean;
        variance += delta * delta;
        if (i + 1u < count) {
            const uint8_t previous = (uint8_t)((index +
                                     ECG_DETECTOR_MAX_RR_INTERVALS - 1u) %
                                     ECG_DETECTOR_MAX_RR_INTERVALS);
            const float diff = (float)d->rr_ms[index] -
                               (float)d->rr_ms[previous];
            squared_diffs += diff * diff;
        }
    }
    *cv = (count > 1u && *mean > 0.0f) ?
          sqrtf(variance / (float)(count - 1u)) / *mean : 0.0f;
    *rmssd_ratio = (count > 1u && *mean > 0.0f) ?
                   sqrtf(squared_diffs / (float)(count - 1u)) / *mean : 0.0f;
}

static uint16_t median_previous_rr(const ecg_detector_t *d, uint8_t count)
{
    uint16_t values[8];
    uint8_t available;
    uint8_t i, j;
    if (d->rr_count <= 1u) return 0u;
    available = (uint8_t)(d->rr_count - 1u); /* Exclude newest interval. */
    if (count > available) count = available;
    if (count > 8u) count = 8u;
    for (i = 0u; i < count; ++i) {
        const uint8_t index = (uint8_t)((d->rr_position +
                              ECG_DETECTOR_MAX_RR_INTERVALS - 2u - i) %
                              ECG_DETECTOR_MAX_RR_INTERVALS);
        values[i] = d->rr_ms[index];
    }
    for (i = 1u; i < count; ++i) {
        const uint16_t value = values[i];
        j = i;
        while (j > 0u && values[j - 1u] > value) {
            values[j] = values[j - 1u];
            --j;
        }
        values[j] = value;
    }
    if (count == 0u) return 0u;
    if ((count & 1u) != 0u) return values[count / 2u];
    return (uint16_t)(((uint32_t)values[count / 2u - 1u] +
                       values[count / 2u]) / 2u);
}

static float history_at_age(const ecg_detector_t *d, uint16_t age)
{
    const uint16_t index = (uint16_t)((d->history_position + d->history_length -
                                      1u - age) % d->history_length);
    return d->filtered_history[index];
}

static uint64_t locate_r_peak_sample(const ecg_detector_t *d)
{
    uint16_t search = (uint16_t)((250u * d->config.sample_rate_hz) / 1000u);
    uint16_t peak_age = 0u;
    uint16_t age;
    float peak = 0.0f;
    if (d->history_count == 0u) return d->sample_index;
    if (search >= d->history_count) search = (uint16_t)(d->history_count - 1u);
    for (age = 0u; age <= search; ++age) {
        const float magnitude = fabsf(history_at_age(d, age));
        if (magnitude > peak) {
            peak = magnitude;
            peak_age = age;
        }
    }
    return d->sample_index - peak_age;
}

static bool find_wave_peak(const ecg_detector_t *d, uint64_t start,
                           uint64_t end, float minimum_uv,
                           uint64_t *peak_sample, float *peak_uv)
{
    uint64_t sample;
    float start_value, end_value;
    float best_residual = 0.0f;
    bool found = false;
    if (start >= end || end > d->sample_index ||
        d->sample_index - start >= d->history_count) return false;
    start_value = history_at_age(d, (uint16_t)(d->sample_index - start));
    end_value = history_at_age(d, (uint16_t)(d->sample_index - end));
    for (sample = start + 1u; sample < end; ++sample) {
        const float fraction = (float)(sample - start) / (float)(end - start);
        const float baseline = start_value + fraction * (end_value - start_value);
        const float value = history_at_age(d,
                            (uint16_t)(d->sample_index - sample));
        const float previous = history_at_age(d,
                               (uint16_t)(d->sample_index - sample + 1u));
        const float next = history_at_age(d,
                           (uint16_t)(d->sample_index - sample - 1u));
        const float residual = fabsf(value - baseline);
        const bool local_extremum =
            (value >= previous && value >= next) ||
            (value <= previous && value <= next);
        if (local_extremum && residual > best_residual) {
            best_residual = residual;
            *peak_sample = sample;
            *peak_uv = value;
            found = true;
        }
    }
    return found && best_residual >= minimum_uv;
}

static uint64_t samples_before(uint64_t sample, uint32_t amount)
{
    return sample > amount ? sample - amount : 0u;
}

static uint32_t delineate_at_qrs(ecg_detector_t *d, uint64_t r_peak,
                                 ecg_detector_output_t *output)
{
    const uint32_t fs = d->config.sample_rate_hz;
    uint32_t events = 0u;
    uint64_t start, end;

    if (d->t_wave_pending) {
        start = d->pending_t_r_sample + (120u * fs) / 1000u;
        end = d->pending_t_r_sample + (500u * fs) / 1000u;
        if (r_peak > (80u * fs) / 1000u &&
            end > r_peak - (80u * fs) / 1000u)
            end = r_peak - (80u * fs) / 1000u;
        if (find_wave_peak(d, start, end, d->config.t_wave_min_uv,
                           &output->t_peak_sample_index,
                           &output->t_peak_uv))
            events |= ECG_EVENT_T_WAVE;
        d->t_wave_pending = false;
    }

    start = samples_before(r_peak, (280u * fs) / 1000u);
    end = samples_before(r_peak, (60u * fs) / 1000u);
    if (d->has_r_peak) {
        const uint64_t after_previous = d->last_r_peak_sample +
                                        (80u * fs) / 1000u;
        if (start < after_previous) start = after_previous;
    }
    if (find_wave_peak(d, start, end, d->config.p_wave_min_uv,
                       &output->p_peak_sample_index, &output->p_peak_uv))
        events |= ECG_EVENT_P_WAVE;

    d->last_r_peak_sample = r_peak;
    d->has_r_peak = true;
    d->pending_t_r_sample = r_peak;
    d->t_wave_pending = true;
    return events;
}

static uint32_t delineate_pending_t(ecg_detector_t *d,
                                    ecg_detector_output_t *output)
{
    const uint32_t fs = d->config.sample_rate_hz;
    const uint64_t end = d->pending_t_r_sample + (500u * fs) / 1000u;
    const uint64_t start = d->pending_t_r_sample + (120u * fs) / 1000u;
    if (!d->t_wave_pending || d->sample_index < end) return 0u;
    d->t_wave_pending = false;
    if (find_wave_peak(d, start, end, d->config.t_wave_min_uv,
                       &output->t_peak_sample_index, &output->t_peak_uv))
        return ECG_EVENT_T_WAVE;
    return 0u;
}

static uint16_t estimate_qrs_width_ms(const ecg_detector_t *d)
{
    uint16_t search = (uint16_t)((250u * d->config.sample_rate_hz) / 1000u);
    uint16_t peak_age = 0u;
    uint16_t older, newer;
    float peak = 0.0f;
    float threshold;
    uint16_t age;
    if (search >= d->history_count) search = (uint16_t)(d->history_count - 1u);
    for (age = 0u; age <= search; ++age) {
        const float magnitude = fabsf(history_at_age(d, age));
        if (magnitude > peak) {
            peak = magnitude;
            peak_age = age;
        }
    }
    if (peak < 40.0f) return 0u;
    threshold = fmaxf(40.0f, peak * 0.18f);
    older = peak_age;
    while (older < search && fabsf(history_at_age(d, older)) > threshold)
        older++;
    newer = peak_age;
    while (newer > 0u && fabsf(history_at_age(d, newer)) > threshold)
        newer--;
    return (uint16_t)(((uint32_t)(older - newer) * 1000u) /
                      d->config.sample_rate_hz);
}

static void update_rhythm_after_qrs(ecg_detector_t *d)
{
    float mean, cv, rmssd;
    if (d->rr_count >= 4u) {
        rr_statistics(d, 4u, &mean, &cv, &rmssd);
        d->heart_rate_bpm = mean > 0.0f ? 60000.0f / mean : 0.0f;
        d->brady_active = d->heart_rate_bpm < (float)d->config.brady_bpm;
        d->tachy_active = d->heart_rate_bpm > (float)d->config.tachy_bpm;
    }

    if (d->rr_count >= 6u) {
        uint8_t wide = 0u;
        uint8_t i;
        bool rapid;
        rr_statistics(d, 6u, &mean, &cv, &rmssd);
        for (i = 0u; i < 6u && i < d->qrs_width_count; ++i) {
            const uint8_t index = (uint8_t)((d->qrs_width_position +
                                  ECG_DETECTOR_MAX_RR_INTERVALS - 1u - i) %
                                  ECG_DETECTOR_MAX_RR_INTERVALS);
            if (d->qrs_width_ms[index] >= 120u) wide++;
        }
        rapid = mean > 0.0f && 60000.0f / mean >=
                (float)d->config.vt_bpm && cv < 0.12f;
        d->vt_active = rapid && wide >= 4u;
        d->svt_active = rapid && wide < 4u;
    } else {
        d->vt_active = false;
        d->svt_active = false;
    }

    if (d->rr_count >= 16u) {
        bool evidence;
        rr_statistics(d, 16u, &mean, &cv, &rmssd);
        evidence = mean >= 333.0f && mean <= 1500.0f &&
                   cv > 0.10f && rmssd > 0.12f;
        if (evidence && d->af_evidence < 4u) d->af_evidence++;
        if (!evidence && d->af_evidence > 0u) d->af_evidence--;
        d->af_active = d->af_evidence >= 3u;
    }
}

static uint32_t classify_beat(ecg_detector_t *d)
{
    uint32_t events = 0u;
    const uint16_t width = estimate_qrs_width_ms(d);
    const uint16_t median_rr = median_previous_rr(d, 8u);
    const bool premature = d->rr_count >= 4u && median_rr != 0u &&
                           d->last_rr_ms < (uint16_t)(0.80f * median_rr);
    ecg_beat_type_t beat;
    uint8_t i;

    d->qrs_width_ms[d->qrs_width_position] = width;
    d->qrs_width_position = (uint8_t)((d->qrs_width_position + 1u) %
                                      ECG_DETECTOR_MAX_RR_INTERVALS);
    if (d->qrs_width_count < ECG_DETECTOR_MAX_RR_INTERVALS)
        d->qrs_width_count++;

    if (premature && width >= 120u) {
        beat = ECG_BEAT_PVC_CANDIDATE;
        events |= ECG_EVENT_PVC_CANDIDATE;
    } else if (premature) {
        beat = ECG_BEAT_PAC_CANDIDATE;
        events |= ECG_EVENT_PAC_CANDIDATE;
    } else if (width >= 120u) {
        beat = ECG_BEAT_WIDE_COMPLEX_CANDIDATE;
    } else if (width != 0u) {
        beat = ECG_BEAT_NORMAL_CANDIDATE;
    } else {
        beat = ECG_BEAT_UNCLASSIFIED;
    }

    if (d->rr_count >= 2u && median_rr != 0u &&
        (d->last_rr_ms >= 2000u || d->last_rr_ms > 1.8f * median_rr))
        events |= ECG_EVENT_PAUSE;

    d->beat_history[d->beat_history_position] = beat;
    d->beat_history_position = (uint8_t)((d->beat_history_position + 1u) % 6u);
    if (d->beat_history_count < 6u) d->beat_history_count++;
    d->last_beat_type = beat;

    if (d->beat_history_count >= 2u) {
        const uint8_t last = (uint8_t)((d->beat_history_position + 5u) % 6u);
        const uint8_t prior = (uint8_t)((d->beat_history_position + 4u) % 6u);
        if (d->beat_history[last] == ECG_BEAT_PVC_CANDIDATE &&
            d->beat_history[prior] == ECG_BEAT_PVC_CANDIDATE)
            events |= ECG_EVENT_VENTRICULAR_COUPLET;
    }
    if (d->beat_history_count >= 4u) {
        bool bigeminy = true;
        for (i = 0u; i < 4u; ++i) {
            const uint8_t index = (uint8_t)((d->beat_history_position + 2u + i) % 6u);
            const bool should_be_pvc = (i & 1u) != 0u;
            if ((d->beat_history[index] == ECG_BEAT_PVC_CANDIDATE) !=
                should_be_pvc) bigeminy = false;
        }
        if (bigeminy) events |= ECG_EVENT_BIGEMINY_CANDIDATE;
    }
    if (d->beat_history_count >= 6u) {
        bool trigeminy = true;
        for (i = 0u; i < 6u; ++i) {
            const uint8_t index = (uint8_t)((d->beat_history_position + i) % 6u);
            const bool should_be_pvc = (i % 3u) == 2u;
            if ((d->beat_history[index] == ECG_BEAT_PVC_CANDIDATE) !=
                should_be_pvc) trigeminy = false;
        }
        if (trigeminy) events |= ECG_EVENT_TRIGEMINY_CANDIDATE;
    }
    return events;
}

static bool detect_qrs(ecg_detector_t *d, float filtered)
{
    const float derivative = filtered - d->previous_filtered;
    const float energy = derivative * derivative;
    const uint64_t refractory = ((uint64_t)d->config.sample_rate_hz *
                                 d->config.qrs_refractory_ms) / 1000u;
    bool qrs = false;
    float peak;

    d->previous_filtered = filtered;
    d->mwi_sum -= d->mwi[d->mwi_position];
    d->mwi[d->mwi_position] = energy;
    d->mwi_sum += energy;
    d->mwi_position = (uint16_t)((d->mwi_position + 1u) % d->mwi_length);
    peak = d->mwi_sum / (float)d->mwi_length;

    if (d->warmup_samples < 2u * d->config.sample_rate_hz) {
        if (peak > d->warmup_peak) d->warmup_peak = peak;
        d->warmup_samples++;
        if (d->warmup_samples == 2u * d->config.sample_rate_hz) {
            d->qrs_signal_level = d->warmup_peak;
            d->qrs_noise_level = d->warmup_peak * 0.05f;
            d->qrs_threshold = d->warmup_peak * 0.18f;
        }
    } else if (d->mwi_prev1 > d->mwi_prev2 && d->mwi_prev1 >= peak) {
        const bool outside_refractory = !d->has_qrs ||
            d->sample_index - d->last_qrs_sample >= refractory;
        if (d->mwi_prev1 > d->qrs_threshold && outside_refractory) {
            uint16_t rr;
            qrs = true;
            d->qrs_signal_level = 0.125f * d->mwi_prev1 +
                                  0.875f * d->qrs_signal_level;
            if (d->has_qrs) {
                const uint64_t rr_samples = d->sample_index - 1u -
                                            d->last_qrs_sample;
                rr = (uint16_t)((rr_samples * 1000u) /
                                d->config.sample_rate_hz);
                if (rr >= 250u && rr <= 2500u) {
                    d->last_rr_ms = rr;
                    d->rr_ms[d->rr_position] = rr;
                    d->rr_position = (uint8_t)((d->rr_position + 1u) %
                                               ECG_DETECTOR_MAX_RR_INTERVALS);
                    if (d->rr_count < ECG_DETECTOR_MAX_RR_INTERVALS)
                        d->rr_count++;
                }
            }
            d->last_qrs_sample = d->sample_index - 1u;
            d->has_qrs = true;
            d->vf_qrs_count++;
        } else {
            d->qrs_noise_level = 0.125f * d->mwi_prev1 +
                                 0.875f * d->qrs_noise_level;
        }
        d->qrs_threshold = d->qrs_noise_level +
                           0.25f * (d->qrs_signal_level - d->qrs_noise_level);
        if (d->qrs_threshold < d->warmup_peak * 0.02f)
            d->qrs_threshold = d->warmup_peak * 0.02f;
    }

    d->mwi_prev2 = d->mwi_prev1;
    d->mwi_prev1 = peak;
    return qrs;
}

static bool evaluate_vf_block(ecg_detector_t *d)
{
    float mean = 0.0f;
    float energy = 0.0f;
    uint16_t crossings = 0u;
    uint16_t crossing_intervals = 0u;
    uint16_t previous_crossing = 0u;
    bool has_crossing = false;
    float crossing_sum = 0.0f;
    float crossing_squared_sum = 0.0f;
    uint16_t i;
    float previous;
    float frequency;

    for (i = 0u; i < d->vf_length; ++i) mean += d->vf_samples[i];
    mean /= (float)d->vf_length;
    previous = d->vf_samples[0] - mean;
    for (i = 0u; i < d->vf_length; ++i) {
        const float centered = d->vf_samples[i] - mean;
        energy += centered * centered;
        if (i != 0u && ((centered >= 0.0f && previous < 0.0f) ||
                        (centered < 0.0f && previous >= 0.0f))) {
            if (has_crossing) {
                const float interval = (float)(i - previous_crossing);
                crossing_sum += interval;
                crossing_squared_sum += interval * interval;
                crossing_intervals++;
            }
            previous_crossing = i;
            has_crossing = true;
            crossings++;
        }
        previous = centered;
    }
    energy = sqrtf(energy / (float)d->vf_length);
    frequency = (float)crossings / 8.0f; /* 2 crossings/cycle, 4 s block. */

    {
        float crossing_cv = 0.0f;
        float qrs_mean = 0.0f;
        float qrs_cv = 1.0f;
        float qrs_rmssd = 1.0f;
        bool organized_qrs = false;
        if (crossing_intervals > 1u && crossing_sum > 0.0f) {
            const float crossing_mean = crossing_sum /
                                        (float)crossing_intervals;
            float variance = crossing_squared_sum /
                             (float)crossing_intervals -
                             crossing_mean * crossing_mean;
            if (variance < 0.0f) variance = 0.0f;
            crossing_cv = sqrtf(variance) / crossing_mean;
        }
        if (d->vf_qrs_count >= 3u && d->rr_count >= 3u) {
            rr_statistics(d, d->rr_count < 8u ? d->rr_count : 8u,
                          &qrs_mean, &qrs_cv, &qrs_rmssd);
            organized_qrs = qrs_mean > 0.0f && qrs_cv < 0.15f;
        }

    /* This intentionally reports a candidate, not a diagnostic VF decision. */
    return d->signal_good && energy >= d->config.vf_min_rms_uv &&
           frequency >= 2.0f && frequency <= 12.0f &&
           !organized_qrs &&
           (d->vf_qrs_count <= 2u || crossing_cv > 0.18f);
    }
}

static void update_vf(ecg_detector_t *d, float filtered)
{
    d->vf_samples[d->vf_position++] = filtered;
    if (d->vf_position >= d->vf_length) {
        if (evaluate_vf_block(d))
            d->vf_hold_samples = 2u * d->config.sample_rate_hz;
        d->vf_position = 0u;
        d->vf_qrs_count = 0u;
    }
    if (d->vf_hold_samples > 0u) d->vf_hold_samples--;
}

static uint32_t active_events(const ecg_detector_t *d)
{
    uint32_t events = 0u;
    const uint64_t asystole_samples = ((uint64_t)d->config.sample_rate_hz *
                                       d->config.asystole_ms) / 1000u;
    if (!d->signal_good) events |= ECG_EVENT_SIGNAL_POOR;
    if (d->signal_good && d->brady_active) events |= ECG_EVENT_BRADYCARDIA;
    if (d->signal_good && d->tachy_active) events |= ECG_EVENT_TACHYCARDIA;
    if (d->signal_good && d->af_active) events |= ECG_EVENT_AF_CANDIDATE;
    if (d->signal_good && d->vt_active) events |= ECG_EVENT_VT_CANDIDATE;
    if (d->signal_good && d->svt_active) events |= ECG_EVENT_SVT_CANDIDATE;
    if (d->signal_good && d->vf_hold_samples > 0u)
        events |= ECG_EVENT_VF_CANDIDATE;
    if (d->signal_good && d->has_qrs &&
        d->sample_index - d->last_qrs_sample >= asystole_samples)
        events |= ECG_EVENT_ASYSTOLE_CANDIDATE;
    return events;
}

static ecg_rhythm_t select_rhythm(const ecg_detector_t *d, uint32_t events)
{
    if (d->warmup_samples < 2u * d->config.sample_rate_hz)
        return ECG_RHYTHM_WARMUP;
    if ((events & ECG_EVENT_SIGNAL_POOR) != 0u)
        return ECG_RHYTHM_UNANALYSABLE;
    if ((events & ECG_EVENT_ASYSTOLE_CANDIDATE) != 0u)
        return ECG_RHYTHM_ASYSTOLE_CANDIDATE;
    if ((events & ECG_EVENT_VF_CANDIDATE) != 0u)
        return ECG_RHYTHM_VF_CANDIDATE;
    if ((events & ECG_EVENT_VT_CANDIDATE) != 0u)
        return ECG_RHYTHM_VT_CANDIDATE;
    if ((events & ECG_EVENT_SVT_CANDIDATE) != 0u)
        return ECG_RHYTHM_SVT_CANDIDATE;
    if ((events & ECG_EVENT_AF_CANDIDATE) != 0u)
        return ECG_RHYTHM_AF_CANDIDATE;
    if ((events & ECG_EVENT_BRADYCARDIA) != 0u)
        return ECG_RHYTHM_BRADYCARDIA_CANDIDATE;
    if ((events & ECG_EVENT_TACHYCARDIA) != 0u)
        return ECG_RHYTHM_TACHYCARDIA_CANDIDATE;
    if (d->rr_count >= 4u) return ECG_RHYTHM_REGULAR;
    return ECG_RHYTHM_UNKNOWN;
}

static ecg_detector_status_t fail_closed_sample(ecg_detector_t *d,
                                                 uint32_t input_flags,
                                                 ecg_detector_status_t status,
                                                 ecg_detector_output_t *output)
{
    const ecg_detector_config_t config = d->config;
    const uint64_t next_sample = d->sample_index == UINT64_MAX ?
                                 UINT64_MAX : d->sample_index + UINT64_C(1);
    (void)ecg_detector_init(d, &config);
    d->sample_index = next_sample;
    memset(output, 0, sizeof(*output));
    output->sample_index = next_sample;
    output->signal_quality = 0u;
    output->rhythm = ECG_RHYTHM_UNANALYSABLE;
    output->active_events = ECG_EVENT_SIGNAL_POOR;
    output->new_events = ECG_EVENT_SIGNAL_POOR;
    if (status == ECG_STATUS_INVALID_SAMPLE) {
        output->active_events |= ECG_EVENT_INPUT_INVALID;
        output->new_events |= ECG_EVENT_INPUT_INVALID;
    }
    output->input_flags = input_flags;
    output->status = status;
    return status;
}

ecg_detector_status_t ecg_detector_process_checked(
    ecg_detector_t *d, float sample_uv, uint32_t input_flags,
    ecg_detector_output_t *output)
{
    float filtered;
    bool qrs;
    uint32_t events;
    uint32_t beat_events = 0u;
    uint32_t wave_events = 0u;
    uint64_t qrs_peak_sample = 0u;
    if (output == NULL) return ECG_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (d == NULL) {
        output->status = ECG_STATUS_INVALID_ARGUMENT;
        return output->status;
    }
    if (d->initialization_cookie != ECG_INITIALIZATION_COOKIE) {
        output->status = ECG_STATUS_NOT_INITIALIZED;
        return output->status;
    }
    if (d->sample_index == UINT64_MAX) {
        output->sample_index = UINT64_MAX;
        output->signal_quality = 0u;
        output->rhythm = ECG_RHYTHM_UNANALYSABLE;
        output->active_events = ECG_EVENT_SIGNAL_POOR |
                                ECG_EVENT_INPUT_INVALID;
        output->new_events = output->active_events;
        output->status = ECG_STATUS_TIMEBASE_EXHAUSTED;
        return output->status;
    }
    if (!isfinite(sample_uv) ||
        (input_flags & ~(uint32_t)ECG_DETECTOR_INPUT_FLAG_MASK) != 0u)
        return fail_closed_sample(d, input_flags, ECG_STATUS_INVALID_SAMPLE,
                                  output);
    if ((input_flags & ECG_DETECTOR_INPUT_FLAG_MASK) != 0u ||
        fabsf(sample_uv) >= d->config.adc_clip_uv)
        return fail_closed_sample(d, input_flags,
                                  ECG_STATUS_SIGNAL_UNAVAILABLE, output);

    d->input_flags = input_flags;
    filtered = filter_sample(d, sample_uv);
    d->filtered_history[d->history_position] = filtered;
    d->history_position = (uint16_t)((d->history_position + 1u) %
                                     d->history_length);
    if (d->history_count < d->history_length) d->history_count++;
    update_quality(d, sample_uv, input_flags);
    qrs = detect_qrs(d, filtered);
    if (qrs) {
        qrs_peak_sample = locate_r_peak_sample(d);
        output->qrs_peak_sample_index = qrs_peak_sample;
        wave_events |= delineate_at_qrs(d, qrs_peak_sample, output);
        beat_events = classify_beat(d);
        update_rhythm_after_qrs(d);
    }
    wave_events |= delineate_pending_t(d, output);
    update_vf(d, filtered);
    d->sample_index++;
    events = active_events(d);

    output->qrs_peak_sample_index = qrs_peak_sample;
    output->sample_index = d->sample_index;
    output->filtered_uv = filtered;
    output->heart_rate_bpm = d->heart_rate_bpm;
    output->last_rr_ms = d->last_rr_ms;
    output->qrs_width_ms = qrs ? d->qrs_width_ms[
        (d->qrs_width_position + ECG_DETECTOR_MAX_RR_INTERVALS - 1u) %
        ECG_DETECTOR_MAX_RR_INTERVALS] : 0u;
    output->beat_type = qrs ? d->last_beat_type : ECG_BEAT_NONE;
    output->signal_quality = d->signal_quality;
    output->active_events = events | beat_events | wave_events |
                            (qrs ? ECG_EVENT_QRS : 0u);
    output->new_events = (events & ~d->previous_active_events) | beat_events |
                         wave_events;
    if (qrs) output->new_events |= ECG_EVENT_QRS;
    output->rhythm = select_rhythm(d, events);
    output->input_flags = input_flags;
    output->status = ECG_STATUS_OK;
    d->previous_active_events = events;
    return ECG_STATUS_OK;
}

void ecg_detector_process(ecg_detector_t *d, float sample_uv,
                          uint32_t input_flags,
                          ecg_detector_output_t *output)
{
    (void)ecg_detector_process_checked(d, sample_uv, input_flags, output);
}

const char *ecg_detector_rhythm_name(ecg_rhythm_t rhythm)
{
    switch (rhythm) {
    case ECG_RHYTHM_WARMUP: return "warmup";
    case ECG_RHYTHM_UNANALYSABLE: return "unanalysable";
    case ECG_RHYTHM_UNKNOWN: return "unknown";
    case ECG_RHYTHM_REGULAR: return "regular";
    case ECG_RHYTHM_BRADYCARDIA_CANDIDATE: return "bradycardia candidate";
    case ECG_RHYTHM_TACHYCARDIA_CANDIDATE: return "tachycardia candidate";
    case ECG_RHYTHM_AF_CANDIDATE: return "AF candidate";
    case ECG_RHYTHM_SVT_CANDIDATE: return "SVT candidate";
    case ECG_RHYTHM_VT_CANDIDATE: return "VT candidate";
    case ECG_RHYTHM_VF_CANDIDATE: return "VF candidate";
    case ECG_RHYTHM_ASYSTOLE_CANDIDATE: return "asystole candidate";
    default: return "invalid";
    }
}
