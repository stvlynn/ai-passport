// main/voice_codec.c —— see voice_codec.h. Standard IMA ADPCM tables.
#include "voice_codec.h"

static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t INDEX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

// Shared state update, so encoder and decoder track the same predictor.
static void step_state(int16_t *pred, uint8_t *index, uint8_t nib) {
    int step = STEP[*index];
    int diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    int p = *pred + ((nib & 8) ? -diff : diff);
    if (p > 32767) p = 32767;
    if (p < -32768) p = -32768;
    *pred = (int16_t)p;
    int i = *index + INDEX[nib];
    *index = (uint8_t)(i < 0 ? 0 : i > 88 ? 88 : i);
}

static uint8_t encode(int16_t *pred, uint8_t *index, int sample) {
    int step = STEP[*index];
    int diff = sample - *pred;
    uint8_t nib = 0;
    if (diff < 0) { nib = 8; diff = -diff; }
    if (diff >= step) { nib |= 4; diff -= step; }
    if (diff >= step >> 1) { nib |= 2; diff -= step >> 1; }
    if (diff >= step >> 2) nib |= 1;
    step_state(pred, index, nib);
    return nib;
}

void voice_enc_init(voice_enc_t *e) {
    e->predictor = 0;
    e->index = 0;
    e->prev = 0;
}

size_t voice_enc_chunk(voice_enc_t *e, const int16_t *x, size_t n, uint8_t *out) {
    size_t bytes = 0;
    uint8_t pending = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        int left = i ? x[i - 1] : e->prev;
        int y = (left + 2 * x[i] + x[i + 1]) / 4;
        uint8_t nib = encode(&e->predictor, &e->index, y);
        if ((i / 2) & 1) out[bytes++] = (uint8_t)(pending | (nib << 4));
        else pending = nib;
    }
    if (n >= 2) e->prev = x[n - 1];
    return bytes;
}

void voice_dec_init(voice_dec_t *d) {
    d->predictor = 0;
    d->index = 0;
}

void voice_dec(voice_dec_t *d, const uint8_t *in, size_t samples, int16_t *out) {
    for (size_t i = 0; i < samples; i++) {
        uint8_t nib = (i & 1) ? (in[i / 2] >> 4) : (in[i / 2] & 15);
        step_state(&d->predictor, &d->index, nib);
        out[i] = d->predictor;
    }
}
