/* Audio DSP adapted from PrimeRecomp runtime/services/audio.c, commit
   138318ab06a365b81583ffeae23a472a88483e87.
   Copyright (c) 2026 Peiikai and PrimeRecomp contributors.
   MIT notice: ../LICENSES/primerecomp-MIT.txt.
   Native adapter uses only host mixer buffers; no source IPC, SDL, settings,
   game-address hooks, or guest-pointer casts are included. */
#include "audio_effects.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float *buf; uint32_t sr, cur, last, count, max; } DelayLine;

static void dl_set(DelayLine *d, float ms) {
    uint32_t n = (uint32_t)roundf(d->sr * fminf(fmaxf(ms, 0), 10000));
    d->count = n < d->max ? n : d->max;
    d->cur = 0;
    d->last = d->count ? d->count : 1;
}
static void dl_init(DelayLine *d, uint32_t sr, float ms_max) {
    d->sr = sr;
    d->max = (uint32_t)roundf(sr * ms_max);
    free(d->buf);
    d->buf = calloc(d->max + 1, sizeof(float));
    dl_set(d, ms_max);
}
static float dl_read(DelayLine *d) { return d->buf ? d->buf[d->cur] : 0; }
static float dl_update(DelayLine *d, float v) {
    if (!d->buf) return 0;
    float o = d->buf[d->cur];
    d->buf[d->cur++] = v;
    if (d->cur >= d->last) d->cur = 0;
    return o;
}
static float dl_tap_unsafe(DelayLine *d, uint32_t idx, int off) {
    if (!d->buf) return 0;
    int t = (int)d->cur - ((int)idx + off);
    if (t < 0) t += (int)d->count;
    if (t < 0 || (uint32_t)t > d->max) return 0;
    return d->buf[t];
}
static float dl_tap(DelayLine *d, uint32_t idx) {
    if (idx >= d->count) idx = d->count ? d->count - 1 : 0;
    return dl_tap_unsafe(d, idx, -1);
}

typedef struct { DelayLine dl; float rate; } DecayDelay;
static float dd_update(DecayDelay *d, float v) {
    float p = v - d->rate * dl_read(&d->dl);
    return dl_update(&d->dl, p) + p * d->rate;
}

typedef struct {
    uint8_t input[6], output[6];
    uint16_t ch_max, ch;
    int32_t sample_rate, early_mode, early_gain, pre_delay, late_mode, late_gain, decay_time, hf_decay_ratio,
        coloration, reverb_gain, out_gain, dry_gain;
} ReverbParam;

typedef struct {
    int ready;
    uint8_t raw[0x44];  // last parameter bytes seen
    ReverbParam p;
    DelayLine fdn[4], pre, fc;
    DecayDelay decay[4];
    uint32_t early_time[10], pre_time;
    float early_gain[10], hf_direct[4], hf_prev[4], prev_fb[4];
} Reverb;

static const float rv_fdn_times[20] = {53.953247f, 79.192566f, 116.238770f, 130.615295f, 53.953247f, 79.192566f,
    116.238770f, 170.615295f, 5, 10, 5, 10, 47.03f, 71, 103, 170, 53.953247f, 79.192566f, 116.238770f, 170.615295f};
static const float rv_decay_times[20] = {7, 9, 13, 17, 7, 9, 13, 17, 1, 1, 1, 1, 7, 7, 13, 9, 7, 9, 13, 17};
static const float rv_early_times[50] = {0.0f, 3.5f, 2.8f, 3.9f, 2.7f, 13.4f, 7.9f, 8.4f, 9.9f, 12.0f,
    0.0f, 11.8f, 5.5f, 11.2f, 10.4f, 38.1f, 22.2f, 29.6f, 21.2f, 24.8f,
    0.0f, 41.5f, 20.5f, 41.3f, 0.0f, 29.5f, 33.8f, 45.2f, 46.8f, 0.0f,
    33.1f, 43.3f, 22.8f, 37.9f, 14.9f, 35.3f, 17.9f, 34.2f, 0.0f, 43.3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const float rv_early_gain[50] = {0.70f, 0.68f, 0.70f, 0.68f, 0.70f, 0.68f, 0.70f, 0.68f, 0.68f, 0.68f,
    0.70f, 0.68f, 0.70f, 0.68f, 0.70f, 0.68f, 0.68f, 0.68f, 0.68f, 0.68f,
    0.50f, 0.70f, 0.70f, 0.68f, 0.50f, 0.68f, 0.68f, 0.70f, 0.68f, 0.00f,
    0.93f, 0.92f, 0.87f, 0.86f, 0.94f, 0.81f, 0.80f, 0.77f, 0.76f, 0.65f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const float rv_pre_times[5] = {12.5f, 40.0f, 50.0f, 50.0f, 0.0f};

static float q14(int32_t v) { return v / 16384.0f; }
static uint32_t rv_rate(const ReverbParam *p) {
    uint32_t sr = (uint32_t)q14(p->sample_rate);  // samples per millisecond
    return sr && sr <= 192 ? sr : 48;
}

static void reverb_update(Reverb *rv) {
    ReverbParam *p = &rv->p;
    uint32_t sr = rv_rate(p);
    float pre_ms = q14(p->pre_delay), early_gain = q14(p->early_gain), coloration = q14(p->coloration);
    float decay_time = q14(p->decay_time);
    int em = p->early_mode >= 0 && p->early_mode < 5 ? p->early_mode : 4;
    int lm = p->late_mode >= 0 && p->late_mode < 5 ? p->late_mode : 0;
    for (int i = 0; i < 10; i++) {
        uint32_t t = (uint32_t)roundf(sr * (rv_early_times[i] + pre_ms));
        rv->early_time[i] = (t < rv->pre.max ? t : rv->pre.max) + 1;
        rv->early_gain[i] = rv_early_gain[i] * early_gain;
    }
    if (p->ch == 2) { rv->early_gain[4] *= 0.5f; rv->early_gain[5] *= 0.5f; }
    uint32_t pt = (uint32_t)roundf(sr * (rv_pre_times[em] + pre_ms));
    rv->pre_time = pt < rv->pre.max ? pt : rv->pre.max;
    float hf_ratio = q14(p->hf_decay_ratio), hf_cos = cosf(1280.0f / sr);
    for (int i = 0; i < 4; i++) {
        dl_set(&rv->fdn[i], rv_fdn_times[lm * 4 + i]);
        dl_set(&rv->decay[i].dl, rv_decay_times[lm * 4 + i]);
        float a = -3.0f * (rv->decay[i].dl.count + rv->fdn[i].count);
        float b = decay_time > 0 ? a / (decay_time * sr) : -1000.0f;
        float c, d;
        if (hf_ratio < 0.995f && hf_ratio > 0) {
            float e = powf(10.0f, (((1.0f / hf_ratio) - 1.0f) * 2) / 100 * (b / 10));
            float f = 1.0f - e, g = 2.0f - (e * 2 * hf_cos);
            float h = sqrtf(fmaxf(0, g * g - f * f * 4));
            c = f != 0 ? (g - h) / (f * 2) : 0;
            d = 1.0f - c;
        } else {
            c = 0;
            d = 1;
        }
        rv->hf_direct[i] = powf(10.0f, b / 1000) * d * 0.7071f;
        rv->hf_prev[i] = c;
        rv->prev_fb[i] = 0;
        rv->decay[i].rate = 0.6f * (1.0f - coloration);
    }
}

static void reverb_init(Reverb *rv) {
    uint32_t sr = rv_rate(&rv->p);
    for (int i = 0; i < 4; i++) {
        dl_init(&rv->fdn[i], sr, rv_fdn_times[16 + i]);
        dl_init(&rv->decay[i].dl, sr, rv_decay_times[16 + i]);
    }
    dl_init(&rv->pre, sr, 150.0f);  // long pre-delay needs REV3+... 350 ms
    dl_init(&rv->fc, sr, 5.0f);
    reverb_update(rv);
    rv->ready = 1;
}

static void reverb_process(Reverb *rv, float *const *in, float *const *out, uint32_t n) {
    static const int8_t oe1[10] = {0}, te[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    static const int8_t o1[4] = {0}, tf1[4] = {0, 1, 2, 3};
    static const int8_t oe2[10] = {0, 0, 1, 1, 0, 1, 0, 0, 1, 1}, o2[4] = {0, 0, 1, 1}, tf2[4] = {2, 0, 3, 1};
    static const int8_t oe4[10] = {0, 0, 1, 1, 0, 1, 2, 2, 3, 3}, o4[4] = {0, 1, 2, 3}, tf4[4] = {0, 1, 2, 3};
    static const int8_t oe6[20] = {0, 5, 0, 5, 1, 5, 1, 5, 4, 5, 4, 5, 2, 5, 2, 5, 3, 5, 3, 5};
    static const int8_t te6[20] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9};
    static const int8_t o6[6] = {0, 1, 2, 3, 4, 5}, tf6[6] = {0, 1, 2, 3, -1, 3};
    int ch = rv->p.ch;
    const int8_t *oe, *tel, *o, *tf;
    int ne, nf;
    switch (ch) {
    case 1: oe = oe1; tel = te; o = o1; tf = tf1; ne = 10; nf = 4; break;
    case 2: oe = oe2; tel = te; o = o2; tf = tf2; ne = 10; nf = 4; break;
    case 4: oe = oe4; tel = te; o = o4; tf = tf4; ne = 10; nf = 4; break;
    case 6: oe = oe6; tel = te6; o = o6; tf = tf6; ne = 20; nf = 6; break;
    default: return;
    }
    float reverb_gain = q14(rv->p.reverb_gain), late_gain = q14(rv->p.late_gain);
    float out_gain = q14(rv->p.out_gain), dry_gain = q14(rv->p.dry_gain);
    for (uint32_t s = 0; s < n; s++) {
        float ov[6] = {0}, fb[4], fo[4], cin[6];
        for (int i = 0; i < ne; i++) ov[oe[i]] += dl_tap_unsafe(&rv->pre, rv->early_time[tel[i]], 0) * rv->early_gain[tel[i]];
        if (ch == 6) ov[5] *= 0.2f;
        float pre = 0;
        for (int c = 0; c < ch; c++) { cin[c] = in[c][s] * 64; pre += cin[c] * reverb_gain; }
        dl_update(&rv->pre, pre);
        float late = dl_tap(&rv->pre, rv->pre_time) * late_gain;
        for (int i = 0; i < 4; i++) {
            fo[i] = dl_read(&rv->fdn[i]) * rv->hf_direct[i] + rv->prev_fb[i] * rv->hf_prev[i];
            rv->prev_fb[i] = fo[i];
        }
        fb[0] = fo[2] + fo[1];
        fb[1] = -fo[0] - fo[3];
        fb[2] = fo[0] - fo[3];
        fb[3] = fo[1] - fo[2];
        for (int i = 0; i < 4; i++) {
            fo[i] = dd_update(&rv->decay[i], fb[i] + late);
            dl_update(&rv->fdn[i], fo[i]);
        }
        for (int i = 0; i < nf; i++) if (tf[i] >= 0) ov[o[i]] += fo[tf[i]];
        if (ch == 6) ov[4] += dl_update(&rv->fc, (fo[2] - fo[3]) * 0.5f);
        for (int c = 0; c < ch; c++) out[c][s] = (ov[c] * out_gain + cin[c] * dry_gain) / 64;
    }
}

// ------------------------------------------------------------------ I3DL2 reverb (port of Ryujinx Reverb3dState /
// Reverb3dCommand). Its delay lines write ahead of the read position (DelayLine3d).
typedef struct { float *buf; uint32_t sr, cur, last, count, max; } DelayLine3d;

static void d3_set(DelayLine3d *d, float ms) {
    uint32_t n = (uint32_t)roundf(d->sr * fminf(fmaxf(ms, 0), 10000));
    if (d->max >= n) {
        d->count = n;
        d->last = (d->cur + n) % (d->max + 1);
    }
}
static void d3_init(DelayLine3d *d, uint32_t sr, float ms_max) {
    d->sr = sr;
    d->max = (uint32_t)roundf(sr * ms_max);
    free(d->buf);
    d->buf = calloc(d->max + 1, sizeof(float));
    d->cur = d->last = 0;
    d3_set(d, ms_max);
}
static float d3_read(DelayLine3d *d) { return d->buf ? d->buf[d->cur] : 0; }
static float d3_update(DelayLine3d *d, float v) {
    if (!d->buf) return 0;
    d->buf[d->last++] = v;
    float o = d->buf[d->cur++];
    if (d->cur > d->max) d->cur = 0;
    if (d->last > d->max) d->last = 0;
    return o;
}
static float d3_tap(DelayLine3d *d, uint32_t idx, int off) {
    if (!d->buf) return 0;
    int t = (int)d->last - ((int)idx + off);
    if (t < 0) t += (int)d->max + 1;
    return t >= 0 && (uint32_t)t <= d->max ? d->buf[t] : 0;
}

typedef struct { DelayLine3d dl; float rate; } DecayDelay3d;
static float dd3_update(DecayDelay3d *d, float v) {
    float p = v - d->rate * d3_read(&d->dl);
    return d3_update(&d->dl, p) + p * d->rate;
}

typedef struct {  // I3dl2ReverbParameter
    uint8_t input[6], output[6];
    uint16_t ch_max, ch;
    uint32_t reserved, sample_rate;
    float room_hf, hf_reference, decay_time, hf_decay_ratio, room_gain, reflections_gain, reverb_gain, diffusion,
        reflection_delay, reverb_delay, density, dry_gain;
} Reverb3dParam;

typedef struct {
    int ready;
    uint8_t raw[0x48];
    Reverb3dParam p;
    DelayLine3d fdn[4], pre, fc;
    DecayDelay3d d1[4], d2[4];
    uint32_t refl_time, early_time[20];
    float prev_pre_value, prev_pre_gain, target_pre_gain, early_refl_gain, late_gain;
    float direct[4], cur_fdn[4], cur_out[4], prev_fb[4];
} Reverb3d;

static const float r3_fdn_min[4] = {5.0f, 6.0f, 13.0f, 14.0f};
static const float r3_fdn_max[4] = {45.704f, 82.782f, 149.94f, 271.58f};
static const float r3_decay_max1[4] = {17.0f, 13.0f, 9.0f, 7.0f};
static const float r3_decay_max2[4] = {19.0f, 11.0f, 10.0f, 6.0f};
static const float r3_early_times[20] = {0.017136f, 0.059154f, 0.16173f, 0.39019f, 0.42526f, 0.45541f, 0.68974f,
    0.74591f, 0.83384f, 0.8595f, 0.0f, 0.075024f, 0.16879f, 0.2999f, 0.33744f, 0.3719f, 0.59901f, 0.71674f, 0.81786f,
    0.85166f};
static const float r3_early_gain[20] = {0.67096f, 0.61027f, 1.0f, 0.35680f, 0.68361f, 0.65978f, 0.51939f, 0.24712f,
    0.45945f, 0.45021f, 0.64196f, 0.54879f, 0.92925f, 0.38270f, 0.72867f, 0.69794f, 0.5464f, 0.24563f, 0.45214f,
    0.44042f};

static float pow10c(float x) { return x >= 0 ? 1.0f : x <= -5.3f ? 0.0f : powf(10, x); }  // (capped at 1)
static float cos_deg(float d) { return cosf(d * (float)M_PI / 180); }
static float sin_deg(float d) { return sinf(d * (float)M_PI / 180); }
static uint32_t r3_rate(const Reverb3dParam *p) { return p->sample_rate >= 1000 && p->sample_rate <= 192000 ? p->sample_rate : 48000; }

static void reverb3d_update(Reverb3d *rv) {
    Reverb3dParam *p = &rv->p;
    float SR = (float)r3_rate(p);
    uint32_t sr = r3_rate(p) / 1000;  // samples per millisecond
    float decay = fmaxf(p->decay_time, 0.1f), ratio = fmaxf(p->hf_decay_ratio, 0.1f);
    float hf_ref = fminf(fmaxf(p->hf_reference, 20.0f), SR / 2);
    memset(rv->prev_fb, 0, sizeof rv->prev_fb);
    rv->prev_pre_value = 0;
    rv->early_refl_gain = pow10c(fminf(p->room_gain + p->reflections_gain, 5000.0f) / 2000.0f);
    rv->late_gain = pow10c(fminf(p->room_gain + p->reverb_gain, 5000.0f) / 2000.0f);
    float hf_gain = pow10c(p->room_hf / 2000.0f);
    if (hf_gain < 1.0f) {
        float a = 1.0f - hf_gain, b = 2.0f - 2.0f * hf_gain * cos_deg(256.0f * hf_ref / SR);
        float c = sqrtf(fmaxf(0, b * b - 4.0f * a * a));
        rv->prev_pre_gain = (b - c) / (2.0f * a);
        rv->target_pre_gain = 1.0f - rv->prev_pre_gain;
    } else {
        rv->prev_pre_gain = 0;
        rv->target_pre_gain = 1;
    }
    float refl_delay = fmaxf(p->reflection_delay, 0), reverb_delay = fmaxf(p->reverb_delay, 0);
    rv->refl_time = (uint32_t)roundf(sr * 1000.0f * (refl_delay + reverb_delay));
    float echo = 0.6f * p->diffusion * 0.01f;
    for (int i = 0; i < 4; i++) {
        d3_set(&rv->fdn[i], r3_fdn_min[i] + p->density / 100 * (r3_fdn_max[i] - r3_fdn_min[i]));
        uint32_t n = rv->fdn[i].count + rv->d1[i].dl.count + rv->d2[i].dl.count;
        float A = (-60.0f * n) / (decay * SR), B = A / ratio;  // dB per pass through the loop, low / high frequencies
        float C = cos_deg(64.0f * hf_ref / SR) / sin_deg(64.0f * hf_ref / SR);
        float D = pow10c((B - A) / 40.0f), E = pow10c((B + A) / 40.0f) * 0.7071f;
        rv->direct[i] = E * (D * C + 1.0f) / (C + D);
        rv->cur_fdn[i] = E * (1.0f - D * C) / (C + D);
        rv->cur_out[i] = (C - D) / (C + D);
        rv->d1[i].rate = echo;
        rv->d2[i].rate = echo * -0.9f;
    }
    for (int i = 0; i < 20; i++) {
        uint32_t n = (uint32_t)roundf(sr * (refl_delay * 1000.0f +
                                            r3_early_times[i] * 1000.0f * (reverb_delay * 0.9998f + 0.02f)));
        rv->early_time[i] = n < rv->pre.max ? n : rv->pre.max;
    }
}

static void reverb3d_init(Reverb3d *rv) {
    uint32_t sr = r3_rate(&rv->p) / 1000;
    for (int i = 0; i < 4; i++) {
        d3_init(&rv->fdn[i], sr, r3_fdn_max[i]);
        d3_init(&rv->d1[i].dl, sr, r3_decay_max1[i]);
        d3_init(&rv->d2[i].dl, sr, r3_decay_max2[i]);
    }
    d3_init(&rv->pre, sr, 400);
    d3_init(&rv->fc, sr, 5);
    reverb3d_update(rv);
    rv->ready = 1;
}

// in / out: the effect's channels (in place is fine). wet = 0 leaves only the dry part (the reverb switched off).
static void reverb3d_process(Reverb3d *rv, float *const *in, float *const *out, uint32_t n, float wet) {
    // early reflections: tap i goes to output channel oe[i] (surround: two taps per reflection, one to channel 5)
    static const int8_t oe1[20] = {0}, oe2[20] = {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1};
    static const int8_t oe4[20] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 3, 3, 3};
    static const int8_t oe6[40] = {4, 5, 0, 5, 0, 5, 1, 5, 1, 5, 1, 5, 1, 5, 2, 5, 2, 5, 2, 5, 1, 5, 1, 5, 1, 5, 0, 5,
                                   0, 5, 0, 5, 0, 5, 3, 5, 3, 5, 3, 5};
    static const int8_t tf6[6] = {0, 1, 2, 3, -1, 3};  // FDN line per output channel (front centre: its own delay)
    int ch = rv->p.ch, ne = ch == 6 ? 40 : 20;
    const int8_t *oe = ch == 1 ? oe1 : ch == 2 ? oe2 : ch == 4 ? oe4 : oe6;
    float dry = rv->p.dry_gain;
    for (uint32_t s = 0; s < n; s++) {
        float ov[6] = {0}, cin[6], fb[4], fo[4], v[4], pre = 0;
        float tap = d3_tap(&rv->pre, rv->refl_time, 1);
        for (int i = 0; i < ne; i++) {
            int k = ch == 6 ? i / 2 : i;
            ov[oe[i]] += d3_tap(&rv->pre, rv->early_time[k], 1) * r3_early_gain[k];
        }
        for (int c = 0; c < ch; c++) { cin[c] = in[c][s]; pre += cin[c]; ov[c] *= rv->early_refl_gain; }
        rv->prev_pre_value = pre * rv->target_pre_gain + rv->prev_pre_value * rv->prev_pre_gain;
        d3_update(&rv->pre, rv->prev_pre_value);
        for (int i = 0; i < 4; i++) {
            float x = d3_read(&rv->fdn[i]);
            fo[i] = x * rv->direct[i] + rv->prev_fb[i];
            rv->prev_fb[i] = x * rv->cur_fdn[i] + fo[i] * rv->cur_out[i];
        }
        fb[0] = fo[2] + fo[1];
        fb[1] = -fo[0] - fo[3];
        fb[2] = fo[0] - fo[3];
        fb[3] = fo[1] - fo[2];
        for (int i = 0; i < 4; i++) {
            v[i] = dd3_update(&rv->d2[i], dd3_update(&rv->d1[i], tap * rv->late_gain + fb[i]));
            d3_update(&rv->fdn[i], v[i]);
        }
        if (ch == 1) {
            out[0][s] = (ov[0] + v[0] + v[1]) * wet + cin[0] * dry;
        } else {
            for (int c = 0; c < ch; c++)
                if (ch != 6 || tf6[c] >= 0) out[c][s] = (ov[c] + v[ch == 6 ? tf6[c] : c]) * wet + cin[c] * dry;
            if (ch == 6) out[4][s] = (ov[4] + d3_update(&rv->fc, (v[2] - v[3]) * 0.5f)) * wet + cin[4] * dry;
        }
    }
}

// ------------------------------------------------------------------ delay (port of Ryujinx DelayState / DelayCommand)
typedef struct {  // DelayParameter (gains Q14)
    uint8_t input[6], output[6];
    uint16_t ch_max, ch;
    uint32_t delay_max, delay, sample_rate, in_gain, feedback, out_gain, dry_gain, spread, lowpass;
} DelayParam;

typedef struct {
    int ready;
    uint8_t raw[0x34];
    DelayParam p;
    DelayLine dl[6];
    float lpz[6], feedback, base, cross, lp_fb, lp_base;
} Delay;

static void delay_update(Delay *d) {
    d->feedback = q14((int32_t)d->p.feedback) * 0.98f;
    float spread = q14((int32_t)d->p.spread);
    d->base = (1.0f - spread) * d->feedback;
    d->cross = spread * (d->p.ch == 4 || d->p.ch == 6 ? 0.5f : 1.0f) * d->feedback;
    d->lp_fb = 0.95f * q14((int32_t)d->p.lowpass);
    d->lp_base = 1.0f - d->lp_fb;
}

static void delay_init(Delay *d) {
    uint32_t sr = (uint32_t)(d->p.sample_rate >> 14) / 1000;
    if (!sr || sr > 192) sr = 48;
    float max_ms = d->p.delay_max <= 10000 ? (float)d->p.delay_max : 10000.0f;
    for (int c = 0; c < d->p.ch; c++) {
        dl_init(&d->dl[c], sr, max_ms);
        dl_set(&d->dl[c], (float)d->p.delay);
    }
    memset(d->lpz, 0, sizeof d->lpz);
    delay_update(d);
    d->ready = 1;
}

static void delay_process(Delay *d, float *const *in, float *const *out, uint32_t n) {
    int ch = d->p.ch;
    float fb = q14((int32_t)d->p.feedback), b = d->base, x = d->cross;
    float in_gain = q14((int32_t)d->p.in_gain), dry = q14((int32_t)d->p.dry_gain), out_gain = q14((int32_t)d->p.out_gain);
    float m[6][6] = {{0}};  // feedback between the channels' delay lines (symmetric)
    if (ch == 1) m[0][0] = fb;
    else if (ch == 2) { float t[2][2] = {{b, x}, {x, b}}; for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) m[i][j] = t[i][j]; }
    else if (ch == 4) {
        float t[4][4] = {{b, x, x, 0}, {x, b, 0, x}, {x, 0, b, x}, {0, x, x, b}};
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) m[i][j] = t[i][j];
    } else {
        float t[6][6] = {{b, 0, x, 0, x, 0}, {0, b, x, 0, 0, x}, {x, x, b, 0, 0, 0},
                         {0, 0, 0, fb, 0, 0}, {x, 0, 0, 0, b, x}, {0, x, 0, 0, x, b}};
        memcpy(m, t, sizeof m);
    }
    for (uint32_t s = 0; s < n; s++) {
        float cin[6], dv[6];
        for (int c = 0; c < ch; c++) { cin[c] = in[c][s]; dv[c] = dl_read(&d->dl[c]); }
        for (int c = 0; c < ch; c++) {
            float t = cin[c] * in_gain;
            for (int k = 0; k < ch; k++) t += dv[k] * m[k][c];
            d->lpz[c] = d->lp_fb * d->lpz[c] + t * d->lp_base;
            dl_update(&d->dl[c], d->lpz[c]);
        }
        for (int c = 0; c < ch; c++) out[c][s] = cin[c] * dry + dv[c] * out_gain;
    }
}

struct SwrDspEffect {
    uint8_t type, enabled, channels, input[6], output[6];
    uint8_t parameters[0xa0];
    union { Reverb reverb; Reverb3d reverb3d; Delay delay; } state;
};

_Static_assert(sizeof(ReverbParam)==0x40,"Reverb wire layout");
_Static_assert(sizeof(Reverb3dParam)==0x48,"I3DL2 wire layout");
_Static_assert(sizeof(DelayParam)==0x34,"Delay wire layout");

static int valid_channels(unsigned ch){ return ch==1 || ch==2 || ch==4 || ch==6; }
static int32_t clamp_i32(int32_t v,int32_t lo,int32_t hi){ return v<lo?lo:v>hi?hi:v; }
static float clamp_finite(float v,float lo,float hi){ return !isfinite(v)?lo:v<lo?lo:v>hi?hi:v; }

void swr_dsp_free(SwrDspEffect* effect){
    if(!effect) return;
    if(effect->type==3){
        for(int i=0;i<6;i++) free(effect->state.delay.dl[i].buf);
    } else if(effect->type==4){
        Reverb* r=&effect->state.reverb;
        for(int i=0;i<4;i++){ free(r->fdn[i].buf); free(r->decay[i].dl.buf); }
        free(r->pre.buf); free(r->fc.buf);
    } else if(effect->type==5){
        Reverb3d* r=&effect->state.reverb3d;
        for(int i=0;i<4;i++){ free(r->fdn[i].buf); free(r->d1[i].dl.buf); free(r->d2[i].dl.buf); }
        free(r->pre.buf); free(r->fc.buf);
    }
    free(effect);
}

SwrDspEffect* swr_dsp_configure(SwrDspEffect* effect,const uint8_t config[0xc0]){
    const uint8_t* wire=config+0x20;
    uint16_t channels,maximum;
    memcpy(&maximum,wire+12,2); memcpy(&channels,wire+14,2);
    if(config[0]<3 || config[0]>5 || !valid_channels(channels) ||
       !valid_channels(maximum) || channels>maximum){ swr_dsp_free(effect); return NULL; }
    int reset=!effect || effect->type!=config[0] || config[1] || effect->channels!=channels;
    if(reset){
        swr_dsp_free(effect); effect=calloc(1,sizeof *effect);
        if(!effect) return NULL;
        effect->type=config[0]; effect->channels=(uint8_t)channels;
    }
    effect->enabled=config[2]!=0;
    memcpy(effect->input,wire,6); memcpy(effect->output,wire+6,6);
    if(!reset && !memcmp(effect->parameters,wire,sizeof effect->parameters)) return effect;
    memcpy(effect->parameters,wire,sizeof effect->parameters);
    if(effect->type==3){
        Delay* d=&effect->state.delay; DelayParam p; memcpy(&p,wire,sizeof p);
        p.delay_max=p.delay_max>10000?10000:p.delay_max;
        p.delay=p.delay>p.delay_max?p.delay_max:p.delay;
        if(p.sample_rate!=(32000u<<14) && p.sample_rate!=(48000u<<14)) p.sample_rate=48000u<<14;
        p.in_gain=(uint32_t)clamp_i32((int32_t)p.in_gain,0,16<<14);
        p.out_gain=(uint32_t)clamp_i32((int32_t)p.out_gain,0,16<<14);
        p.dry_gain=(uint32_t)clamp_i32((int32_t)p.dry_gain,0,16<<14);
        p.feedback=(uint32_t)clamp_i32((int32_t)p.feedback,0,16383);
        p.spread=(uint32_t)clamp_i32((int32_t)p.spread,0,1<<14);
        p.lowpass=(uint32_t)clamp_i32((int32_t)p.lowpass,0,1<<14);
        int init=reset || p.delay_max!=d->p.delay_max || p.delay!=d->p.delay || p.sample_rate!=d->p.sample_rate;
        d->p=p; if(init) delay_init(d); else delay_update(d);
    } else if(effect->type==4){
        Reverb* r=&effect->state.reverb; ReverbParam p; memcpy(&p,wire,sizeof p);
        p.pre_delay=clamp_i32(p.pre_delay,0,350<<14);
        p.decay_time=clamp_i32(p.decay_time,0,20<<14);
        p.hf_decay_ratio=clamp_i32(p.hf_decay_ratio,0,2<<14);
        p.coloration=clamp_i32(p.coloration,0,1<<14);
        p.early_gain=clamp_i32(p.early_gain,0,16<<14);
        p.late_gain=clamp_i32(p.late_gain,0,16<<14);
        p.reverb_gain=clamp_i32(p.reverb_gain,0,16<<14);
        p.out_gain=clamp_i32(p.out_gain,0,16<<14);
        p.dry_gain=clamp_i32(p.dry_gain,0,16<<14);
        if(p.sample_rate!=(32<<14) && p.sample_rate!=(48<<14)) p.sample_rate=48<<14;
        int init=reset || p.sample_rate!=r->p.sample_rate;
        r->p=p; if(init) reverb_init(r); else reverb_update(r);
    } else {
        Reverb3d* r=&effect->state.reverb3d; Reverb3dParam p; memcpy(&p,wire,sizeof p);
        if(p.sample_rate!=32000 && p.sample_rate!=48000) p.sample_rate=48000;
        p.room_hf=clamp_finite(p.room_hf,-10000,0);
        p.hf_reference=clamp_finite(p.hf_reference,20,20000);
        p.decay_time=clamp_finite(p.decay_time,.1f,20);
        p.hf_decay_ratio=clamp_finite(p.hf_decay_ratio,.1f,2);
        p.room_gain=clamp_finite(p.room_gain,-10000,0);
        p.reflections_gain=clamp_finite(p.reflections_gain,-10000,1000);
        p.reverb_gain=clamp_finite(p.reverb_gain,-10000,2000);
        p.diffusion=clamp_finite(p.diffusion,0,100);
        p.reflection_delay=clamp_finite(p.reflection_delay,0,.3f);
        p.reverb_delay=clamp_finite(p.reverb_delay,0,.1f);
        p.density=clamp_finite(p.density,0,100);
        p.dry_gain=clamp_finite(p.dry_gain,0,16);
        int init=reset || p.sample_rate!=r->p.sample_rate;
        r->p=p; if(init) reverb3d_init(r); else reverb3d_update(r);
    }
    return effect;
}

void swr_dsp_process(SwrDspEffect* effect,float* mix,unsigned buffers,unsigned frames){
    if(!effect || !mix || !frames || buffers>24) return;
    float* input[6]; float* output[6];
    for(unsigned ch=0;ch<effect->channels;ch++){
        if(effect->input[ch]>=buffers || effect->output[ch]>=buffers) return;
        input[ch]=mix+(size_t)effect->input[ch]*frames;
        output[ch]=mix+(size_t)effect->output[ch]*frames;
    }
    /* Copy all sources before writing any output: channel remaps can overlap. */
    if(frames>4096) return;
    float scratch[6][4096];
    for(unsigned ch=0;ch<effect->channels;ch++){
        memcpy(scratch[ch],input[ch],frames*sizeof(float)); input[ch]=scratch[ch];
    }
    if(!effect->enabled){
        for(unsigned ch=0;ch<effect->channels;ch++) memcpy(output[ch],input[ch],frames*sizeof(float));
    } else if(effect->type==3) delay_process(&effect->state.delay,input,output,frames);
    else if(effect->type==4) reverb_process(&effect->state.reverb,input,output,frames);
    else reverb3d_process(&effect->state.reverb3d,input,output,frames,1);
    for(unsigned ch=0;ch<effect->channels;ch++) for(unsigned f=0;f<frames;f++)
        if(!isfinite(output[ch][f])) output[ch][f]=0;
}

