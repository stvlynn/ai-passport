// tests/test_voice_codec.c —— 16 kHz PCM -> 8 kHz IMA ADPCM round trip.
#include "voice_codec.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static int16_t in[16000 * VOICE_MAX_SECONDS], ref[VOICE_MAX_SAMPLES], dec[VOICE_MAX_SAMPLES];
    static uint8_t buf[VOICE_DATA_BYTES];
    for (size_t i = 0; i < sizeof(in) / sizeof(in[0]); i++) {
        double t = (double)i / 16000.0;
        in[i] = (int16_t)(8000 * sin(2 * M_PI * 440 * t) + 3000 * sin(2 * M_PI * 1250 * t));
    }
    voice_enc_t e;
    voice_enc_init(&e);
    size_t bytes = 0;
    // Encode in 20 ms chunks exactly like the audio task.
    for (size_t off = 0; off < sizeof(in) / sizeof(in[0]); off += 320) bytes += voice_enc_chunk(&e, in + off, 320, buf + bytes);
    assert(bytes == VOICE_DATA_BYTES);
    assert(VOICE_BODY_BYTES == 20004);

    voice_dec_t d;
    voice_dec_init(&d);
    voice_dec(&d, buf, VOICE_MAX_SAMPLES, dec);
    // Reference: the same decimation filter, without quantization.
    for (size_t j = 0; j < VOICE_MAX_SAMPLES; j++) {
        int left = j ? in[2 * j - 1] : 0;
        ref[j] = (int16_t)((left + 2 * in[2 * j] + in[2 * j + 1]) / 4);
    }
    double sig = 0, err = 0;
    for (size_t j = 400; j < VOICE_MAX_SAMPLES; j++) {   // skip the adaptation ramp
        sig += (double)ref[j] * ref[j];
        err += (double)(ref[j] - dec[j]) * (ref[j] - dec[j]);
    }
    double snr = 10 * log10(sig / err);
    printf("voice_codec: SNR %.1f dB\n", snr);
    assert(snr > 20);

    // Odd chunk boundaries keep the filter state continuous.
    voice_enc_t a, b;
    voice_enc_init(&a);
    voice_enc_init(&b);
    uint8_t x[160], y[160];
    size_t nx = voice_enc_chunk(&a, in, 640, x);
    size_t ny = voice_enc_chunk(&b, in, 320, y);
    ny += voice_enc_chunk(&b, in + 320, 320, y + ny);
    assert(nx == 160 && ny == 160 && memcmp(x, y, 160) == 0);
    puts("test_voice_codec: PASS");
    return 0;
}
